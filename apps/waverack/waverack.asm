; =============================================================================
; os8088 - apps/waverack/waverack.asm
;
; WAVERACK (SPEC.md 123): a sound recorder, WAV player and waveform editor
; behind the face of a rack unit - a silver faceplate, a recessed LCD with
; seven-segment time, a segmented LED level bar over an INPUT GAIN slider,
; picture transport keys, the waveform and its overview, the edit row, the
; format row and a VOLUME slider. The design is the owner's three mockups in
; docs/waverack/design/; the delivery record is docs/plans/WAVERACK-PLAN.md.
;
; WAVE 1 WAS THE FACE (US-01..US-03): the package, the three layouts in both
; themes, every control drawn from REAL state. WAVE 2 WAS THE DOCUMENT
; (US-04): File > New and Open (and OSAPI_ARG_FILE at launch), the RIFF walk
; (wrwav.inc), sources and the piece table (wrdoc.inc), the coarse envelope's
; scan, the view cache and its refinement, the waveform band, zoom, scroll,
; the overview and the cursor (wrenv.inc). WAVE 3 WAS THE TRANSPORT (US-05,
; US-08, US-07's playback half): Play, Pause, Stop, seek and Loop through the
; Sound Blaster's ring stream (wrxport.inc), the playhead, the meter while
; playing, and the selection - drag, handles, Shift, Select All / None. WAVE 4
; IS EDITING AND SAVING (US-09, US-10): Trim, Delete, Cut, Copy, Paste, Undo
; and Redo over the piece table (wredit.inc), the dirty title and the
; save-changes question, and Save, Save As and Save Selection As through a
; temp file and a rename (wrsave.inc). What a later wave builds (record) is
; GREYED through the one predicate, wr_cmdok, which a key, its menu item and
; its keystroke all ask (SPEC.md 47 rule 2), and a press on a greyed control
; says why on the status bar (47 rule 6).
;
; THE FILES (SPEC.md 123.1):
;   waverack.asm  this: the entry, the commands, the predicate, the menus, the
;                 keys and the callbacks
;   wrdraw.inc    the drawing primitives - roles, themes, the band
;   wrface.inc    the layouts' static parts, the elements, the key painter
;   wrwav.inc     the RIFF/WAVE reader (tools/wrsim.py is its reference)
;   wrdoc.inc     sources, the piece table, 32-bit frame arithmetic, reading
;   wrenv.inc     the envelopes, the view and its cache, the waveform band,
;                 the selection
;   wrxport.inc   the transport: the state machine, the look-ahead, the
;                 worker, the conversion, VOLUME, the playhead, the meter
;   wredit.inc    the edits, undo and redo, the clipboard, the source sweep
;   wrsave.inc    Save, Save As, Save Selection As: the temp file, the
;                 replace, the rebase
;   build/wrart.inc  the pictures, the fonts, the meter's table, the role
;                 tables and the three layouts as DATA, made and measured on
;                 the host by tools/os88wrart.py (and tools/os88wrlay.py)
;
; .WAV IS NOT CLAIMED: Audio owns the association (SPEC.md 86), and WaveRack
; opens a WAV through File > Open, or when another program starts it with
; one (OSAPI_ARG_FILE, SPEC.md 123.4).
;
; REGISTER CONTRACTS are the callbacks' (SPEC.md 111.6): W_PAINT and W_ONKEY
; give SI back; W_ONTIMER and the W_ONCLICK family may clobber AX, BX, CX,
; DX, DI and ES. Internal procs say what they clobber.
; =============================================================================

%include "os88api.inc"
%include "wrart.inc"                ; its constants and the icon (the data
                                    ; half is included with the data, below)

    OS88_HEADER 'WaveRack', wr_entry, OS88_F_ICON
    OS88_ICON16                     ; a rack panel with a wave on its LCD
    WRART_ICON16
    OS88_ICON16_END

WR_PARK     equ 0x7FF0              ; a key a layout lacks: off every screen
WR_GAINMAX  equ 12                  ; INPUT GAIN: 13 steps, -12..+12 dB, unity
WR_GAIN0    equ 6                   ; in the middle (SPEC.md 123.7)
WR_VOLSTEP  equ 5                   ; VOLUME's keys and wheel: 5% a step
WD_BANDSZ   equ 1024                ; the composing band (wrdraw.inc)

; -DWR_CALLS counts every drawing primitive WaveRack calls into [wr_ncalls]
; (a fill, a frame, a font run, a blit): the instrument the redraw costs in
; SPEC.md 123.13 were measured with (PERFORMANCE.md's rule 4 - a counter, not
; a timer). The shipped build carries none of it
%macro WR_NCALL 0
  %ifdef WR_CALLS
    pushf
    inc word [wr_ncalls]
    popf
  %endif
%endmacro
WF_NKEYS    equ WRE_N               ; element keys: one a layout element

; --- the commands: a key, a menu item and a keystroke each name one, and
;     wr_cmdok is the ONE predicate over them (SPEC.md 47 rule 2). The menu
;     items are in this order, so an item is wr_mbase[menu] + its index
C_NEW       equ 0                   ; File
C_OPEN      equ 1
C_SAVE      equ 2
C_SAVEAS    equ 3
C_SAVESEL   equ 4
C_RECOVER   equ 5
C_UNDO      equ 6                   ; Edit
C_REDO      equ 7
C_CUT       equ 8
C_COPY      equ 9
C_PASTE     equ 10
C_DELETE    equ 11
C_TRIM      equ 12
C_SELALL    equ 13
C_SELNONE   equ 14
C_ZIN       equ 15                  ; View
C_ZOUT      equ 16
C_FIT       equ 17
C_LAYOUT    equ 18
C_THEME     equ 19
C_PLAY      equ 20                  ; Sound: Play / Pause
C_STOP      equ 21
C_RECORD    equ 22
C_LOOP      equ 23
C_RATE      equ 24
C_NMENU     equ 25                  ; ...the menus' items end here
C_PAUSE     equ 25                  ; the Pause key's own
C_CURSOR    equ 26                  ; the cursor keys (a document's)
C_PLAYK     equ 27                  ; the Play key's own (latched while playing)
K_VOLDN     equ 0xF0                ; keystrokes that are not commands
K_VOLUP     equ 0xF1
K_GAINDN    equ 0xF2
K_GAINUP    equ 0xF3
SC_LEFT     equ 0x4B                ; C_CURSOR's keys, by scan code
SC_RIGHT    equ 0x4D
SC_HOME     equ 0x47
SC_END      equ 0x4F
SC_PGUP     equ 0x49
SC_PGDN     equ 0x51

; =============================================================================
; ENTRY
; =============================================================================

; -----------------------------------------------------------------------------
; wr_entry - the package entry point (SPEC.md 20.2)
; out: BX = the window, CF clear (set = abort). Nothing here may draw.
; -----------------------------------------------------------------------------
wr_entry:
    call OSAPI_ARG_FILE             ; launched to open a document? (read and
    jc .noarg                       ; clear: ask first). ES = KERNEL_SEG
    mov di, wo_nbuf
    mov cx, 13
.an:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .an
    mov byte [wo_nbuf+13], 0
    mov [wr_argdir], dx
    mov [wr_argvol], bl
    mov byte [wr_argpend], 1        ; opened from the first tick
.noarg:
    call OSAPI_SND_CAPS             ; what the machine can do (SPEC.md 123.2)
    mov [wr_caps], ax
    call OSAPI_VIDEO                ; the primary: colour by default where it
    mov byte [wr_theme], 1          ; has more than one bit (SPEC.md 123.11)
    cmp dh, 1
    jbe .bw
    mov byte [wr_theme], 0
.bw:
    sub cx, MBAR_H                  ; its desktop band, for the size table
    mov [wr_pband], cx
    mov ax, WRL_BANDKB              ; the waveform band (SPEC.md 123.5): a
    call OSAPI_MEM_CLAIM            ; refusal is not fatal - the waveform is
    jnc .band                       ; then drawn without its grid
    xor dx, dx
.band:
    mov [wr_bseg], dx
    mov word [wv_ww], 0xFFFF        ; no width yet: the first paint's
    mov byte [wr_gain], WR_GAIN0
    mov byte [wr_vol], 75
    call wt_vbuild                  ; VOLUME's table (SPEC.md 123.6)
    mov byte [wr_rate], 1           ; the next take at 11,025 Hz
    mov si, wr_s_ready
    call wr_say
    mov word [wd_lp], wrart_l_cmp   ; a layout to stand on before the paint
    mov word [wd_th], wrart_tbw
    mov si, wr_tpl
    call OSAPI_WM_CREATE            ; BX = the window
    jc wr_ret
    OS88_REGION_MOVABLE_NOKEEP wr_ret ; every byte that outlives a pass is a
                                    ; static or in a pinned claim
    mov [wr_win], bx
    mov ax, wr_ontimer              ; the scan, the refinement, a launch's
    call OSAPI_WM_ONTIMER           ; document, the playhead: all from the
    mov ax, wr_onwake               ; one-shot timer; the refills and the
    call OSAPI_WM_ONWAKE            ; play's end from the worker's wake
    mov ax, wr_onclose              ; (SPEC.md 123.6); and a close stops the
    call OSAPI_WM_ONCLOSE           ; play first
    cmp byte [wr_argpend], 0
    je .noopen
    call wr_arm
.noopen:
    mov di, wr_drops                ; the drop-downs' window and counts
    mov si, wr_dropn
    mov cx, WR_NDROP
.dw:
    mov [di+OS88UI_DR_WIN], bx
    mov al, [si]
    xor ah, ah
    mov [di+OS88UI_DR_N], ax
    inc si
    add di, OS88UI_DR_SIZE
    loop .dw
    mov word [wr_drops + OS88UI_DR_SIZE + OS88UI_DR_SEL], 1
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on the 8-pixel grid
    mov al, 1
    call OSAPI_WM_OWNBG             ; every pixel is the paint's
    mov si, wr_about
    call OSAPI_ABOUT_SET
    mov si, wr_menus
    call OSAPI_MENU_SET
    mov ax, wr_wheel
    call OSAPI_WM_ONWHEEL
    call wr_prefset
    mov si, wr_pref
    call OSAPI_WM_PREFER            ; the size, per adapter - and applied
    xchg ax, bx                     ; AX = the window
    mov bx, wr_btrec
    mov si, wr_onup
    mov di, wr_ondrag
    mov dx, wr_onclick
    call os88ui_btninit             ; keeps every register
    xchg ax, bx
    clc
wr_ret:
    ret

; -----------------------------------------------------------------------------
; wr_prefset - the OSAPI_WM_PREFER table for what the user wants: FULL on a
;              VGA (MID where the primary's band is EGA's 350 lines), MID on
;              Hercules, COMPACT on the CGA - and COMPACT everywhere once
;              asked for. clobbers AX
; -----------------------------------------------------------------------------
wr_prefset:
    mov word [wr_pref + 8], WRL_CMP_W + 2
    mov word [wr_pref + 10], WRL_CMP_H + TITLE_H + 1
    cmp byte [wr_want], 0
    jne .cmp
    mov word [wr_pref], WRL_FULL_W + 2
    mov word [wr_pref + 4], WRL_MID_W + 2
    mov word [wr_pref + 6], WRL_MID_H + TITLE_H + 1
    mov ax, WRL_FULL_H + TITLE_H + 1
    cmp word [wr_pband], ax
    jae .vga
    mov ax, WRL_MID_H + TITLE_H + 1
.vga:
    mov [wr_pref + 2], ax
    ret
.cmp:
    mov ax, WRL_CMP_W + 2
    mov [wr_pref], ax
    mov [wr_pref + 4], ax
    mov ax, WRL_CMP_H + TITLE_H + 1
    mov [wr_pref + 2], ax
    mov [wr_pref + 6], ax
    ret

; =============================================================================
; THE PREDICATE, THE REASONS, THE COMMANDS
; =============================================================================

; -----------------------------------------------------------------------------
; wr_cmdok - would command AL do anything? (SPEC.md 47 rule 2: the ONE
;            predicate, shared by the keys' greying, the menus and the keys)
; out: CF = 0 live, CF = 1 refused; every register preserved
;
; The transport's commands follow SPEC.md 123.6's table - Play with a card and
; frames, Pause and Stop while a play is held. The document's - New, Open, the
; edits and the saves - want the transport IDLE (`Stop first`: the table's
; "no" for PLAYING, so an edit never mutates what is being played; Copy alone
; is live under a play, the document unchanged), no save running (`Still
; saving`), and a scanned document; each its own condition besides (wave 4,
; SPEC.md 123.9, 123.10). Record is a later wave's, refused until it lands.
; -----------------------------------------------------------------------------
wr_cmdok:
    cmp al, C_LOOP
    je .yes
    cmp al, C_RATE
    je .yes
    cmp al, C_THEME
    je .theme
    cmp al, C_LAYOUT
    je .layout
    cmp al, C_STOP
    je .stop
    cmp al, C_ZIN
    je .zin
    cmp al, C_ZOUT
    je .zout
    cmp al, C_FIT
    je .fit
    cmp al, C_CURSOR
    je .frames
    cmp al, C_SELALL
    je .selall
    cmp al, C_SELNONE
    je .selnone
    cmp al, C_PAUSE
    je .pause
    cmp al, C_COPY                  ; Copy: a selection, even under a play
    je .copy
    cmp al, C_RECORD                ; (Record, Recover Take: wave 5's)
    je .no
    cmp al, C_RECOVER
    je .no
    call wr_asking                  ; every other command waits for a
    jnc .no                         ; question to be answered (the alerts
    cmp byte [ws_busy], 0           ; are not modal: wave 4's review, H2)...
    jne .no                         ; ...and for a save
    cmp al, C_PLAY
    je .play
    cmp al, C_PLAYK
    je .play
    call wt_active                  ; ...and for the transport to be idle
    jnc .no
    cmp al, C_OPEN
    je .open
    cmp al, C_PASTE
    je .paste
    cmp al, C_NEW
    je .new
    call we_ready                   ; the rest want a scanned document
    jc .no
    cmp al, C_SAVEAS
    je .yes
    cmp al, C_SAVE
    je .save
    cmp al, C_UNDO
    je .undo
    cmp al, C_REDO
    je .redo
    jmp short .selected             ; Trim, Delete, Cut, Save Selection As
.no:
    stc
    ret
.yes:
    clc
    ret
.open:
    push ax                         ; Open: a source record the clipboard does
    call wx_clipsrcs                ; not hold (SPEC.md 123.3)
    cmp ax, WO_NSRC
    pop ax
    jae .no
    clc
    ret
.new:
    cmp byte [wo_doc], 0            ; New: a document to empty
    je .no
    clc
    ret
.paste:
    cmp word [wo_ncp], 0            ; Paste: a clipboard of the document's
    je .no                          ; format (none: it is adopted), and a
    call wx_clipfmt                 ; document scanned when there is one
    jc .no
    cmp byte [wo_doc], 0
    je .yes
    jmp we_ready
.save:
    cmp byte [wo_titled], 0         ; Save: an untitled document (it is Save
    je .yes                         ; As), or an edited one
    cmp byte [wo_dirty], 0
    je .no
    clc
    ret
.undo:
    cmp byte [wo_upos], 0           ; Undo: a state before this one
    je .no
    clc
    ret
.redo:
    push ax                         ; Redo: one after it
    mov al, [wo_upos]
    inc al
    cmp al, [wo_un]
    pop ax
    jae .no
    clc
    ret
.copy:
    cmp byte [ws_busy], 0
    jne .no
    call we_ready
    jc .no
.selected:
    jmp we_hassel
.frames:
    cmp byte [wo_doc], 0            ; the cursor: frames to stand on
    je .no
    push ax
    mov ax, [wo_frames]
    or ax, [wo_frames+2]
    pop ax
    jz .no
    clc
    ret
.selall:
    call we_ready                   ; Select All: frames, scanned
    jc .no
    jmp short .frames
.selnone:
    jmp we_hassel                   ; Select None: a selection
.stop:
    cmp byte [ws_busy], 0           ; Stop: a save to cancel...
    jne .yes
    call wt_active                  ; ...a play held...
    jnc .yes
    cmp byte [we_busy], 1           ; ...or a scan to cancel (SPEC.md 123.5)
    jne .no                         ; - not a save's rescan: that document
    cmp byte [ww_note], N_REBASE    ; is saved, and stays
    je .no
    clc
    ret
.pause:
    jmp wt_active                   ; Pause: a play held
.play:
    call wt_active                  ; Play: a play held (pause, resume)...
    jnc .yes
    test word [wr_caps], SND_CAP_PCM_BG ; ...or a card, and frames scanned
    jz .no
    call we_ready
    jc .no
    jmp short .frames
.zin:
    call we_ready
    jc .no
    cmp byte [wv_z], 0
    je .no
    clc
    ret
.zout:
    call we_ready
    jc .no
    push ax
    mov al, [wv_z]
    cmp al, [wv_zfit]
    pop ax
    jae .no
    clc
    ret
.fit:
    call we_ready
    jc .no
    push ax
    mov al, [wv_z]
    cmp al, [wv_zfit]
    jne .fitp
    mov ax, [wv_first]
    or ax, [wv_first+2]
.fitp:
    pop ax
    je .no
    clc
    ret
.theme:
    cmp byte [wr_bpp1], 0           ; one bit: one theme
    jne .no
    clc
    ret
.layout:
    push si                         ; COMPACT -> the big one where it fits;
    mov si, [wd_lp]                 ; the big one -> COMPACT always
    cmp word [si+WL_CMP], 0
    pop si
    je .yes
    jmp wr_canbig

; wr_asking - CF = 0 while a question of ours is up: Save changes (its
; action in [wr_qact]) or Replace ([ws_rq]). keeps all
wr_asking:
    cmp byte [wr_qact], 0
    jne .yes
    cmp byte [ws_rq], 0
    jne .yes
    stc
    ret
.yes:
    clc
    ret

; wt_idle - CF = 0 when the transport is idle (wr_cmdok's). keeps all
wt_idle:
    call wt_active
    cmc
    ret

; wr_canbig - CF = 0 when the window's display holds MID at least. keeps all
wr_canbig:
    cmp word [wr_dband], WRL_MID_H + TITLE_H + 1
    jb .no
    cmp word [wr_dwid], WRL_MID_W + 2
    jb .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wr_explain - command AL was refused: the status bar says why (SPEC.md 47
;              rule 6, 123.2), in wr_cmdok's order. clobbers SI
; -----------------------------------------------------------------------------
wr_explain:
    mov si, wr_s_bwonly
    cmp al, C_THEME
    je .say
    mov si, wr_s_noroom
    cmp al, C_LAYOUT
    je .say
    cmp al, C_STOP
    je .stop
    cmp al, C_PAUSE
    je .pause
    cmp al, C_SELNONE
    je .nosel
    cmp al, C_RECORD
    je .rec
    mov si, wr_s_notyet
    cmp al, C_RECOVER
    je .say
    cmp al, C_CURSOR
    je .nodoc
    cmp al, C_ZIN
    je .view
    cmp al, C_ZOUT
    je .view
    cmp al, C_FIT
    je .view
    cmp al, C_SELALL
    je .view
    cmp al, C_COPY
    je .copy
    call wr_asking                  ; the document's commands and Play: a
    jnc .answer                     ; question up, first (wr_cmdok's order);
    cmp al, C_PLAY                  ; then a save, a play, no document, a
                                    ; scan - then their own
    je .play
    cmp al, C_PLAYK
    je .play
    mov si, wr_s_saving
    cmp byte [ws_busy], 0
    jne .say
    mov si, wr_s_stopfirst
    call wt_active
    jnc .say
    cmp al, C_OPEN
    je .open
    cmp al, C_PASTE
    je .paste
    cmp byte [wo_doc], 0
    je .nodoc
    mov si, wr_s_busy
    cmp byte [we_busy], 1
    je .say
    mov si, wr_s_clean
    cmp al, C_SAVE
    je .say
    mov si, wr_s_noundo
    cmp al, C_UNDO
    je .say
    mov si, wr_s_noredo
    cmp al, C_REDO
    je .say
.nosel:
    mov si, wr_s_nosel              ; Trim, Delete, Cut, Copy, Save Selection
.say:                               ; As, Select None
    jmp wr_say
.nodoc:
    mov si, wr_s_nodoc
    jmp short .say
.answer:
    push bx                         ; ...and the question put back in front:
    mov bx, [os88ui_awin]           ; a click into WaveRack can bury it whole,
    or bx, bx                       ; and an alert has no dock tile (os88ui's
    jz .an                          ; own re-front, OSAPI_WM_SHOW)
    call OSAPI_WM_SHOW
.an:
    pop bx
    mov si, wr_s_answer
    jmp short .say
.open:
    mov si, ww_e17                  ; 'Clipboard uses 4 files'
    jmp short .say
.paste:
    mov si, wr_s_noclip
    cmp word [wo_ncp], 0
    je .say
    call wx_clipfmt
    jnc .pscan
    push ax
    push dx
    push di
    call wx_clipsay                 ; 'Clip: 22050 16b stereo'
    pop di
    pop dx
    pop ax
    ret
.pscan:
    mov si, wr_s_busy
    jmp short .say
.copy:
    mov si, wr_s_saving
    cmp byte [ws_busy], 0
    jne .say
    jmp short .view
.stop:
    mov si, wr_s_nostop
    jmp short .say
.pause:
    mov si, wr_s_noplay             ; Pause: nothing playing, or no card
.play:
    test word [wr_caps], SND_CAP_PCM_BG
    jz .nocard
    cmp al, C_PAUSE
    je .say
    mov si, wr_s_saving
    cmp byte [ws_busy], 0
    jne .say
    cmp byte [wo_doc], 0            ; Play: no document, or still scanning
    je .nodoc
    mov si, wr_s_busy
    jmp short .say
.nocard:
    mov si, wr_s_nocard
    jmp short .say
.rec:
    mov si, wr_s_notyet
    test word [wr_caps], SND_CAP_PCM_IN
    jnz .say
    mov si, wr_s_nocap
    jmp .say
.view:
    cmp byte [wo_doc], 0            ; no document, still scanning, or the
    je .nodoc                       ; zoom at its end
    mov si, wr_s_busy
    cmp byte [we_busy], 1
    je .say
    cmp al, C_COPY
    je .nosel
    mov si, wr_s_nodoc              ; (Select All: no frames)
    cmp al, C_SELALL
    je .say
    mov si, wr_s_zinmax
    cmp al, C_ZIN
    je .say
    mov si, wr_s_whole
    jmp .say

; -----------------------------------------------------------------------------
; wr_cmd - do command AL (it is live: the caller asked wr_cmdok)
; in:  BX = the window. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wr_cmd:
    mov ah, WRQ_NEW                 ; New and Open: an edited document is
    cmp al, C_NEW                   ; asked about first (wr_ask)
    je .ask
    mov ah, WRQ_OPEN
    cmp al, C_OPEN
    jne .e0
.ask:
    mov al, ah
    call wr_ask
    jc .out                         ; asked: the answer does the rest
    jmp wr_qrun
.e0:
    mov si, wx_trim                 ; the edits and the saves (wredit.inc,
    cmp al, C_TRIM                  ; wrsave.inc)
    je .run
    mov si, wx_delete
    cmp al, C_DELETE
    je .run
    mov si, wx_cut
    cmp al, C_CUT
    je .run
    mov si, wx_copy
    cmp al, C_COPY
    je .run
    mov si, wx_paste
    cmp al, C_PASTE
    je .run
    mov si, wx_undo
    cmp al, C_UNDO
    je .run
    mov si, wx_redo
    cmp al, C_REDO
    je .run
    mov byte [ws_qact], 0           ; a save from a key or a menu carries no
    mov si, ws_save                 ; deferred action (only the question's
    cmp al, C_SAVE                  ; does: wr_onask)
    je .run
    mov si, ws_saveas
    cmp al, C_SAVEAS
    je .run
    mov si, ws_savesel
    cmp al, C_SAVESEL
    jne .c2
.run:
    jmp si
.c2:
    cmp al, C_STOP
    jne .c3
    cmp byte [ws_busy], 0           ; a save: cancelled, its temp file gone
    je .nsave
    jmp ws_cancel
.nsave:
    call wt_active                  ; a play: stopped (SPEC.md 123.6)
    jc .scan
    jmp wt_stop
.scan:
    call wo_new                     ; a scan abandoned: the open with it
    mov si, wr_s_cancel
    jmp wr_say
.c3:
    cmp al, C_ZIN
    jne .c4
    mov al, -1
    jmp we_zoom
.c4:
    cmp al, C_ZOUT
    jne .c5
    mov al, 1
    jmp we_zoom
.c5:
    cmp al, C_FIT
    jne .c6
    xor al, al
    jmp we_zoom
.c6:
    cmp al, C_CURSOR
    jne .t1
    jmp wr_cursor
.t1:
    cmp al, C_PLAY                  ; Space: play, pause, resume
    jne .t2
    cmp byte [wt_state], WT_PLAY
    jne .t3
    jmp wt_pause
.t2:
    cmp al, C_PLAYK                 ; the Play key: play, resume - latched
    jne .t4                         ; while playing
    cmp byte [wt_state], WT_PLAY
    je .out
.t3:
    cmp byte [wt_state], WT_PPAUSE
    jne .go
    jmp wt_resume
.go:
    jmp wt_go
.t4:
    cmp al, C_PAUSE                 ; the Pause key: pause - and resume, when
    jne .t5                         ; latched
    cmp byte [wt_state], WT_PPAUSE
    je .t3
    jmp wt_pause
.t5:
    cmp al, C_SELALL
    jne .t6
    jmp we_selall
.t6:
    cmp al, C_SELNONE
    jne .c7
    jmp we_selnone
.c7:
    cmp al, C_LOOP
    jne .n1
    xor byte [wr_loop], 1
    mov si, wr_s_loop0
    cmp byte [wr_loop], 0
    je .say
    mov si, wr_s_loop1
.say:
    jmp wr_say
.n1:
    cmp al, C_RATE
    jne .n2
    mov al, [wr_rate]
    xor al, 1
    jmp wr_setrate
.n2:
    cmp al, C_THEME
    jne .n3
    xor byte [wr_theme], 1
    jmp wr_repaint                  ; a theme is every pixel
.n3:
    cmp al, C_LAYOUT
    jne .out
    jmp wr_relayout
.out:
    ret

; -----------------------------------------------------------------------------
; wr_cursor - C_CURSOR, by AH = its scan code: Left / Right one column (one
;             frame at fpp = 1), Home / End to the ends, PgUp / PgDn a page
;             of the view (seven eighths of it). clobbers AX, BX, CX, DX, SI,
;             DI
; -----------------------------------------------------------------------------
wr_cursor:
    cmp ah, SC_PGUP
    je .page
    cmp ah, SC_PGDN
    je .page
    call wr_shift                   ; Shift+Left / Right: the selection
    jz .plain                       ; extended (SPEC.md 123.9)
    cmp ah, SC_LEFT
    mov al, -1
    je .ext
    cmp ah, SC_RIGHT
    jne .plain
    mov al, 1
.ext:
    jmp we_extend
.plain:
    call wt_active                  ; a play held: the step is from the
    jc .sel                         ; PLAYHEAD, where the user is looking
    push ax
    mov ax, [wt_ph]
    mov [wo_cur], ax
    mov ax, [wt_ph+2]
    mov [wo_cur+2], ax
    pop ax
.sel:
    push ax                         ; a plain move: the selection goes, the
    push bx                         ; cursor moves - and a play seeks there
    call we_selnone
    pop bx
    pop ax
    push bx
    call wr_cmove
    pop bx
    call wt_active
    jc .out
    jmp wt_seek
.page:
    call we_ready                   ; a scanned document: the view moves
    jc .out
    push ax
    mov ax, [we_ww]
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    sub ax, cx
    pop cx                          ; CH = the scan code
    cmp ch, SC_PGUP
    jne .pd
    neg ax
.pd:
    jmp we_scroll
.out:
    ret

; wr_cmove - the cursor by AH's key: Home, End, Left, Right. clobbers AX,
; BX, CX, DX, SI, DI
wr_cmove:
    cmp ah, SC_HOME
    jne .end
    xor ax, ax
    xor dx, dx
    jmp we_setcur
.end:
    cmp ah, SC_END
    jne .step
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
    jmp we_setcur
.step:
    push ax
    call we_fpp                     ; CX:BX = one column of frames
    mov bx, ax
    mov cx, dx
    pop ax
    cmp ah, SC_LEFT
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    jne .right
    sub ax, bx
    sbb dx, cx
    jmp we_setcur                   ; (negative: clamped to 0)
.right:
    add ax, bx
    adc dx, cx
    jmp we_setcur

; -----------------------------------------------------------------------------
; wr_open - File > Open...: the Standard File dialog (SPEC.md 38); its
;           answer is wr_dlgdone. in: BX = the window. clobbers AX, SI, DI
; -----------------------------------------------------------------------------
wr_open:
    mov al, FDLG_OPEN
    mov di, wr_dlgdone
    xor si, si
    call OSAPI_FILE_DLG
    jnc .out
    mov si, wr_s_dlg                ; one is up already
    jmp wr_say
.out:
    ret

; wr_dlgdone - the dialog's answer: AL = its mode, SI = the window, ES:DI =
; the name (the kernel's: copied), DX:CX its size. The instance stands in the
; folder the user chose. The dialog lay over the content: repainted whole
wr_dlgdone:
    push si
    mov si, di
    mov di, wo_nbuf
    mov cx, 13
.c:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [wo_nbuf+13], 0
    pop bx
    push bx
    call wo_open                    ; CF = 1 refused: the status bar says why
    pop bx
    jmp wr_repaint

; wr_arm - the one-shot timer in one tick (SPEC.md 13.9). keeps every register
wr_arm:
    push ax
    push bx
    mov bx, [wr_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_ontimer - W_ONTIMER: a launch's document opened, one gulp of the scan,
;              one refinement read, or - while PLAYING - the playhead and the
;              meter (wt_tick; a refinement waits for the play to stop: its
;              reads would race the look-ahead's) - then what changed is
;              drawn. Each step re-arms the timer while it has more to do.
; in:  SI = the window. Keeps SI and BP
; -----------------------------------------------------------------------------
wr_ontimer:
    push si
    push bp
    mov bx, si
    cmp byte [wr_argpend], 0
    je .work
    mov byte [wr_argpend], 0        ; OSAPI_ARG_FILE's: stand in its folder,
    mov dx, [wr_argdir]             ; then open it
    mov bl, [wr_argvol]
    call OSAPI_FILE_GOTO_QM
    jc .noarg
    mov bx, si
    call wo_open
    jmp short .draw
.noarg:
    mov byte [ww_err], E_NOFILE
    call ww_say
    jmp short .draw
.work:
    cmp byte [ws_busy], 0           ; a save's gulp (wrsave.inc); a
    je .play                        ; refinement waits for it
    call ws_step
    jmp short .draw
.play:
    cmp byte [wt_state], WT_PLAY
    jne .scan
    call wt_tick
    jmp short .draw
.scan:
    cmp byte [we_busy], 1
    jne .ref
    call we_scanstep
    jmp short .draw
.ref:
    cmp byte [we_busy], 2
    jne .draw
    call we_refstep
.draw:
    pop bp
    pop bx
    push bx
    call wr_redraw
    pop si
    ret

; wr_onwake - OSAPI_WM_ONWAKE (UI task, the gfx lock NOT held): the
; worker's refill and the play's end (wrxport.inc). SI = the window
wr_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call wt_onwake
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wr_onclose - the close negotiator (SPEC.md 75.1): a save running is
; cancelled (its temp file deleted), an edited document is asked about
; (CF = 1: not yet - wr_onask closes it when it is answered), and a play is
; stopped and every claim it held given back before the window goes - the
; stream closed, so the card is never left running. CF = 0 lets it close
wr_onclose:
    push si
    call ws_cancel
    mov al, WRQ_CLOSE
    call wr_ask
    jc .not
    call wt_release
    pop si
    clc
    ret
.not:
    pop si
    stc
    ret

; =============================================================================
; THE SAVE-CHANGES QUESTION (SPEC.md 123.3): New, Open and Close throw the
; document away, and an EDITED one is asked about first with os88ui's alert
; (SPEC.md 75.3) - Save / Discard / Cancel, TexPad's shape (SPEC.md 69.12).
; The alert is a window of its own and not modal, so asking returns at once
; and wr_onask carries the action out when it is answered - or, for Save,
; hands it to the save, which carries it out only if the save SUCCEEDS
; ([ws_qact], ws_finish): a failed or cancelled save leaves the document
; here, still edited.
; =============================================================================
WRQ_NEW     equ 1
WRQ_OPEN    equ 2
WRQ_CLOSE   equ 3

; -----------------------------------------------------------------------------
; wr_ask - AL = WRQ_*: CF = 0 the document is not edited, go ahead; CF = 1 it
;          is - 'Save changes to NAME?' is up (or, one being up already, it
;          was raised and its action stands). keeps every register
; -----------------------------------------------------------------------------
wr_ask:
    cmp byte [wo_dirty], 0
    jne .ask
    clc
    ret
.ask:
    push ax
    push bx
    push si
    push di
    push es
    push ax
    mov di, wr_qbuf                 ; 'Save changes to NAME?' - 29 characters
    mov si, wr_s_qsave              ; at most, under OS88UI_AMAX
    call wr_put
    mov si, wo_name
    call wr_put
    mov word [di], '?'
    mov ax, KERNEL_SEG              ; os88ui_ask reads our caption out of the
    mov es, ax                      ; window record through ES (SPEC.md 20.1)
    mov bx, [wr_win]
    mov si, wr_qbuf
    mov di, wr_onask
    mov al, OS88UI_ASAVE
    call os88ui_ask
    pop ax
    jnc .q
    cmp al, WRQ_CLOSE               ; refused - a question is up already: a
    jne .o                          ; close takes it over, closing being the
.q:                                 ; stronger intent (SPEC.md 69.12; review,
    mov [wr_qact], al               ; L1)
.o:
    pop es
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; wr_onask - the alert answered: AL = 0 Save, 1 Discard, 2 Cancel or
; OS88UI_ACANCEL; SI = our window, the gfx lock held, the alert gone and what
; it covered repainted
wr_onask:
    mov ah, [wr_qact]
    mov byte [wr_qact], 0
    mov bx, si
    cmp al, 1
    ja .out                         ; Cancel / dismissed: nothing happens
    push ax                         ; THE STATE ASKED AGAIN (review, H2): the
    push bx                         ; alert was not modal, so a save or a play
    call ws_cancel                  ; begun under it ends before New, Open,
    call wt_active                  ; Close or Save act on the document
    jc .idle
    call wt_stop
.idle:
    pop bx
    pop ax
    or al, al
    jz .save
    mov al, ah                      ; Discard
    call wr_qrun
    jmp short .draw
.save:
    mov [ws_qact], ah               ; ...carried out when the save is done
    push bx
    call ws_save
    pop bx
.draw:
    jmp wr_redraw
.out:
    ret

; -----------------------------------------------------------------------------
; wr_qrun - carry out AL = WRQ_*: New (the document emptied), Open (the
;           dialog) or Close (the play released and the window closed -
;           DEFERRED, SPEC.md 75.2, so nothing may draw after: wr_redraw
;           sees [wr_closing]). in: BX = the window. clobbers AX, CX, DX, SI,
;           DI
; -----------------------------------------------------------------------------
wr_qrun:
    cmp al, WRQ_OPEN
    jne .n1
    jmp wr_open
.n1:
    cmp al, WRQ_CLOSE
    je .close
    call wo_new                     ; the document emptied, its claims back
    mov si, wr_s_new
    jmp wr_say
.close:
    push bx
    call ws_cancel                  ; a save under way: its temp file goes
    call wt_release                 ; (the negotiator is retired, SPEC.md
    pop bx                          ; 75.2: wr_onclose will not run; review,
    mov byte [wr_closing], 1        ; H2)
    call OSAPI_WM_CLOSE
    ret

; -----------------------------------------------------------------------------
; wr_retitle - THE DIRTY INDICATOR IS THE TITLE (SPEC.md 123.3): 'WaveRack -
;              NAME*' while edited, 'WaveRack - NAME' saved, 'WaveRack' with
;              no document - through OSAPI_WM_TITLE, which keeps the POINTER,
;              so the buffer is bss (TexPad's tp_retitle). Set only when it
;              changes: a caption is 17 rows. keeps every register
; -----------------------------------------------------------------------------
wr_retitle:
    push ax
    push bx
    push si
    push di
    mov ax, wr_ttl                  ; the program's own name...
    xor bl, bl
    cmp byte [wo_doc], 0
    je .set
    mov di, wr_tbuf2                ; ...or the document's
    mov si, wr_s_ttl
    call wr_put
    mov si, wo_name
    call wr_put
    cmp byte [wo_dirty], 0
    je .z
    mov byte [di], '*'
    inc di
.z:
    mov byte [di], 0
    mov si, wr_tbuf2                ; the same as it is? nothing to do
    mov di, wr_ttlbuf
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .copy
    inc si
    inc di
    or al, al
    jnz .cmp
    jmp short .out
.copy:
    mov si, wr_tbuf2
    mov di, wr_ttlbuf
    call wr_put
    mov byte [di], 0
    mov ax, wr_ttlbuf
    mov bl, 1
.set:
    cmp bl, 0                       ; the program's name: only when the
    jne .go                         ; document's was up
    cmp byte [wr_ttlbuf], 0
    je .out
    mov byte [wr_ttlbuf], 0
.go:
    mov bx, [wr_win]
    or bx, bx
    jz .out
    call OSAPI_WM_TITLE
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_wpress - a press in the waveform at content x CX (SPEC.md 123.9): Shift
;             extends the selection from its anchor (the cursor, with none);
;             a press on a selection's handle drags that end; otherwise the
;             cursor goes there, the selection goes, and a play seeks there.
;             A drag goes on in wr_ondrag. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wr_wpress:
    call wr_wcol                    ; CX = the column
    mov byte [wr_drag], 5           ; (5: an end held still)
    call wr_shift
    jz .nshift
    call we_hassel
    jnc .ext
    mov ax, [wo_cur]                ; none: from the cursor
    mov [wo_anc], ax
    mov ax, [wo_cur+2]
    mov [wo_anc+2], ax
.ext:
    call we_colframe
    jmp we_dragsel
.nshift:
    call we_handle                  ; a handle: the other end holds still
    jnc .out
    mov byte [wr_drag], 4           ; plain: the cursor, here
    call we_colframe
    mov [wo_anc], ax
    mov [wo_anc+2], dx
    push ax
    push dx
    call we_selnone
    pop dx
    pop ax
    call we_setcur
    call wt_active
    jc .out
    mov bx, [wr_win]
    jmp wt_seek
.out:
    ret

; wr_wdrag - the drag in the waveform at content x CX: the selection between
; the anchor and the column under the pointer. clobbers AX, BX, CX, DX, SI, DI
wr_wdrag:
    call wr_wcol
    call we_colframe
    jmp we_dragsel

; wr_shift - ZF = 0 when a Shift key is down (the BIOS's flags at 0040:0017,
; which the keyboard driver keeps; a plain read). keeps every register
wr_shift:
    push ax
    push es
    mov ax, 0x40
    mov es, ax
    test byte [es:0x17], 3
    pop es
    pop ax
    ret

; wr_ovat - CX = a content x over the overview: the view centred there.
; clobbers AX, BX, CX, DX, SI, DI
wr_ovat:
    call wr_wcol
    mov bx, cx
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
    mov cx, [we_ww]
    call wo_muldiv
    jmp we_centre

; wr_wcol - CX = a content x -> CX = the waveform's column, clamped. keeps
; the rest
wr_wcol:
    sub cx, [we_bx]
    jns .pos
    xor cx, cx
.pos:
    cmp cx, [we_ww]
    jb .in
    mov cx, [we_ww]
    dec cx
.in:
    ret

; wr_setrate - the next take's rate, AL = 0 (8,000 Hz) or 1 (11,025 Hz):
; the drop-down's pick and the status bar follow. clobbers AX, SI
wr_setrate:
    mov [wr_rate], al
    xor ah, ah
    mov [wr_drops + OS88UI_DR_SIZE + OS88UI_DR_SEL], ax
    mov si, wr_s_rate8
    or al, al
    jz .s
    mov si, wr_s_rate11
.s:
    jmp wr_say

; wr_dropdis - SI = a drop-down 0..3 -> DI = its flags: Input, Bits and
; Channels hold one choice each and are greyed (SPEC.md 123.7); Rate is
; wr_cmdok's. keeps everything else
wr_dropdis:
    mov di, OS88UI_DIS
    cmp si, 1
    jne .out
    push ax
    mov al, C_RATE
    call wr_cmdok
    pop ax
    jc .out
    xor di, di
.out:
    ret

; -----------------------------------------------------------------------------
; wr_say - SI = a NUL string for the status bar (at most 22 characters, the
;          COMPACT field's width - tools/os88wrlay.py STATUS_MAX). It is drawn
;          by the next pass. clobbers nothing
; -----------------------------------------------------------------------------
wr_say:
    push ax
    push cx
    push si
    push di
    mov byte [wr_stsel], 0          ; 'No selection' is up: the selection's
    cmp si, wr_s_nosel              ; next change must not leave it standing
    jne .c0                         ; over one (wr_selsaid)
    mov byte [wr_stsel], 1
.c0:
    mov di, wr_stbuf
    mov cx, 39
.c:
    lodsb
    mov [di], al
    inc di
    or al, al
    loopnz .c
    mov byte [di], 0
    inc word [wr_stkey]             ; a new message: a new key...
    cmp word [wr_stkey], 0xFFFF     ; ...never the forgotten one
    jne .out
    mov word [wr_stkey], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; wr_selsaid - a selection was made: a 'No selection' refusal still on the
; status bar would contradict the face, so it becomes 'Ready'. keeps all
wr_selsaid:
    cmp byte [wr_stsel], 0
    je .out
    push si
    mov si, wr_s_ready
    call wr_say
    pop si
.out:
    ret

; --- INPUT GAIN and VOLUME ---------------------------------------------------------

; wr_gainset - INPUT GAIN to AL (0..12), said in dB. clobbers AX, SI
wr_gainset:
    mov [wr_gain], al
    mov si, wr_s_gain               ; 'Input gain +nn dB'
    mov di, wr_tbuf2
    call wr_put
    mov al, [wr_gain]
    sub al, WR_GAIN0
    shl al, 1                       ; 2 dB a step
    mov ah, '+'
    jns .sg
    mov ah, '-'
    neg al
.sg:
    or al, al
    jnz .sgn
    mov ah, ' '
.sgn:
    mov [di], ah
    inc di
    call wr_num
    mov si, wr_s_db
    call wr_put
    mov byte [di], 0
    mov si, wr_tbuf2
    jmp wr_say

; wr_volset - VOLUME to AL (0..100), said. clobbers AX, SI, DI
wr_volset:
    mov [wr_vol], al
    call wt_vbuild                  ; the worker's table, at once
    mov si, wr_s_volume
    mov di, wr_tbuf2
    call wr_put
    mov al, [wr_vol]
    call wr_num
    mov byte [di], '%'
    mov byte [di+1], 0
    mov si, wr_tbuf2
    jmp wr_say

; wr_volstep - VOLUME AX (signed) steps of 5%. clobbers AX, CX, SI, DI
wr_volstep:
    mov cx, WR_VOLSTEP
    imul cx
    mov cl, [wr_vol]
    xor ch, ch
    add ax, cx
    jns .lo
    xor ax, ax
.lo:
    cmp ax, 100
    jbe .hi
    mov ax, 100
.hi:
    jmp wr_volset

; wr_gainstep - INPUT GAIN AX (signed) steps. clobbers AX, CX, SI, DI
wr_gainstep:
    mov cl, [wr_gain]
    xor ch, ch
    add ax, cx
    jns .lo
    xor ax, ax
.lo:
    cmp ax, WR_GAINMAX
    jbe .hi
    mov ax, WR_GAINMAX
.hi:
    jmp wr_gainset

; -----------------------------------------------------------------------------
; wr_relayout - between COMPACT and the big layout (View > Compact / Full
;               Layout): the size table for the new choice, and the window
;               resized to it on the display it is on
; in:  BX = the window. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wr_relayout:
    mov si, [wd_lp]
    mov byte [wr_want], 1
    cmp word [si+WL_CMP], 0
    je .size
    call wr_canbig                  ; compact now: the big one, if it fits
    jc .out
    mov byte [wr_want], 0
.size:
    call wr_prefset
    cmp byte [wr_want], 0
    jne .small
    mov cx, WRL_FULL_W + 2
    mov dx, WRL_FULL_H + TITLE_H + 1
    cmp word [wr_dband], dx
    jae .rs
    mov cx, WRL_MID_W + 2
    mov dx, WRL_MID_H + TITLE_H + 1
    jmp short .rs
.small:
    mov cx, WRL_CMP_W + 2
    mov dx, WRL_CMP_H + TITLE_H + 1
.rs:
    call OSAPI_WM_RESIZE            ; and the whole window repainted
.out:
    ret

; =============================================================================
; THE KEYS' GREYING (tests/btnsites.txt: this file calls os88ui_btn, so it
; writes the record too)
; =============================================================================

; -----------------------------------------------------------------------------
; wr_regrey - every key's flags from wr_cmdok; draw the ones that changed
;             (all when DL = 1). clobbers AX, BX, DI
; -----------------------------------------------------------------------------
wr_regrey:
    mov word [wr_btrec + OS88UI_BT_RECTS], wd_rects
    cmp byte [wr_mode], WRM_FACE    ; the About card or an open list: no keys
    jne .out
    mov word [wr_btrec + OS88UI_BT_N], WR_NB
    xor ax, ax
    mov di, wr_bflags
.one:
    call wr_bflag                   ; BX = the flags it should have
    cmp dl, 1
    je .draw
    cmp [di], bx
    je .next
.draw:
    mov [di], bx
    push ax
    push bx
    inc ax                          ; the index PLUS ONE (os88ui_btn's way)
    mov bx, wr_btrec
    call os88ui_btn                 ; keeps every register
    pop bx
    pop ax
.next:
    inc di
    inc di
    inc ax
    cmp al, WR_NB
    jb .one
.out:
    ret

; wr_bflag - AL = a key -> BX = its flags: OWN, and greyed when its command
; is refused. keeps everything else
wr_bflag:
    push ax
    mov bx, OS88UI_OWN
    cmp al, B_PLAY                  ; Play latched while playing, Pause while
    jne .np                         ; paused (SPEC.md 123.6)
    cmp byte [wt_state], WT_PLAY
    jne .np
    or bx, OS88UI_LATCH
.np:
    cmp al, B_PAUSE
    jne .cmd
    cmp byte [wt_state], WT_PPAUSE
    jne .cmd
    or bx, OS88UI_LATCH
.cmd:
    call wr_bcmd
    call wr_cmdok
    jnc .live
    or bx, OS88UI_DIS
.live:
    pop ax
    ret

; wr_bcmd - AL = a key -> AL = its command. keeps everything else
wr_bcmd:
    push bx
    mov bx, wr_btncmd
    xlatb
    pop bx
    ret

; =============================================================================
; DRAWING ENTRY POINTS
; =============================================================================

; -----------------------------------------------------------------------------
; wr_redraw - after the state moved: what changed, where the window can be
;             seen. in: BX = the window; gfx lock held. clobbers AX, CX, DX,
;             SI, DI
; -----------------------------------------------------------------------------
wr_redraw:
    cmp byte [wr_closing], 0        ; closing: the window is going (75.2)
    jne .out
    cmp byte [wr_mode], WRM_ABOUT   ; the About card is opaque over the face,
    je .out                         ; and its dismissal repaints the content
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one visible pixel
    jc .out
    call wd_origin
    xor dl, dl
    call wf_update
.out:
    ret

; wr_repaint - the whole content, now. BX = the window. clobbers AX, CX, DX,
; SI, DI
wr_repaint:
    cmp byte [wr_closing], 0
    jne wr_redraw.out
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, bx
    call wr_paint
.out:
    ret

; -----------------------------------------------------------------------------
; wr_paint - W_PAINT: the static parts, every element and every key; the
;            About card last
; in:  SI = the window (gfx lock held; NOT white-filled - OSAPI_WM_OWNBG).
;      Keeps SI
; -----------------------------------------------------------------------------
wr_paint:
    push si
    mov bx, si
    call wf_setup
    push bx
    call OSAPI_WM_DISPLAY           ; the band, for wr_canbig
    sub cx, si
    mov [wr_dband], cx
    mov [wr_dwid], ax
    pop bx
    call wf_deco                    ; keeps BX
    call wf_forget
    mov dl, 1
    call wf_update
    pop si
    cmp byte [wr_mode], WRM_ABOUT   ; ...and the About card LAST
    jne .out
    mov bx, si
    mov si, wr_ablines
    call os88ui_about_d             ; keeps every register
    mov si, bx
.out:
    ret

; =============================================================================
; CALLBACKS
; =============================================================================

; -----------------------------------------------------------------------------
; wr_onclick - a press the keys did not take: the About card's dismissal, an
;              open list, a drop-down, the sliders, the Loop box
; in:  CX, DX = the pointer (screen), SI = the window
; out: CF = 1 the press was ours
; -----------------------------------------------------------------------------
wr_onclick:
    cmp byte [wr_mode], WRM_ABOUT
    jne .notabout
    call wr_dismiss
    stc
    ret
.notabout:
    push si
    mov bx, si
    call wd_origin
    cmp byte [wr_mode], WRM_DROP
    jne .closed
    call wr_dropbx                  ; BX = the open one's record
    call os88ui_drpress
    pop bx
    push bx
    call wr_dropend                 ; which draws what it changed
    jmp short .yes0
.closed:
    mov bx, wr_drops                ; a press on a drop-down's box
    xor di, di
.drop:
    push bx
    call os88ui_drpress             ; AH = 1 spent (it greyed refuses)
    pop bx
    or ah, ah
    jz .nd
    cmp byte [bx+OS88UI_DR_OPEN], 0
    je .yes
    mov ax, di                      ; the list is down: every press is its
    inc ax                          ; until it goes
    mov [wr_dropo], al
    mov byte [wr_mode], WRM_DROP
    mov word [wr_btrec + OS88UI_BT_N], 0
    jmp short .yes0
.nd:
    add bx, OS88UI_DR_SIZE
    inc di
    cmp di, WR_NDROP
    jb .drop
    sub cx, [wd_ox]                 ; the rest are content-relative
    sub dx, [wd_oy]
    mov bx, si
    call wr_hit
    jc .not
.yes:
    pop bx
    push bx
    call wr_redraw
.yes0:
    pop si
    stc
    ret
.not:
    pop si
    clc
    ret

; wr_dropbx - BX = the open drop-down's record. keeps everything else
wr_dropbx:
    push ax
    mov al, [wr_dropo]
    dec al
    mov ah, OS88UI_DR_SIZE
    mul ah
    add ax, wr_drops
    mov bx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_dropend - after os88ui answered a press or a release on the open list
; (AH = 1 spent, AL = a pick or 0FFh, CF = 1 repaint): if the list went, the
; keys come back, a pick is applied and the window is redrawn - or repainted
; whole when the library could not write the banked pixels back.
; in:  BX = the window. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wr_dropend:
    pushf
    push ax
    push bx
    call wr_dropbx
    cmp byte [bx+OS88UI_DR_OPEN], 0
    pop bx
    pop ax
    jne .still
    push ax
    mov al, [wr_dropo]
    mov byte [wr_dropo], 0
    mov byte [wr_mode], WRM_FACE
    mov word [wr_btrec + OS88UI_BT_N], WR_NB
    cmp al, 2                       ; Rate, the one live drop-down
    pop ax
    jne .nopick
    cmp al, 0FFh
    je .nopick
    call wr_setrate
.nopick:
    popf
    jnc wr_redraw
    jmp wr_repaint
.still:
    popf
    ret

; wr_hit - CX, DX = a content point: act on it. CF = 1 nothing there.
; clobbers AX, BX, CX, DX, SI, DI
wr_hit:
    mov si, [wd_lp]                 ; INPUT GAIN
    add si, WL_GSL
    call wf_in
    jc .ngain
    mov byte [wr_drag], 1
    mov ax, WR_GAINMAX
    call wf_slval
    call wr_gainset
    clc
    ret
.ngain:
    mov si, [wd_lp]                 ; VOLUME
    add si, WL_VSL
    call wf_in
    jc .nvol
    mov byte [wr_drag], 2
    mov ax, 100
    call wf_slval
    call wr_volset
    clc
    ret
.nvol:
    mov si, [wd_lp]                 ; a greyed drop-down says why (47 rule
    add si, WL_DROPS                ; 6): it holds one choice (SPEC.md 123.7)
    xor di, di
.gdrop:
    call wf_in
    jc .gnext
    push si
    mov si, di
    call wr_dropdis
    pop si
    or di, di
    jz .no                          ; the live one is os88ui's, not ours
    mov si, wr_s_one
    call wr_say
    clc
    ret
.gnext:
    add si, 8
    inc di
    cmp di, WR_NDROP
    jb .gdrop
    cmp byte [wo_doc], 0            ; the waveform: the cursor there
    je .loop
    mov si, [wd_lp]
    add si, WL_WAVE
    call wf_in
    jc .novw
    call wr_wpress                  ; the cursor, a selection, a seek
    clc
    ret
.novw:
    mov si, [wd_lp]                 ; the overview: the view centred there
    add si, WL_OVW
    call wf_in
    jc .loop
    call we_ready
    jc .loop
    mov byte [wr_drag], 3
    call wr_ovat
    clc
    ret
.loop:
    mov si, [wd_lp]                 ; the Loop box and its label
    add si, WL_LOOP
    call wf_in
    jc .no
    mov al, C_LOOP
    call wr_try
    clc
    ret
.no:
    stc
    ret

; wr_try - command AL: done when live, explained when refused. BX = the
; window. clobbers AX, CX, DX, SI, DI
wr_try:
    call wr_cmdok
    jc .why
    jmp wr_cmd
.why:
    jmp wr_explain

; -----------------------------------------------------------------------------
; wr_ondrag / wr_onup - the gesture (SPEC.md 13.8): an open list's, a slider's
;                       (ours), or a key's (the library's, firing on the
;                       RELEASE)
; in:  CX, DX = the pointer (screen), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
wr_ondrag:
    cmp byte [wr_mode], WRM_DROP
    jne .ndrop
    push si
    call wr_dropbx
    call os88ui_drdrag
    pop si
    ret
.ndrop:
    cmp byte [wr_drag], 0
    je .btn
    push si
    mov bx, si
    call wd_origin
    sub cx, [wd_ox]
    mov si, [wd_lp]
    cmp byte [wr_drag], 3           ; the overview: the view follows
    jne .ncur
    call wr_ovat
    jmp short .draw
.ncur:
    cmp byte [wr_drag], 4           ; the waveform: the selection follows
    jb .ngain
    call wr_wdrag
    jmp short .draw
.ngain:
    cmp byte [wr_drag], 1
    jne .vol
    add si, WL_GSL
    mov ax, WR_GAINMAX
    call wf_slval
    cmp al, [wr_gain]
    je .same
    call wr_gainset
    jmp short .draw
.vol:
    add si, WL_VSL
    mov ax, 100
    call wf_slval
    cmp al, [wr_vol]
    je .same
    call wr_volset
.draw:
    pop bx
    push bx
    call wr_redraw
.same:
    pop si
    ret
.btn:
    mov bx, wr_btrec
    jmp os88ui_btndrag

wr_onup:
    cmp byte [wr_mode], WRM_DROP
    jne .ndrop
    push si
    call wr_dropbx
    call os88ui_drup
    pop bx
    push bx
    call wr_dropend
    pop si
    ret
.ndrop:
    cmp byte [wr_drag], 0
    je .btn
    mov byte [wr_drag], 0           ; the slider let go
    ret
.btn:
    mov bx, wr_btrec
    call os88ui_btnup               ; AX = what FIRED, index + 1; 0 = cancelled
    dec ax
    js .out
    call wr_bcmd
    push si
    mov bx, si
    call wr_try                     ; done, or the status bar says why
    pop bx
    push bx
    call wr_redraw
    pop si                          ; the window, given back (SPEC.md 111.6)
.out:
    ret

; -----------------------------------------------------------------------------
; wr_wheel - the wheel turns VOLUME. in: AX = signed notches, SI = the window
; -----------------------------------------------------------------------------
wr_wheel:
    push si
    push ax
    mov bx, si
    call wd_origin
    call OSAPI_MOUSE                ; CX, DX = the pointer (the wheel's own
    pop ax                          ; call carries none)
    sub cx, [wd_ox]
    sub dx, [wd_oy]
    call we_ready                   ; over the waveform or the overview, with
    jc .vol                         ; a document scanned: the view scrolls,
    mov si, [wd_lp]                 ; an eighth of it a notch
    add si, WL_WAVE
    call wf_in
    jnc .scroll
    mov si, [wd_lp]
    add si, WL_OVW
    call wf_in
    jc .vol
.scroll:
    mov cx, [we_ww]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    imul cx
    call we_scroll
    jmp short .draw
.vol:
    neg ax                          ; elsewhere VOLUME: a notch toward the
    call wr_volstep                 ; user is quieter (clobbers SI)
.draw:
    pop bx
    push bx
    call wr_redraw
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_key - W_ONKEY: SPEC.md 123.12's table (wr_keytab)
; in:  AL = ASCII, AH = scan, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
wr_key:
    push si
    cmp byte [wr_mode], WRM_ABOUT
    jne .notabout
    call wr_dismiss                 ; any key takes the About card down
    jmp .out
.notabout:
    cmp byte [wr_mode], WRM_DROP
    jne .keys
    cmp ah, 0x01                    ; Escape takes an open list down
    jne .out
    call wr_dropbx
    call os88ui_drclose
    mov ax, 00FFh
    pop bx
    push bx
    call wr_dropend
    jmp .out
.keys:
    mov bx, wr_keytab
.find:
    mov dl, [bx]
    cmp dl, 0xFF
    je .out                         ; not ours: the F keys, Alt+Enter, ...
    cmp dl, 0
    jne .ascii
    cmp ah, [bx+1]                  ; a scan code
    je .hit
    jmp short .next
.ascii:
    mov dh, al
    cmp dl, 1
    jne .exact
    and dh, 0xDF                    ; a letter, any case
.exact:
    cmp dh, [bx+1]
    je .hit
.next:
    add bx, 3
    jmp short .find
.hit:
    mov al, [bx+2]
    pop bx
    push bx
    cmp al, K_VOLDN
    jb .cmd
    cbw                             ; AX = -16..-13: which keystroke
    add ax, 0x10 - 0                ; ...0..3
    mov cx, ax
    mov ax, -1
    test cl, 1
    jz .dir
    mov ax, 1
.dir:
    cmp cl, 2
    jae .gain
    call wr_volstep
    jmp short .draw
.gain:
    call wr_gainstep
    jmp short .draw
.cmd:
    call wr_try
.draw:
    pop bx
    push bx
    call wr_redraw
.out:
    pop si
    ret

; =============================================================================
; THE MENUS (SPEC.md 12.2, 123.12) - About is the application menu's own
; =============================================================================

; -----------------------------------------------------------------------------
; wr_menu - a menu item: AH = the menu, AL = the item, SI = the window
; -----------------------------------------------------------------------------
wr_menu:
    push si
    push ax
    cmp byte [wr_mode], WRM_DROP    ; a menu takes an open list down first
    jne .go
    call wr_dropbx
    call os88ui_drclose             ; CF = 1: repaint what it lay over
    mov byte [wr_dropo], 0
    mov byte [wr_mode], WRM_FACE
    mov word [wr_btrec + OS88UI_BT_N], WR_NB
    jnc .go
    mov bx, si
    call wr_repaint
.go:
    pop ax
    mov bl, ah                      ; the command: the menu's base + the item
    xor bh, bh
    add al, [wr_mbase + bx]
    mov bx, si
    call wr_try                     ; (which may clobber SI: a resize does)
    pop bx
    push bx
    call wr_redraw
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_menulive - every item's greying and every toggle's name, from the same
;               predicate the keys use (SPEC.md 47 rule 2). The kernel reads
;               the strings when a menu opens, so writing them is enough.
; clobbers AX, BX, SI
; -----------------------------------------------------------------------------
wr_menulive:
    mov si, wr_m_comp               ; the toggles name their state (no check
    mov bx, [wd_lp]                 ; marks, SPEC.md 12.2)
    cmp word [bx+WL_CMP], 0
    je .lay
    mov si, wr_m_full
.lay:
    mov [wr_items + 2 * C_LAYOUT], si
    mov si, wr_m_col
    cmp word [wd_th], wrart_tcol
    je .th
    mov si, wr_m_bw
.th:
    mov [wr_items + 2 * C_THEME], si
    mov si, wr_m_loop0
    cmp byte [wr_loop], 0
    je .lp
    mov si, wr_m_loop1
.lp:
    mov [wr_items + 2 * C_LOOP], si
    mov si, wr_m_rate8
    cmp byte [wr_rate], 0
    je .rt
    mov si, wr_m_rate11
.rt:
    mov [wr_items + 2 * C_RATE], si
    xor al, al                      ; ...and every item's greying
    mov bx, wr_items
.it:
    call wr_mlive
    add bx, 2
    inc al
    cmp al, C_NMENU
    jb .it
    ret

; wr_mlive - BX = an item's slot: it points at the live string, or at the
; MENU_DIS byte laid out in front of it (the greyed form). Live unless
; command AL is refused. keeps everything
wr_mlive:
    push si
    mov si, [bx]
    cmp byte [si], MENU_DIS
    jne .live
    inc si
.live:
    call wr_cmdok
    jnc .set
    dec si
.set:
    mov [bx], si
    pop si
    ret

; =============================================================================
; 'About WaveRack' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; While the card is up the key record has NO keys, so a press anywhere in the
; content reaches wr_onclick and takes the card down (CDRack's idiom).
wr_about:
    mov byte [wr_mode], WRM_ABOUT
    mov word [wr_btrec + OS88UI_BT_N], 0
    mov bx, si
    mov si, wr_ablines
    jmp os88ui_about

; wr_dismiss - the keys back and the content repainted. SI = the window
wr_dismiss:
    mov byte [wr_mode], WRM_FACE
    mov word [wr_btrec + OS88UI_BT_N], WR_NB
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call wr_paint                   ; SI = the window still; keeps it
.out:
    ret

; =============================================================================
; TEXT HELPERS
; =============================================================================

; wr_put - copy the NUL string at SI to DI, NUL not copied; DI past it.
; clobbers SI (keeps AX)
wr_put:
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

; wr_num - AL (0..255) in decimal at DI, no leading zeros; DI past it.
; clobbers AX
wr_num:
    xor ah, ah
    cmp al, 100
    jb .two
    push bx
    mov bl, 100
    div bl                          ; AL = hundreds, AH = the rest
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    pop bx
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    add di, 2
    ret
.two:
    cmp al, 10
    jb .one
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    add di, 2
    ret
.one:
    add al, '0'
    mov [di], al
    inc di
    ret

%include "wrdraw.inc"
%include "wrface.inc"
%include "wrwav.inc"
%include "wrdoc.inc"
%include "wrenv.inc"
%include "wrxport.inc"
%include "wredit.inc"
%include "wrsave.inc"

; =============================================================================
; TEXT HELPERS (numbers)
; =============================================================================

; wr_hex4 - AX as four hex digits at DI; DI past them. clobbers AX
wr_hex4:
    push bx
    push cx
    mov bx, ax
    mov cx, 4
.h:
    push cx
    mov cl, 4
    rol bx, cl
    pop cx
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 7
.d:
    mov [di], al
    inc di
    loop .h
    pop cx
    pop bx
    ret

; wr_upper - AL in upper case. keeps the rest
wr_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; wo_fmttext - the document's format line at wr_tbuf2, the LCD's spelling:
; '11,025 Hz  8-bit  Mono', '22,050 Hz 16-bit Stereo'. clobbers AX, DX, SI,
; DI
wo_fmttext:
    mov di, wr_tbuf2
    mov ax, [wo_rate]
    call wr_ratetxt
    mov si, wr_s_hz
    call wr_put
    mov si, wr_s_b8
    cmp byte [wo_bits], 16
    jne .b
    mov si, wr_s_b16
.b:
    call wr_put
    mov si, wr_s_mono
    cmp byte [wo_ch], 2
    jne .c
    mov si, wr_s_stereo
.c:
    call wr_put
    mov byte [di], 0
    ret

; wr_ratetxt - a rate AX (1,000 or more) with its thousands comma at DI:
; '22,050'; DI past it. clobbers AX, DX
wr_ratetxt:
    xor dx, dx
    push cx
    mov cx, 1000
    div cx
    pop cx
    push dx
    call dec_u16
    mov byte [di], ','
    inc di
    pop ax
    push cx
    mov cl, 100
    div cl                          ; AL = hundreds, AH = the rest
    pop cx
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    jmp wo_two

; =============================================================================
; DATA
; =============================================================================
wr_tpl:     dw 6, 24, WRL_CMP_W + 2, WRL_CMP_H + TITLE_H + 1
            dw wr_ttl, wr_paint, wr_key, 0
wr_ttl      equ 16                  ; the header's name field: 'WaveRack'

wr_pref:    times 6 dw 0            ; OSAPI_WM_PREFER's table (wr_prefset)

    WDDSET df, DF
    WDDSET dm, DM
    WDDSET dc, DC

wr_dropn:   db WR_DROPN0, WR_DROPN1, WR_DROPN2, WR_DROPN3

; the keys' commands, B_* order
wr_btncmd:  db C_RECORD, C_STOP, C_PAUSE, C_PLAYK, C_OPEN, C_SAVE, C_ZOUT
            db C_ZIN, C_TRIM, C_DELETE, C_SAVEAS
%if ($ - wr_btncmd) != WR_NB
%error "wr_btncmd needs a command for every key"
%endif

; the keystrokes (SPEC.md 123.12): kind (0 a scan code, 1 a letter of either
; case, 2 an exact character), the code, the command; FF ends it
wr_keytab:
    db 0, 0x39, C_PLAY              ; Space: play / pause
    db 1, 'S', C_STOP
    db 0, 0x01, C_STOP              ; Escape
    db 1, 'R', C_RECORD
    db 1, 'L', C_LOOP
    db 0, 0x4B, C_CURSOR            ; Left, Right, Home, End
    db 0, 0x4D, C_CURSOR
    db 0, 0x47, C_CURSOR
    db 0, 0x4F, C_CURSOR
    db 0, 0x49, C_CURSOR            ; PgUp, PgDn: a page of the view
    db 0, 0x51, C_CURSOR
    db 2, '+', C_ZIN
    db 2, '=', C_ZIN
    db 2, '-', C_ZOUT
    db 1, 'F', C_FIT
    db 0, 0x53, C_DELETE            ; Delete
    db 1, 'T', C_TRIM
    db 1, 'U', C_UNDO
    db 1, 'Y', C_REDO
    db 1, 'X', C_CUT
    db 1, 'C', C_COPY
    db 1, 'V', C_PASTE
    db 1, 'A', C_SELALL
    db 2, '[', K_VOLDN
    db 2, ']', K_VOLUP
    db 2, '<', K_GAINDN
    db 2, ',', K_GAINDN
    db 2, '>', K_GAINUP
    db 2, '.', K_GAINUP
    db 0xFF

; --- the menus (SPEC.md 123.12) ---------------------------------------------------
    OS88_MENUSET wr_menus, wr_name, wr_menu
        OS88_MENU wr_mt_file, wr_items + 2 * C_NEW, 6
        OS88_MENU wr_mt_edit, wr_items + 2 * C_UNDO, 9
        OS88_MENU wr_mt_view, wr_items + 2 * C_ZIN, 5
        OS88_MENU wr_mt_sound, wr_items + 2 * C_PLAY, 5
    OS88_MENUSET_END wr_menus
wr_mbase:   db C_NEW, C_UNDO, C_ZIN, C_PLAY
wr_name:    db 'WaveRack', 0
wr_mt_file: db 'File', 0
wr_mt_edit: db 'Edit', 0
wr_mt_view: db 'View', 0
wr_mt_sound: db 'Sound', 0
wr_items:                           ; C_* order: a slot an item
    dw wr_m_new, wr_m_open, wr_m_save, wr_m_saveas, wr_m_savesel, wr_m_recover
    dw wr_m_undo, wr_m_redo, wr_m_cut, wr_m_copy, wr_m_paste, wr_m_delete
    dw wr_m_trim, wr_m_selall, wr_m_selnone
    dw wr_m_zin, wr_m_zout, wr_m_fit, wr_m_comp, wr_m_col
    dw wr_m_play, wr_m_stop, wr_m_rec, wr_m_loop0, wr_m_rate11
%if ($ - wr_items) != 2 * C_NMENU
%error "wr_items needs a slot for every menu command"
%endif
            db MENU_DIS
wr_m_new:   db 'New', 0
            db MENU_DIS
wr_m_open:  db 'Open...', 0
            db MENU_DIS
wr_m_save:  db 'Save', 0
            db MENU_DIS
wr_m_saveas: db 'Save As...', 0
            db MENU_DIS
wr_m_savesel: db 'Save Selection As...', 0
            db MENU_DIS
wr_m_recover: db 'Recover Take', 0
            db MENU_DIS
wr_m_undo:  db 'Undo  U', 0
            db MENU_DIS
wr_m_redo:  db 'Redo  Y', 0
            db MENU_DIS
wr_m_cut:   db 'Cut  X', 0
            db MENU_DIS
wr_m_copy:  db 'Copy  C', 0
            db MENU_DIS
wr_m_paste: db 'Paste  V', 0
            db MENU_DIS
wr_m_delete: db 'Delete  Del', 0
            db MENU_DIS
wr_m_trim:  db 'Trim  T', 0
            db MENU_DIS
wr_m_selall: db 'Select All  A', 0
            db MENU_DIS
wr_m_selnone: db 'Select None', 0
            db MENU_DIS
wr_m_zin:   db 'Zoom In  +', 0
            db MENU_DIS
wr_m_zout:  db 'Zoom Out  -', 0
            db MENU_DIS
wr_m_fit:   db 'Fit  F', 0
            db MENU_DIS
wr_m_comp:  db 'Compact Layout', 0
            db MENU_DIS
wr_m_full:  db 'Full Layout', 0
            db MENU_DIS
wr_m_col:   db 'Theme: Color', 0
            db MENU_DIS
wr_m_bw:    db 'Theme: Black and White', 0
            db MENU_DIS
wr_m_play:  db 'Play/Pause  Space', 0
            db MENU_DIS
wr_m_stop:  db 'Stop  S', 0
            db MENU_DIS
wr_m_rec:   db 'Record  R', 0
            db MENU_DIS
wr_m_loop0: db 'Loop: Off  L', 0
            db MENU_DIS
wr_m_loop1: db 'Loop: On  L', 0
            db MENU_DIS
wr_m_rate8: db 'Rate: 8,000 Hz', 0
            db MENU_DIS
wr_m_rate11: db 'Rate: 11,025 Hz', 0

; --- strings ---------------------------------------------------------------------
wr_s_nofile: db 'NO FILE', 0
wr_s_zero:   db '00:00.00', 0
wr_s_total:  db '/ 00:00.00', 0
wr_s_fmt8:   db '8,000 Hz  8-bit  Mono', 0
wr_s_fmt11:  db '11,025 Hz  8-bit  Mono', 0
; the status bar's (22 characters at most: tools/os88wrlay.py STATUS_MAX)
wr_s_ready:  db 'Ready', 0
wr_s_notyet: db 'Not in this build yet', 0
wr_s_nocard: db 'Needs a Sound Blaster', 0
wr_s_nocap:  db 'No capture device', 0
wr_s_nostop: db 'Nothing to stop', 0
wr_s_one:    db 'Only one choice here', 0
wr_s_bwonly: db 'One color on screen', 0
wr_s_noroom: db 'Screen too small', 0
wr_s_loop0:  db 'Loop off', 0
wr_s_loop1:  db 'Loop on', 0
wr_s_rate8:  db 'Next take 8,000 Hz', 0
wr_s_rate11: db 'Next take 11,025 Hz', 0
wr_s_gain:   db 'Input gain ', 0
wr_s_db:     db ' dB', 0
wr_s_volume: db 'Volume ', 0
wr_s_new:    db 'New document', 0
wr_s_cancel: db 'Open cancelled', 0
wr_s_nodoc:  db 'No document', 0
wr_s_busy:   db 'Still scanning', 0
wr_s_zinmax: db 'Zoomed in fully', 0
wr_s_whole:  db 'Whole sound shown', 0
wr_s_dlg:    db 'A dialog is open', 0
wr_s_stopfirst: db 'Stop first', 0
wr_s_noplay: db 'Not playing', 0
wr_s_nosel:  db 'No selection', 0
wr_s_paused: db 'Paused', 0
wr_s_stopped: db 'Stopped', 0
wr_s_answer: db 'Answer the question', 0
wr_s_end:    db 'End of the sound', 0
wr_s_unders: db ' underruns', 0
wr_s_under1: db ' underrun', 0
wr_s_lowrate: db 'Card needs 4,000+ Hz', 0
wr_s_cbusy:  db 'Sound card is busy', 0
wr_s_rateno: db 'Card refused the rate', 0
wr_s_noopen: db 'Cannot open the card', 0
wr_s_notask: db 'No task for the sound', 0
wr_s_saving: db 'Still saving', 0
wr_s_clean:  db 'Nothing to save', 0
wr_s_noundo: db 'Nothing to undo', 0
wr_s_noredo: db 'Nothing to redo', 0
wr_s_noclip: db 'Clipboard is empty', 0
wr_s_npc:    db 'Too many edits; save', 0
wr_s_cplx:   db 'Selection too complex', 0
wr_s_trim:   db 'Trimmed', 0
wr_s_del:    db 'Deleted', 0
wr_s_cut:    db 'Cut', 0
wr_s_copied: db 'Copied', 0
wr_s_pasted: db 'Pasted', 0
wr_s_undone: db 'Undone', 0
wr_s_redone: db 'Redone', 0
wr_s_clip:   db 'Clip: ', 0
wr_s_bmono:  db 'b mono', 0
wr_s_bster:  db 'b stereo', 0
wr_s_untitled: db 'UNTITLED.WAV', 0
wr_s_ttl:    db 'WaveRack - ', 0
wr_s_qsave:  db 'Save changes to ', 0
wr_s_repl:   db 'Replace ', 0
wr_s_saved:  db 'Saved ', 0
wr_s_scancel: db 'Save cancelled', 0
wr_s_inuse:  db 'File is in use', 0
wr_s_dfull:  db 'Not enough disk space', 0
wr_s_dfull2: db 'Disk is full', 0
wr_s_wprot:  db 'Disk write-protected', 0
wr_s_dirfull: db 'Folder is full', 0
wr_s_wfail:  db 'Cannot write the file', 0
wr_s_norepl: db 'Cannot replace file', 0
wr_s_kept    equ ws_kmsg
wr_s_hz:     db ' Hz', 0
wr_s_b8:     db '  8-bit', 0
wr_s_b16:    db ' 16-bit', 0
wr_s_mono:   db '  Mono', 0
wr_s_stereo: db ' Stereo', 0

wr_ablines:
    dw wr_ab1, wr_ab2, wr_ab3, 0
wr_ab1:     db 'WaveRack for os8088', 0
wr_ab2:     db 'records, plays and', 0
wr_ab3:     db 'edits WAV sound', 0

%define WRART_DATA
%include "wrart.inc"

%define DEC_U16                     ; the numbers: os88dec.inc (SPEC.md 20.5.2)
%define DEC_U32
%include "os88dec.inc"
%include "os88rseq.inc"             ; os88_rseq: every read a stream (wrdoc.inc)

%define OS88UI_ABOUT
%define OS88UI_ALERT                ; the save-changes and replace questions
%define OS88UI_NOGLYPH
%define OS88UI_BOWN
%define OS88UI_DROP
%include "os88ui.inc"

    OS88UI_BTNREC wr_btrec, wd_rects, wr_blbl, wr_bflags, WR_NB
wr_blbl:    times WR_NB dw wf_bpaint    ; every key is WaveRack's to paint

; --- the four drop-downs (SPEC.md 13.14): Input, Rate, Bits, Channels -----------
; Their rects are made screen-absolute by wd_origin and their items pointed
; at the layout's spelling by wf_setup.
wr_drops:   times WR_NDROP * OS88UI_DR_SIZE db 0

; --- what wr_mode says is in charge of a press ------------------------------------
WRM_FACE    equ 0                   ; the face: keys, drop-downs, sliders
WRM_ABOUT   equ 1                   ; the About card is up
WRM_DROP    equ 2                   ; a drop-down's list is open

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------------
WRB_SIZE0   equ 240 + WR_NB * 10 + WF_NKEYS * 2 + WD_BANDSZ

%define WRB(off) (os88_image_end + (off))

; ...and the document's, laid out by size after the face's (WRV name, bytes)
%assign WRX 0
%macro WRV 2
%1 equ WRB(WRB_SIZE0 + WRX)
%assign WRX WRX + %2
%endmacro
    WRV wd_tseg, 2                  ; wrdraw.inc: wd_bor's target band...
    WRV wd_tbase, 2
    WRV wd_tst, 2                   ; ...and its stride
    WRV wf_bdis, 1                  ; wrface.inc: a greyed key on one bit
    WRV wr_argpend, 1               ; OSAPI_ARG_FILE's document, pending
    WRV wr_argvol, 1
    WRV wr_argdir, 2
    ; --- the reader (wrwav.inc) ------------------------------------------------------
    WRV ww_err, 1                   ; E_*: the last open's refusal
    WRV ww_note, 1                  ; N_*: ...or its note
    WRV ww_narg, 1
    WRV ww_wv, 1                    ; the peek window is valid
    WRV ww_n, 1                     ; chunk headers read
    WRV ww_gotf, 1
    WRV ww_gotd, 1
    WRV ww_pad0, 1
    WRV ww_arg, 2                   ; the number the refusal names
    WRV ww_clu, 2                   ; the volume's cluster in bytes
    WRV ww_wl, 2                    ; the window's bytes
    WRV ww_wofs, 2                  ; ...where it starts in the I/O claim
    WRV ww_head, 2                  ; ww_peek: the head, the bytes' end,
    WRV ww_need, 2                  ; T (those across a cluster's end)
    WRV ww_tl, 2
    WRV ww_tbuf, 48
    WRV ww_tag, 2                   ; fmt: 1 = PCM, else the tag
    WRV ww_ch, 2
    WRV ww_ba, 2
    WRV ww_bits, 2
    WRV ww_flen, 4                  ; the file's raw size
    WRV ww_wb, 4                    ; the window's offset
    WRV ww_pos, 4                   ; the chunk being read
    WRV ww_csz, 4
    WRV ww_rate, 4
    WRV ww_doff, 4                  ; data: its offset and (clamped,
    WRV ww_dlen, 4                  ; floored) length
    WRV wo_nbuf, 14                 ; the name being opened
    WRV wo_fbuf, 24                 ; OSAPI_FIND_SZ
    ; --- the document (wrdoc.inc): wo_new zeroes from wo_zero0 to wo_zero1 ----------
    WRV wo_zero0, 0
    WRV wo_doc, 1                   ; 1 = a document is open
    WRV wo_dirty, 1
    WRV wo_bits, 1                  ; its format
    WRV wo_ch, 1
    WRV wo_ba, 1
    WRV wo_sh, 1
    WRV wo_rate, 2
    WRV wo_npc, 2                   ; pieces in the table
    WRV wo_frames, 4                ; its length in frames
    WRV wo_cur, 4                   ; the cursor, a frame
    WRV wo_name, 14                 ; the name the LCD shows
    WRV wo_cv, 1                    ; the I/O claim's run is valid
    WRV wv_z, 1                     ; the view: fpp = 1 << z
    WRV wv_zfit, 1
    WRV wv_ok, 1                    ; the view cache is built
    WRV we_busy, 1                  ; 0 idle, 1 scanning, 2 refining
    WRV we_bs, 1                    ; the envelope's block shift
    WRV we_yh, 2                    ; the row table's lane height
    WRV wv_first, 4                 ; the view's first frame
    WRV wo_s, 4                     ; the selection [s, e), frames (SPEC.md
    WRV wo_e, 4                     ; 123.9), and the end a drag holds still
    WRV wo_anc, 4
    WRV wo_titled, 1                ; 0: UNTITLED.WAV (New, then Paste)
    WRV wo_fvol, 1                  ; the document's own file's folder (a
    WRV wo_fdir, 2                  ; Save writes there)
    WRV wo_zero1, 0
    WRV wo_gen, 2                   ; generations: element keys
    WRV we_wgen, 2
    WRV we_ogen, 2
    WRV we_cgen, 2
    WRV wo_sgen, 2                  ; the selection's
    WRV wo_ioseg, 2                 ; claims: the I/O buffer...
    WRV we_vseg, 2                  ; ...the view cache
    WRV wv_ww, 2                    ; the width the cache is for
    WRV wo_csrc, 2                  ; the run the I/O claim holds
    WRV wo_rcap, 2                  ; wo_srcread: this call's read, bytes
    WRV wo_cofs, 2                  ; ...and where its window starts in it
    WRV wo_iokb, 2                  ; the I/O claim's KB (wo_iofit)
    WRV wo_ceof, 1                  ; the window ends at the file's end
    WRV wo_ntry, 1                  ; wo_srcread's reads left this call
    WRV wo_x, 4                     ; ...and the byte it wants
    WRV wo_clen, 2
    WRV wo_rmax, 2
    WRV wo_cbase, 4
    WRV wo_t0, 2                    ; wo_rdrun: the read's first tick
    WRV wo_src, WO_NSRC * WS_SIZE   ; the sources
    WRV wo_pc, WO_NPC * WP_SIZE     ; the piece table
    WRV we_ssrc, 2                  ; the source the scan is building
    ; --- the edits, the history, the clipboard (wredit.inc) --------------------
    WRV wo_ncp, 2                   ; the clipboard: its pieces, frames and
    WRV wo_cfr, 4                   ; format - kept across New and Open
    WRV wo_crate, 2
    WRV wo_cbits, 1                 ; (bits, then channels: wx_clipfmt reads
    WRV wo_cch, 1                   ; the two as one word)
    WRV wo_csh, 1
    WRV wo_un, 1                    ; the history: states held, the current
    WRV wo_upos, 1                  ; one, the ring's oldest
    WRV wo_ubase, 1
    WRV wo_useg, 2                  ; ...its claim (WX_UKB)
    WRV wx_tn, 2                    ; the list being built: pieces, too many
    WRV wx_tov, 1
    WRV wx_adopted, 1               ; this Paste adopted an empty document
    WRV wx_es, 2                    ; wx_emit's piece
    WRV wx_ef, 4
    WRV wx_ec, 4
    WRV wx_ra, 4                    ; wx_range's frames and position
    WRV wx_rb, 4
    WRV wx_rpos, 4
    WRV wx_na, 4                    ; the selection the edit leaves
    WRV wx_nb, 4
    WRV wo_clip, WO_NCLIP * WP_SIZE ; the clipboard's pieces
    WRV wx_tmp, WO_NPC * WP_SIZE    ; the list being built
    ; --- the save (wrsave.inc) --------------------------------------------------
    WRV ws_busy, 1                  ; 1 = a save is writing (state BUSY)
    WRV ws_mode, 1                  ; WSM_*
    WRV ws_made, 1                  ; WRSAVE.$$$ exists
    WRV ws_padded, 1                ; the pad byte is decided
    WRV ws_pct, 1                   ; the percentage said
    WRV ws_qact, 1                  ; WRQ_*: carried out when it succeeds
    WRV ws_rb, 1                    ; the document was rebased onto it
    WRV ws_dvol, 1                  ; the destination: volume, folder,
    WRV ws_ddir, 2                  ; cluster
    WRV ws_dclu, 2
    WRV ws_oseg, 2                  ; the output claim, its bytes
    WRV ws_fill, 2
    WRV ws_osz, 2                   ; ...and its size: 16 KB or a cluster
    WRV ws_tok, 2                   ; OSAPI_FILE_WRITE_SEQ's token
    WRV ws_tmask, 2                 ; the source records that ARE the target
    WRV ws_bytes, 4                 ; the data's bytes
    WRV ws_f, 4                     ; frames: next, first, end
    WRV ws_f0, 4
    WRV ws_f1, 4
    WRV ws_tname, 14                ; the target's name
    WRV ws_tmp, 14                  ; the temp's name (ws_tmpmake)
    WRV ws_kmsg, 22                 ; 'Kept as WRSAV07.$$$'
    WRV ws_tn, 1                    ; ws_tmpmake's number, and tries left
    WRV ws_tcnt, 1
    WRV ws_rq, 1                    ; the Replace question is up
    WRV wr_qact, 1                  ; the question's action (WRQ_*)
    WRV wr_closing, 1               ; OSAPI_WM_CLOSE asked: draw nothing
    WRV wr_stsel, 1                 ; the status is 'No selection'
    WRV wr_qbuf, 40                 ; the question's line
    WRV wr_ttlbuf, 28               ; the title (OSAPI_WM_TITLE keeps it)
    ; --- the waveform (wrenv.inc) ----------------------------------------------------
    WRV we_bx, 2                    ; the boxes: the waveform's x, y,
    WRV we_by, 2                    ; width, ruler, rows, lanes
    WRV we_ww, 2
    WRV we_rulh, 2
    WRV we_wrows, 2
    WRV we_lh, 2
    WRV we_oy, 2                    ; ...the overview's
    WRV we_oh, 2
    WRV we_oh1, 2
    WRV we_lt0, 2                   ; the lanes: tops, the row between,
    WRV we_lt1, 2                   ; a lane's height
    WRV we_lsep, 2
    WRV we_lnh, 2
    WRV we_boff, 2                  ; the scan: the envelope's next entry,
    WRV we_pct, 1                   ; the percentage said
    WRV we_inexact, 1
    WRV we_rcs, 1                   ; refinement: a column under way,
    WRV we_rsaid, 1                 ; 'Refining' said
    WRV we_rcol, 2
    WRV we_pc1, 2
    WRV we_pgi, 2
    WRV we_ng, 2                    ; the ruler's majors
    WRV we_lend, 2
    WRV we_dpx, 2
    WRV we_mst, 2
    WRV we_nst, 2
    WRV we_ox0, 2
    WRV we_ox1, 2
    WRV we_dec, 2
    WRV we_gx, WE_NGX * 2
    WRV we_sf, 4
    WRV we_bleft, 4
    WRV we_acc, 4
    WRV we_racc, 4
    WRV we_cacc, 4                  ; the refinement's column, so far
    WRV we_f0, 4
    WRV we_f1, 4
    WRV we_n, 4
    WRV we_fppd, 4
    WRV we_rf, 4
    WRV we_rend, 4
    WRV we_tgt, 4
    WRV we_base, 4
    WRV we_s, 4
    WRV we_t, 4
    ; --- the selection's draw (wrenv.inc) ---------------------------------------
    WRV we_smode, 1                 ; we_stime: 0 'SS.cc', 1 'M:SS.c'
    WRV we_kon, 1                   ; the class record: a selection in view,
    WRV we_k0, 2                    ; its first and last columns, its edges
    WRV we_k1, 2                    ; in view (lanes)
    WRV we_kf, 2
    WRV we_dl, 7                    ; ...as the glass shows it: the lanes,
    WRV we_do, 7                    ; the overview
    WRV we_clsp, 2                  ; we_runs: the class proc, the rows
    WRV we_rs, 2
    WRV we_rb, 2
    WRV we_rn, 2
    WRV we_dput, 2                  ; we_seldiff's putter
    WRV wf_mn, 4                    ; wrface.inc: the meter's cells, now...
    WRV wf_mc, 4                    ; ...and changed
    WRV wr_hold, 1                  ; the meter's hold: its cell + 1, 0 none
    ; --- the transport (wrxport.inc) --------------------------------------------
    WRV wt_state, 1                 ; WT_*
    WRV wt_sopen, 1                 ; a stream is open (the worker feeds it)
    WRV wt_hand, 1                  ; ...its handle
    WRV wt_mixing, 1                ; the worker is in a feed pass
    WRV wt_ghave, 1                 ; the ring is held: ours, or the grant
    WRV wt_ext, 1                   ; ...ours: an external ring (34.5.3)
    WRV wt_noext, 1                 ; the card refused one: the grant
    WRV wt_rcode, 1                 ; ...its RL code (SND_OPENF_RLSH)
    WRV wt_uiown, 1                 ; the UI is topping the ring up
    WRV wt_inui, 1                  ; ...and the pass running is the UI's
    WRV wt_ntop, 1                  ; ...and its halves at most
    WRV wt_lsh, 1                   ; the look-ahead's frame shift
    WRV wt_held, 1                  ; the play reads from the hold...
    WRV wt_hload, 1                 ; ...which is loading
    WRV wt_hpct, 1                  ; ...its percentage said
    WRV wt_slow, 1                  ; no stream, no hold: 1 the disk, 2 CPU
    WRV wt_wk, 1                    ; the worker is hired
    WRV wt_lp, 1                    ; Loop, as the play began
    WRV wt_d, 1                     ; the divisor (SPEC.md 123.6)
    WRV wt_ntry, 1                  ; counters: the pre-roll's tries, a
    WRV wt_nfill, 1                 ; refill's reads, a feed's halves
    WRV wt_nfeed, 1
    WRV wt_laseg, 2                 ; the look-ahead claim, its size, mask
    WRV wt_lasz, 2
    WRV wt_lamask, 2
    WRV wt_rseg, 2                  ; our ring's claim (its words after it)
    WRV wt_rdsz, 2                  ; a play's bounded read, bytes
    WRV wt_csh, 2                   ; wt_plan: the streamed play's CPU share
    WRV wt_hseg, 2                  ; the hold's claim
    WRV wt_ht0, 2                   ; a load pass's first tick
    WRV wt_hlo, 4                   ; the hold: frames [hlo, hhi), loaded
    WRV wt_hhi, 4                   ; up to hpos
    WRV wt_hpos, 4
    WRV wt_nlo, 4                   ; (wt_plan's region low)
    WRV wt_grant, 2                 ; the ring: the grant's offset, size, mask
    WRV wt_ring, 2
    WRV wt_rmask, 2
    WRV wt_preroll, 2               ; halves before the open
    WRV wt_chalf, 2                 ; the card's block: 2 or 4 KB (H1)
    WRV wt_lshort, 1                ; Loop asked, the region too short
    WRV wt_out, 2                   ; the card's rate
    WRV wt_cvp, 2                   ; the format's inner loop
    WRV wt_mtick, 2                 ; the meter's last tick, the hold's, PEAK's
    WRV wt_holdt, 2
    WRV wt_peakt, 2
    WRV wt_sp, 4                    ; frames: the stream's start, the fill's
    WRV wt_fpos, 4                  ; next, the first lap's end, the lap's end
    WRV wt_e1, 4                    ; now, where Stop puts the playhead, the
    WRV wt_lend, 4                  ; region, the playhead, the paused place
    WRV wt_p0, 4
    WRV wt_r0, 4
    WRV wt_r1, 4
    WRV wt_ph, 4
    WRV wt_ppos, 4
    WRV wt_zero0, 0                 ; wt_begin zeroes from here...
    WRV wt_bk, 1                    ; frames in the box group begun
    WRV wt_srcdone, 1               ; the fill has the whole play
    WRV wt_last, 1                  ; the last half is staged
    WRV wt_fin, 1                   ; ...and the card's block filled after it
    WRV wt_cdead, 1                 ; the card stopped before the end (L4)
    WRV wt_ended, 1                 ; the card has played it (the worker's)
    WRV wt_req, 1                   ; a refill asked for
    WRV wt_err, 1                   ; a read failed
    WRV wt_ust, 1                   ; the stream's state at the last poll
    WRV wt_npre, 1                  ; halves pre-rolled
    WRV wt_lwr, 2                   ; the look-ahead's counters (free-running)
    WRV wt_lrd, 2
    WRV wt_total, 2                 ; the stream's: staged, played
    WRV wt_cons, 2
    WRV wt_hfill, 2                 ; the half's bytes so far
    WRV wt_bsum, 2                  ; the box group's sum
    WRV wt_pkcl, 2                  ; the half's peak and clip
    WRV wt_nunder, 2                ; underruns this play
    WRV wt_played, 4                ; output samples played (verb 9)
    WRV wt_mlast, 1                 ; the meter's last half (mod 32)
    WRV wt_pks, 32                  ; each staged half's peak and clip, by
    WRV wt_clips, 32                ; its slot (total / 2 KB, mod 32)
    WRV wt_zero1, 0                 ; ...to here
    WRV wt_vtab, 256                ; VOLUME's table
    WRV wt_hbuf, WT_HALF            ; the half being converted
%ifdef WR_CALLS
    WRV wr_ncalls, 2                ; -DWR_CALLS: drawing primitives called
%endif
WRB_SIZE    equ WRB_SIZE0 + WRX
    OS88_BSS WRB_SIZE
    OS88_IMAGE_END

wr_caps     equ WRB(0)              ; word: OSAPI_SND_CAPS at entry
wr_theme    equ WRB(2)              ; 0 colour, 1 black and white (the choice)
wr_bpp1     equ WRB(3)              ; 1 = the window's display is one bit
wr_want     equ WRB(4)              ; 1 = the user asked for COMPACT
wr_mode     equ WRB(5)              ; WRM_*
wr_dropo    equ WRB(6)              ; the open drop-down + 1, 0 = none
wr_drag     equ WRB(7)              ; 1 INPUT GAIN held, 2 VOLUME held
wr_gain     equ WRB(8)              ; 0..12, unity 6 (SPEC.md 123.7)
wr_vol      equ WRB(9)              ; 0..100 (SPEC.md 123.6)
wr_loop     equ WRB(10)             ; 1 = Loop on
wr_rate     equ WRB(11)             ; the next take: 0 8,000, 1 11,025 Hz
wr_mwho     equ WRB(12)             ; the meter's: 0 none, 1 REC, 2 PLAY
wr_lit      equ WRB(13)             ; its lit cells, 0..24
wr_peak     equ WRB(14)             ; 1 = PEAK lit
wd_cutf     equ WRB(15)             ; wrdraw.inc: the clip cut what was drawn
wd_bdim     equ WRB(16)             ; wrdraw.inc: grey the next band's ink
wd_cgap     equ WRB(17)             ; wrdraw.inc: the small caps' tracking
wd_ctr      equ WRB(18)             ; wrdraw.inc: centre wd_fcaps' text
wr_stkey    equ WRB(20)             ; word: the status message's generation
wr_win      equ WRB(22)             ; word: our window
wr_pband    equ WRB(24)             ; word: the primary's desktop band
wr_dband    equ WRB(26)             ; word: the window's display's band...
wr_dwid     equ WRB(28)             ; word: ...and width
wd_ox       equ WRB(30)             ; word: the content origin, this pass
wd_oy       equ WRB(32)
wr_cw       equ WRB(34)             ; word: the content box
wr_ch       equ WRB(36)
wd_lp       equ WRB(38)             ; word: the layout record
wd_th       equ WRB(40)             ; word: the theme table
wf_slot     equ WRB(42)             ; word: the element's key slot
wf_kpend    equ WRB(44)             ; word: its key, pending the draw
wd_bw       equ WRB(46)             ; word: the band - its width...
wd_bh       equ WRB(48)             ; ...rows...
wd_bst      equ WRB(50)             ; ...and stride
wf_bbw      equ WRB(52)             ; word: a key's inside, its size...
wf_bbh      equ WRB(54)
wf_bx       equ WRB(56)             ; ...and its place
wf_by       equ WRB(58)
wf_bpic     equ WRB(60)             ; word: its picture...
wf_bcap     equ WRB(62)             ; ...its caption...
wf_bink     equ WRB(64)             ; ...its roles, ink | paper << 8
wr_bseg     equ WRB(66)             ; word: the waveform band's claim, 0 none
wf_px       equ WRB(68)             ; words: wf_bplace's answers
wf_py       equ WRB(70)
wf_cx       equ WRB(72)
wf_cy       equ WRB(74)
wf_pich     equ WRB(76)
wf_split    equ WRB(78)
wf_pw1      equ WRB(80)             ; 40 bytes of words from here
wf_ph1      equ WRB(82)
wf_px2      equ WRB(84)
wf_py2      equ WRB(86)
wf_lh       equ WRB(88)             ; the waveform's lanes: height, centre,
wf_lc       equ WRB(90)             ; width, x phase, stride
wf_ww       equ WRB(92)
wf_wk       equ WRB(94)
wf_wst      equ WRB(96)
wr_tbuf     equ WRB(120)            ; 40: a field's text
wr_tbuf2    equ WRB(160)            ; 40: a message being built
wr_stbuf    equ WRB(200)            ; 40: the status bar's message
wd_rects    equ WRB(240)            ; WR_NB x 4 words: the keys, screen
wr_bflags   equ wd_rects + WR_NB * 8    ; WR_NB words: as drawn
wf_keys     equ wr_bflags + WR_NB * 2   ; WF_NKEYS words: as drawn
wd_band     equ wf_keys + WF_NKEYS * 2  ; WD_BANDSZ: the composing band
%if wd_band + WD_BANDSZ - os88_image_end != WRB_SIZE0
%error "WaveRack's bss layout and OS88_BSS disagree"
%endif
