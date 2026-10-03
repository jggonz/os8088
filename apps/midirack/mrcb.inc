; =============================================================================
; os8088 - apps/midirack/mrcb.inc
;
; THE CALLBACKS AND THE TRANSPORT (SPEC.md 105.9.2). Every proc here is a
; near proc with a near `ret` (the header's dispatcher, SPEC.md 20.1).
;
;   mr_paint     W_PAINT        the whole content (white-filled, clip armed)
;   mr_onup      W_ONMOUSEUP    a button FIRES here (SPEC.md 13.7)
;   mr_ondrag    W_ONDRAG       ...a pressed button follows the pointer
;   mr_onclick   W_ONCLICK      what the button library passed on: the list,
;                               its scroll bar, the check boxes, a card
;   mr_onkey     W_ONKEY        the keyboard's transport
;   mr_oncmd     menu           File / Play / Options
;   mr_about     About          the card
;   mr_onwake    WM_ONWAKE      the settled first paint, the launch document
;                               or the shipped songs, and a song's end
;   mr_onclose   WM_ONCLOSE     everything stopped and given back
;   mr_onfile    fdlg done      Add... and Load Directory...
; =============================================================================

    MRB mr_dlgmode                  ; 0 Add / 1 Load Directory
    MRBUF mr_dlgname, 14
    MRB mr_clkrow                   ; the double-click's: the row...
    MRW mr_clkt                     ; ...and the tick

; =============================================================================
mr_paint:
    call mru_draw
    ret

; =============================================================================
mr_onup:
    push ax
    push bx
    mov bx, mru_btrec
    call os88ui_btnup               ; AX = what FIRED (1-based), 0 = cancelled
    or ax, ax
    jz .out
    cmp byte [mr_card], MR_CARD_SET ; the record is the dialog's pair
    jne .rack
    cmp al, 1
    jne .cancel
    call mr_set_ok
    jmp short .out
.cancel:
    call mr_set_cancel
    jmp short .out
.rack:
    dec ax
    call mr_button
.out:
    pop bx
    pop ax
    ret

mr_ondrag:
    push ax
    push bx
    mov bx, mru_btrec
    call os88ui_btndrag
    pop bx
    pop ax
    ret

; mr_button - AL = the button that fired
mr_button:
    push ax
    push bx
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [mr_btab+bx]
    pop bx
    pop ax
    ret

mr_btab:
    dw mr_cmd_add, mr_cmd_remove, mr_cmd_dir
    dw mr_cmd_prev, mr_cmd_play, mr_cmd_pause, mr_cmd_stop, mr_cmd_next
    dw mr_cmd_eject, mr_cmd_loop
    dw mr_cmd_settings, mr_cmd_info, mr_cmd_about, mr_cmd_quit

; =============================================================================
; mr_onclick - CX = x, DX = y (SCREEN), SI = the window. The button library
; has had the press first and found no button under it
; =============================================================================
mr_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mru_layout
    call mru_origin
    call mru_aim
    mov al, [mr_card]
    or al, al
    jz .nocard
    cmp al, MR_CARD_SET
    jne .close                      ; About, MIDI Info: any press takes it down
    call mr_setclick                ; THE DIALOG IS MODAL: its controls, and a
    jmp .out                        ; press anywhere else is nothing
.close:
    mov byte [mr_card], 0
    call mru_repaint
    jmp .out
.nocard:
    mov bx, mru_sb                  ; the scroll bar
    call os88ui_sbhit
    cmp al, OS88UI_SBNONE
    je .nosb
    call mr_scroll
    jmp .out
.nosb:
    cmp byte [mru_cmp], 0
    jne .nock
    mov bx, mru_ck1                 ; the check boxes: each redraws itself
    call os88ui_chkhit
    jc .c2
    mov al, [mru_ck1+OS88UI_CK_ON]
    mov [mru_ckd], al               ; (the library has drawn it)
    mov [mr_levels], al
    mov byte [mr_cfgdirty], 1
    call mr_levels_redraw
    jmp .out
.c2:
    mov bx, mru_ck2
    call os88ui_chkhit
    jc .c3
    mov al, [mru_ck2+OS88UI_CK_ON]
    mov [mru_ckd+1], al
    mov [mr_loop], al
    mov byte [mr_cfgdirty], 1
    call mru_update
    jmp .out
.c3:
    call mr_isspk                   ; greyed unless the speaker plays
    jnc .nock
    mov bx, mru_ck3
    call os88ui_chkhit
    jc .nock
    mov al, [mru_ck3+OS88UI_CK_ON]
    mov [mru_ckd+2], al
    mov [mr_bg], al
    mov byte [mr_cfgdirty], 1
    call mru_update
    jmp .out
.nock:
    call mr_listhit                 ; the list's rows
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mr_listhit - a press at CX, DX (screen) on a row of the list: select it,
; and a second press on the same row within half a second plays it
mr_listhit:
    push ax
    push bx
    push cx
    push dx
    sub cx, [mru_ox]
    sub dx, [mru_oy]
    cmp cx, [mru_lb]
    jbe .out
    mov ax, [mru_lb+4]
    sub ax, 14
    cmp cx, ax
    jae .out
    mov ax, dx
    sub ax, [mru_lb+2]
    jbe .out
    sub ax, 1
    jb .out
    xor dx, dx
    div word [mru_rp]               ; AX = the row
    cmp ax, [mru_lrows]
    jae .out
    add al, [mrl_top]
    cmp al, [mrl_n]
    jae .out
    mov bl, al
    call OSAPI_GET_TICKS
    mov cx, ax
    sub ax, [mr_clkt]
    mov [mr_clkt], cx
    cmp bl, [mr_clkrow]
    mov [mr_clkrow], bl
    jne .sel
    cmp ax, 9
    ja .sel
    mov [mrl_sel], bl               ; the double press: play it
    mov word [mr_clkt], 0x8000      ; (a third press is a new first)
    call mr_play_sel
    jmp short .out
.sel:
    mov [mrl_sel], bl
    call mr_list_redraw
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mr_list_redraw - the list's rows and its bar, under our own clip
mr_list_redraw:
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_layout
    call mru_origin
    call mru_aim
    call mru_list_sync
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    ret

; mr_levels_redraw - Show Levels moved: every bar again
mr_levels_redraw:
    push bx
    xor bx, bx
.c:
    or byte [mrc_dirty+bx], 2
    inc bx
    cmp bx, MR_NCH
    jb .c
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_dyn
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    ret

; mr_scroll - AL = the scroll bar's part: a row or a page
mr_scroll:
    push ax
    push bx
    push cx
    mov bl, [mrl_top]
    mov cx, [mru_lrows]
    cmp al, OS88UI_SBUP
    jne .n1
    or bl, bl
    jz .out
    dec bl
    jmp short .set
.n1:
    cmp al, OS88UI_SBDOWN
    jne .n2
    inc bl
    jmp short .clamp
.n2:
    cmp al, OS88UI_SBPGUP
    jne .n3
    sub bl, cl
    jnc .set
    xor bl, bl
    jmp short .set
.n3:
    cmp al, OS88UI_SBPGDN
    jne .out
    add bl, cl
.clamp:
    mov al, [mrl_n]                 ; the last top that still fills the box
    sub al, cl
    jnc .c
    xor al, al
.c:
    cmp bl, al
    jbe .set
    mov bl, al
.set:
    cmp bl, [mrl_top]
    je .out
    mov [mrl_top], bl
    call mr_list_redraw
.out:
    pop cx
    pop bx
    pop ax
    ret

; mr_isspk - CF = 1 when a BRACKET output - the speaker or a Covox - is the
; one a Play would use (so Play in Background means something: neither can
; play on the desktop, and the tone is what plays there instead)
mr_isspk:
    push ax
    mov al, [mr_want]
    cmp al, MR_OUT_SPK
    je .yes
    cmp al, MR_OUT_LPT
    je .yes
    cmp al, MR_OUT_AUTO
    jne .no
    cmp byte [mr_fmkind], 0
    jne .no
    cmp byte [mr_hassb], 0
    jne .no
.yes:
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; =============================================================================
mr_onkey:                           ; AL = ascii, AH = scancode
    push ax
    push bx
    cmp byte [mr_card], MR_CARD_SET ; the dialog: Enter is OK, Esc is
    jne .notset                     ; Cancel, and nothing else reaches the
    cmp al, 0x0D                    ; player under it
    jne .nok
    call mr_set_ok
    jmp .out
.nok:
    cmp al, 0x1B
    jne .out
    call mr_set_cancel
    jmp .out
.notset:
    cmp byte [mr_card], 0
    je .nocard
    mov byte [mr_card], 0           ; any key takes a card down
    call mru_repaint
    jmp .out
.nocard:
    cmp ah, KSC_UP
    je .up
    cmp ah, KSC_DOWN
    je .down
    cmp ah, KSC_LEFT
    je .prev
    cmp ah, KSC_RIGHT
    je .next
    cmp al, ' '
    je .toggle
    cmp al, 0x0D
    je .enter
    cmp al, 0x1B
    je .stop
    or al, 0x20
    cmp al, 's'
    je .stop
    cmp al, 'n'
    je .next
    cmp al, 'p'
    je .prev
    cmp al, 'l'
    je .loop
    cmp al, 'i'
    je .info
    jmp .out
.toggle:
    cmp byte [mr_state], MR_ST_PLAY
    jne .play
    call mr_cmd_pause
    jmp .out
.enter:
    mov al, [mrl_sel]               ; Enter: the highlighted song, from its
    cmp byte [mr_loaded], 0         ; start, unless it is the one paused
    je .es
    cmp al, [mr_lidx]
    jne .es
.play:
    call mr_cmd_play
    jmp .out
.es:
    call mr_play_sel
    jmp .out
.stop:
    call mr_cmd_stop
    jmp .out
.next:
    call mr_cmd_next
    jmp .out
.prev:
    call mr_cmd_prev
    jmp .out
.loop:
    call mr_cmd_loop
    jmp .out
.info:
    call mr_cmd_info
    jmp .out
.up:
    mov al, [mrl_sel]
    or al, al
    jz .out
    dec al
    jmp short .mv
.down:
    mov al, [mrl_sel]
    inc al
    cmp al, [mrl_n]
    jae .out
.mv:
    mov [mrl_sel], al
    call mr_seen                    ; scrolled into view
    call mr_list_redraw
.out:
    pop bx
    pop ax
    ret

; mr_seen - the top moved so the highlighted row is on show
mr_seen:
    push ax
    push cx
    mov al, [mrl_sel]
    cmp al, [mrl_top]
    jae .lo
    mov [mrl_top], al
    jmp short .out
.lo:
    mov cx, [mru_lrows]
    or cx, cx
    jz .out
    mov ah, [mrl_top]
    add ah, cl
    cmp al, ah
    jb .out
    sub al, cl
    inc al
    mov [mrl_top], al
.out:
    pop cx
    pop ax
    ret

; =============================================================================
mr_oncmd:                           ; AL = item, AH = menu
    push ax
    cmp ah, 1
    je .play
    ja .opt
    cmp al, MR_CMD_ADD
    jne .f1
    call mr_cmd_add
    jmp .out
.f1:
    cmp al, MR_CMD_DIR
    jne .f2
    call mr_cmd_dir
    jmp .out
.f2:
    cmp al, MR_CMD_REM
    jne .f3
    call mr_cmd_remove
    jmp .out
.f3:
    cmp al, MR_CMD_CLR
    jne .f4
    call mr_cmd_clear
    jmp .out
.f4:
    call mr_cmd_quit
    jmp .out
.play:
    push bx
    mov bl, al                      ; Play, Pause, Stop, Previous, Next, Loop
    xor bh, bh
    shl bx, 1
    call [mr_ptab+bx]
    pop bx
    jmp .out
.opt:
    cmp al, MR_CMD_SET
    jne .o1
    call mr_cmd_settings
    jmp .out
.o1:
    cmp al, MR_CMD_INFO
    jne .o2
    call mr_cmd_info
    jmp .out
.o2:
    cmp al, MR_CMD_LEV
    jne .o3
    xor byte [mr_levels], 1
    mov byte [mr_cfgdirty], 1
    call mr_levels_redraw
    call mru_update
    jmp .out
.o3:
    cmp al, MR_CMD_BG
    jne .o4
    call mr_isspk
    jnc .out                        ; (only the speaker has a background)
    xor byte [mr_bg], 1
    mov byte [mr_cfgdirty], 1
    call mru_update
    jmp .out
.o4:
    call mr_cmd_about
.out:
    pop ax
    ret

mr_ptab:    dw mr_cmd_play, mr_cmd_pause, mr_cmd_stop, mr_cmd_prev, mr_cmd_next, mr_cmd_loop

; =============================================================================
; THE TRANSPORT
; =============================================================================

; mr_play_sel - the highlighted song, loaded (if it is not) and played from
; its start
mr_play_sel:
    push ax
    cmp byte [mrl_n], 0
    jne .have
    mov word [mr_msg], mr_s_empty
    call mru_update
    jmp short .out
.have:
    call mro_close                  ; the worker out of the song first
    mov byte [mr_state], MR_ST_STOP
    mov al, [mrl_sel]
    cmp byte [mr_loaded], 0
    je .load
    cmp al, [mr_lidx]
    je .play
.load:
    mov word [mr_msg], mr_s_loading
    call mru_update
    call mrl_load
    jnc .play
    call mru_update                 ; ([mr_msg] says why)
    jmp short .out
.play:
    call mro_play                   ; the speaker: returns from its bracket
    call mru_update
.out:
    pop ax
    ret

mr_cmd_play:
    cmp byte [mr_state], MR_ST_PAUSE
    jne .fresh
    call mro_resume
    call mru_update
    ret
.fresh:
    cmp byte [mr_state], MR_ST_PLAY
    jne mr_play_sel
    mov al, [mrl_sel]               ; playing already: Play on another
    cmp al, [mr_lidx]               ; highlighted song starts that one
    jne mr_play_sel
    ret

mr_cmd_pause:
    cmp byte [mr_state], MR_ST_PAUSE
    jne .p
    jmp mr_cmd_play                 ; Pause again resumes
.p:
    call mro_pause
    call mru_update
    ret

mr_cmd_stop:
    call mro_close
    mov byte [mr_state], MR_ST_STOP
    mov word [mr_msg], mr_s_stopped
    call mr_chan_rest
    call mru_update
    ret

; mr_chan_rest - a stopped song shows its opening instruments and no level
mr_chan_rest:
    cmp byte [mr_loaded], 0
    je .out
    push ax
    call mrl_dryprime
    pop ax
.out:
    ret

mr_cmd_next:
    mov al, [mrl_sel]
    inc al
    cmp al, [mrl_n]
    jae .end
    jmp short mr_skip
.end:
    mov word [mr_msg], mr_s_endlist
    jmp mr_cmd_stop

mr_cmd_prev:
    mov al, [mrl_sel]
    or al, al
    jz mr_skip                      ; the first: from its start again
    dec al
    ; fall through

; mr_skip - AL becomes the highlighted song: played if something was
; playing or paused, else only highlighted
mr_skip:
    mov [mrl_sel], al
    call mr_seen
    cmp byte [mr_state], MR_ST_STOP
    jne mr_play_sel
    call mr_list_redraw
    ret

mr_cmd_eject:
    call mro_close
    mov byte [mr_state], MR_ST_STOP
    mov byte [mr_loaded], 0
    mov word [mr_msg], mr_s_ready
    call mr_first_reset             ; the rack: back to nothing
    call mr_chan_reset
    call mru_update
    ret

mr_cmd_loop:
    xor byte [mr_loop], 1
    mov byte [mr_cfgdirty], 1
    call mru_update
    ret

mr_cmd_remove:
    cmp byte [mrl_n], 0
    je .out
    mov al, [mrl_sel]
    cmp byte [mr_loaded], 0
    je .rm
    cmp al, [mr_lidx]
    jne .other
    call mro_close                  ; the song playing goes with its entry
    mov byte [mr_state], MR_ST_STOP
    mov word [mr_msg], mr_s_ready
.rm:
    call mrl_remove
    jmp short .rep
.other:
    pushf                           ; above the loaded song: it keeps its
    call mrl_remove                 ; index; below it: it moves down one
    popf
    ja .keep
    dec byte [mr_lidx]
.keep:
    mov byte [mr_loaded], 1         ; (mrl_remove forgot it; it is still in)
.rep:
    call mru_update
.out:
    ret

mr_cmd_clear:
    call mro_close
    mov byte [mr_state], MR_ST_STOP
    call mrl_init
    mov word [mr_msg], mr_s_ready
    call mr_first_reset
    call mr_chan_reset
    call mru_update
    ret

mr_cmd_add:
    mov byte [mr_dlgmode], 0
    jmp short mr_dlg
mr_cmd_dir:
    mov byte [mr_dlgmode], 1
mr_dlg:
    push ax
    push bx
    push si
    push di
    mov al, FDLG_OPEN
    mov bx, [mr_win]
    mov di, mr_onfile
    xor si, si
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

mr_cmd_settings:
    push ax
    call mr_lptinfo                 ; the Covox row greyed by today's tier
    mov al, [mr_want]               ; the dialog edits COPIES: OK commits
    mov [mrs_out], al
    mov al, [mr_rate]
    mov [mrs_rate], al
    mov al, [mr_bg]
    mov [mrs_bg], al
    mov al, MR_CARD_SET
    call mru_card_up
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_setclick - a press on the Settings dialog (CX, DX screen): a pick in
; either group or the check box - each control redraws itself - or nothing
; -----------------------------------------------------------------------------
mr_setclick:
    push ax
    push bx
    call mru_screct
    mov bx, mru_rad
    call os88ui_radhit
    jc .r2
    jnz .out
    mov al, [mru_rad+OS88UI_RD_SEL]
    mov [mrs_out], al
    jmp short .out
.r2:
    mov bx, mru_rad2
    call os88ui_radhit
    jc .ck
    jnz .out
    mov al, [mru_rad2+OS88UI_RD_SEL]
    mov [mrs_rate], al
    jmp short .out
.ck:
    cmp byte [mru_cmp], 0           ; (compact has no such box: the menu's
    jne .out                        ; Play in Background is the way there)
    mov bx, mru_ck4
    call os88ui_chkhit
    jc .out
    mov al, [mru_ck4+OS88UI_CK_ON]
    mov [mrs_bg], al
.out:
    pop bx
    pop ax
    ret

; mr_set_cancel - the dialog down, nothing changed
mr_set_cancel:
    mov byte [mr_card], 0
    call mru_repaint
    ret

; -----------------------------------------------------------------------------
; mr_set_ok - the dialog's copies committed and kept (MIDIRACK.CFG), and a
; song that is playing or paused RESTARTED on what was chosen, so a change is
; heard at once rather than at the next Play
; -----------------------------------------------------------------------------
mr_set_ok:
    push ax
    push bx
    xor bl, bl                      ; BL = did the output or the rate move?
    mov al, [mrs_out]
    cmp al, [mr_want]
    je .o
    inc bl
.o:
    mov [mr_want], al
    mov al, [mrs_rate]
    cmp al, [mr_rate]
    je .r
    inc bl
.r:
    mov [mr_rate], al
    mov al, [mrs_bg]
    cmp al, [mr_bg]
    je .b
    inc bl
.b:
    mov [mr_bg], al
    mov byte [mr_card], 0
    call mru_repaint                ; the card down
    or bl, bl
    jz .out
    call mr_cfg_save
    cmp byte [mr_state], MR_ST_STOP
    je .out
    call mr_play_sel                ; the song again, on the new choice
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE SETTINGS, KEPT (SPEC.md 105.9.2): MIDIRACK.CFG in SYSTEM\APPDATA on our
; own volume (SPEC.md 19.9) - the output, the rate, and the three check boxes.
; Read once at the first wake; written when the dialog commits a change and
; at the close when a check box moved. A file that is not ours (the magic) is
; ignored and one that is is CLAMPED, never refused (apps/pixelstein's
; pxset.inc, whose shape this is). A disk without the folder simply keeps
; nothing, which is a user's own disk and not an error
; =============================================================================
MR_CFGN     equ 4 + 5
mr_cfg_file:  db 'MIDIRACK.CFG', 0
mr_cfg_magic: db 'MRC', 1
mr_s_system:  db 'SYSTEM', 0
mr_s_appdat:  db 'APPDATA', 0
    MRBUF mr_cfgbuf, 16
    MRW mr_cfghd                    ; where we were standing
    MRB mr_cfghv

; mr_cfg_enter - into SYSTEM\APPDATA on our volume; CF = 1 there is none
mr_cfg_enter:
    push bx
    push dx
    push si
    call OSAPI_FILE_HERE
    mov [mr_cfghd], dx
    mov [mr_cfghv], bl
    call mrl_root
    mov si, mr_s_system
    call mrl_enter
    jc .back
    mov si, mr_s_appdat
    call mrl_enter
    jc .back
    pop si
    pop dx
    pop bx
    clc
    ret
.back:
    call mr_cfg_leave
    pop si
    pop dx
    pop bx
    stc
    ret

; mr_cfg_leave - back where mr_cfg_enter found us. Preserves every register
; and the flags
mr_cfg_leave:
    pushf
    push ax                         ; (GOTO_QM answers in AX, and the read's
    push bx                         ; size is still in it)
    push dx
    mov dx, [mr_cfghd]
    mov bl, [mr_cfghv]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    pop ax
    popf
    ret

; mr_cfg_load - the kept settings, if there are any
mr_cfg_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call mr_cfg_enter
    jc .out
    push ds
    pop es
    mov si, mr_cfg_file
    mov bx, mr_cfgbuf
    mov cx, 16
    xor dx, dx
    call OSAPI_FILE_READ            ; DX:AX = its size
    call mr_cfg_leave
    jc .out
    or dx, dx
    jnz .out
    cmp ax, MR_CFGN
    jne .out
    mov ax, [mr_cfgbuf]
    cmp ax, [mr_cfg_magic]
    jne .out
    mov ax, [mr_cfgbuf+2]
    cmp ax, [mr_cfg_magic+2]
    jne .out
    mov al, [mr_cfgbuf+4]           ; each byte clamped to what it may hold
    cmp al, MR_NOUT
    jb .w
    xor al, al
.w:
    mov [mr_want], al
    mov al, [mr_cfgbuf+5]
    cmp al, MR_NRATE
    jb .rt
    xor al, al
.rt:
    mov [mr_rate], al
    mov al, [mr_cfgbuf+6]
    and al, 1
    mov [mr_bg], al
    mov al, [mr_cfgbuf+7]
    and al, 1
    mov [mr_loop], al
    mov al, [mr_cfgbuf+8]
    and al, 1
    mov [mr_levels], al
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mr_cfg_save - the settings written; [mr_cfgdirty] cleared only when the
; write LANDED (pxset.inc's review lesson)
mr_cfg_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov byte [mr_cfgdirty], 1
    mov ax, [mr_cfg_magic]
    mov [mr_cfgbuf], ax
    mov ax, [mr_cfg_magic+2]
    mov [mr_cfgbuf+2], ax
    mov al, [mr_want]
    mov [mr_cfgbuf+4], al
    mov al, [mr_rate]
    mov [mr_cfgbuf+5], al
    mov al, [mr_bg]
    mov [mr_cfgbuf+6], al
    mov al, [mr_loop]
    mov [mr_cfgbuf+7], al
    mov al, [mr_levels]
    mov [mr_cfgbuf+8], al
    call mr_cfg_enter
    jc .out
    push ds
    pop es
    mov si, mr_cfg_file
    mov bx, mr_cfgbuf
    mov cx, MR_CFGN
    xor dx, dx
    call OSAPI_FILE_WRITE
    call mr_cfg_leave
    jc .out
    mov byte [mr_cfgdirty], 0
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mr_cmd_info:
    cmp byte [mr_loaded], 0
    je .out
    push ax
    mov al, MR_CARD_INFO
    call mru_card_up
    pop ax
.out:
    ret

mr_cmd_about:
    push ax
    mov al, MR_CARD_ABOUT
    call mru_card_up
    pop ax
    ret

mr_cmd_quit:
    push bx
    call mro_close
    mov bx, [mr_win]
    call OSAPI_WM_CLOSE             ; deferred to the next UI pass (75.2)
    pop bx
    ret

; mr_about - OSAPI_ABOUT_SET's handler (SI = the window; gfx lock held)
mr_about:
    jmp mr_cmd_about

; =============================================================================
; mr_onwake - UI task, NO gfx lock (SPEC.md 74.1)
; =============================================================================
mr_onwake:
    push ax
    push bx
    push dx
    push si
    cmp byte [mr_first_paint], 0
    je .nofirst
    mov byte [mr_first_paint], 0
    call OSAPI_GFX_LOCK
    call mr_cfg_load                ; the settings kept from the last session
    cmp byte [mr_arg_pending], 0    ; the document we were launched on, or
    je .auto                        ; the songs that ship
    mov byte [mr_arg_pending], 0
    mov si, mr_arg_name
    call mr_ismid
    jne .auto
    mov dx, [mr_arg_dir]
    mov bl, [mr_arg_vol]
    call OSAPI_FILE_GOTO_QM         ; its folder: the rest of it too
    call mrl_adddir
    call mr_select_name             ; ...and the one double-clicked, played
    call mru_update
    call mr_play_sel
    jmp short .fdone
.auto:
    mov word [mr_msg], mr_s_loading
    call mru_update
    call mrl_autoload
    mov word [mr_msg], mr_s_ready
    cmp byte [mrl_n], 0
    jne .al
    mov word [mr_msg], mr_s_empty
    jmp short .rp
.al:
    xor al, al                      ; the first song, ready to play
    call mrl_load
.rp:
    call mru_update
.fdone:
    call OSAPI_GFX_UNLOCK
.nofirst:
    cmp byte [mr_ended], 0          ; a song has played out (the worker, or
    je .out                         ; the speaker's bracket)
    call OSAPI_GFX_LOCK
    call mr_songend
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; mr_select_name - the highlight on the entry named mr_arg_name
mr_select_name:
    push ax
    push bx
    push si
    push di
    xor ax, ax
.e:
    cmp al, [mrl_n]
    jae .out
    push ax
    call mrl_ptr
    mov di, bx
    mov si, mr_arg_name
    call mr_nameeq
    pop ax
    je .hit
    inc al
    jmp short .e
.hit:
    mov [mrl_sel], al
    call mr_seen
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; mr_songend - what a song's end means: again (Loop), the next or the one
; before (a skip in the speaker's bracket), or the list's end
mr_songend:
    push ax
    mov byte [mr_ended], 0
    mov al, [mr_ehint]
    or al, al
    jnz .skip
    cmp byte [mr_loop], 0
    je .adv
    mov al, [mr_lidx]               ; LOOP: the same song from its start
    mov [mrl_sel], al
    call mr_play_sel
    jmp short .out
.adv:
    mov al, [mrl_sel]
    inc al
    cmp al, [mrl_n]
    jb .go
    call mro_close                  ; the list's end
    mov byte [mr_state], MR_ST_STOP
    mov word [mr_msg], mr_s_endlist
    call mr_chan_rest
    call mru_update
    jmp short .out
.skip:
    mov ah, [mrl_sel]
    cmp al, 1
    jne .prv
    inc ah
    cmp ah, [mrl_n]
    jb .sk
    dec ah
    jmp short .sk
.prv:
    or ah, ah
    jz .sk
    dec ah
.sk:
    mov al, ah
.go:
    mov [mrl_sel], al
    call mr_seen
    call mr_play_sel
.out:
    pop ax
    ret

; =============================================================================
mr_onclose:                         ; SI = the window; gfx lock held
    call mro_close
    cmp byte [mr_cfgdirty], 0       ; a check box moved since the file
    je .kept
    call mr_cfg_save
.kept:
    call mro_free
    call mra_free
    call mwt_free
    push dx
    mov dx, [mr_fseg]
    or dx, dx
    jz .n
    call OSAPI_MEM_FREE
    mov word [mr_fseg], 0
.n:
    pop dx
    clc                             ; nothing unsaved: the close goes ahead
    ret

; =============================================================================
; mr_onfile - the Standard File dialog's completion: AL = mode, DI = the
; chosen name (KERNEL_SEG, through ES), DX:CX = its size. UI task, lock held
; =============================================================================
mr_onfile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, di                      ; the name out of the kernel's buffer
    mov di, mr_dlgname
    mov cx, 13
.cn:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .cd
    inc si
    inc di
    loop .cn
.cd:
    mov byte [mr_dlgname+13], 0
    cmp byte [mr_dlgmode], 0
    jne .dir
    mov si, mr_dlgname
    call mr_ismid
    jne .notmid
    call OSAPI_FILE_HERE            ; the folder the dialog left us in
    mov si, mr_dlgname
    call mrl_add                    ; AL = its index
    jc .full
    mov [mrl_sel], al
    call mr_seen
    call mrl_takepath
    mov word [mr_msg], mr_s_added
    cmp byte [mr_state], MR_ST_STOP ; idle: it is the song now
    jne .rep
    call mrl_load
    jmp short .rep
.dir:
    call mrl_adddir                 ; every .MID in the folder chosen in
    or ax, ax
    jnz .some
    mov word [mr_msg], mr_s_nodir
    jmp short .rep
.some:
    mov word [mr_msg], mr_s_added
    cmp byte [mr_state], MR_ST_STOP
    jne .rep
    cmp byte [mr_loaded], 0
    jne .rep
    mov al, [mrl_sel]
    call mrl_load
    jmp short .rep
.notmid:
    mov word [mr_msg], mr_s_nomid
    jmp short .rep
.full:
    mov word [mr_msg], mr_s_full
.rep:
    call mru_update
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
