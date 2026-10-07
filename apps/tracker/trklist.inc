; =============================================================================
; os8088 - apps/tracker/trklist.inc
;
; The PLAYLIST (SPEC.md 45.22): the store, the walk, and the editor window -
; ModPlug Player's PlayList Editor (SPEC.md 56.8) brought over. Prefix tpl_.
;
; THE STORE IS NOT THE EDITOR'S. The transport reads it - a song ending walks
; the list whether or not the editor was ever opened - and so does the
; FULLSCREEN bracket, which is the point of 45.22.2: a list plays on through
; either fullscreen surface, one module after the next, with no window in
; sight. An entry is a name AND a folder (drive and first cluster, from
; OSAPI_FILE_HERE at the moment it was added), because a list is built out of
; more than one directory and a name alone would be looked for in whichever
; one the last dialog left us in.
;
; The editor is a second window that is NEVER BOUND (SPEC.md 56.2): its close
; box only hides it, teardown sweeps it by our segment, and the Add... dialog
; is opened against the PLAYER's window, because fdlg refuses a window no
; instance owns.
;
; AND NOTHING ITS HANDLERS DO MAY TOUCH A FILE OR THE SOUND CARD (45.22.3).
; A callback is billed to the instance that OWNS the window (inst_win_owner),
; and this window's owner is nobody - so a play started from here claimed its
; ring grant as the KERNEL, the player's own close could not free it, and the
; next stream open was refused (err 7) with the grant still held. The file
; API is the same question (SPEC.md 19.2.1: it stands in the CALLER's folder).
; So the editor edits the list in place and POSTS everything else - a play, a
; forwarded key, Add... - to the player's wake handler (tpl_post / tpl_run),
; which the kernel bills to us.
;
; Its buttons are the same standard buttons as the face's - press inverted,
; release fires, slide off cancels - and its rows are opaque runs, drawn only
; when what they say changed.
; =============================================================================

TPL_MAX     equ 16                  ; entries
TPL_ESZ     equ 16                  ; an entry: name[13], drive, cluster
TPL_E_DRV   equ 13
TPL_E_CLU   equ 14
TPL_W       equ 256                 ; the editor's content
TPL_H       equ 136                 ; ...which is CGA's whole desktop band
TPL_RX      equ 8                   ; the rows: x, first y, cells
TPL_RY      equ 4
TPL_RCELLS  equ 20
TPL_NB      equ 6                   ; buttons
TPL_BX      equ 176
TPL_BW      equ 72
TPL_BH      equ 16
TPL_BPY     equ 20
TPLQ_PLAY   equ 1                   ; tpl_post's requests
TPLQ_ADD    equ 2
TPLQ_KEY    equ 3

; -----------------------------------------------------------------------------
; tpl_ent - AL = an index -> SI = its entry. Preserves the rest.
; -----------------------------------------------------------------------------
tpl_ent:
    push ax
    push cx
    xor ah, ah
    mov si, ax
    mov cl, 4
    shl si, cl
    add si, tpl_ents
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tpl_here - [tpl_new] = [trk_fname] in the folder we are in NOW, as an entry:
; the name up to its NUL and zeros after it, so two entries for one file are
; the same sixteen bytes. Preserves everything.
; -----------------------------------------------------------------------------
tpl_here:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, trk_fname
    mov di, tpl_new
    mov cx, 13
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    loopnz .cp
    jcxz .dir
    xor al, al                      ; the name ended early: zero the rest
.z:
    mov [di], al
    inc di
    loop .z
.dir:
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the drive
    mov [tpl_new + TPL_E_DRV], bl
    mov [tpl_new + TPL_E_CLU], dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tpl_same - SI = an entry, DI = another: ZF = 1 the same file. Preserves all
; but the flags.
tpl_same:
    push cx
    push si
    push di
    mov cx, TPL_ESZ / 2
    push es
    push ds
    pop es
    cld
    repe cmpsw
    pop es
    pop di
    pop si
    pop cx
    ret

; tpl_push - [tpl_new] onto the end. CF = 1 the list is full; else AL = its
; index. Preserves the rest.
tpl_push:
    mov al, [tpl_n]
    cmp al, TPL_MAX
    cmc
    jc .out                         ; (jae as a carry: full)
    push cx
    push si
    push di
    call tpl_ent
    mov di, si
    mov si, tpl_new
    mov cx, TPL_ESZ / 2
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    inc byte [tpl_n]
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; tpl_add - put [trk_fname], in the folder we are in now, on the end of the
; list. From trk_fdone, when the dialog was the editor's Add... (lock held).
; -----------------------------------------------------------------------------
tpl_add:
    push ax
    push si
    call tpl_here
    call tpl_push
    mov si, tpl_s_full
    jc .say
    mov si, tpl_s_added
.say:
    call tui_msg
    call tpl_refresh
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tpl_note - a module LOADED (trk_fdone's success path, lock held): bank what
; it is in [tpl_loaded], and unless the list itself asked for it, make it the
; list's current entry - found where it already is, or put on the end
; (SPEC.md 45.22.4). So opening a song is adding it: a double-click on one
; file is a one-entry list under Repeat List, which loops it exactly as
; Repeat Song would, and the second song opened joins the first rather than
; replacing it. A full list keeps playing what it has; the song is just not
; on it.
; -----------------------------------------------------------------------------
tpl_note:
    push ax
    push cx
    push si
    push di
    call tpl_here
    mov si, tpl_new                 ; what is in the grant from now on
    mov di, tpl_loaded
    mov cx, TPL_ESZ / 2
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    cmp byte [tpl_inplay], 0
    jne .out                        ; tpl_play set [tpl_cur] itself
    mov di, tpl_new
    xor al, al
.find:
    cmp al, [tpl_n]
    jae .push
    call tpl_ent
    call tpl_same
    je .cur
    inc al
    jmp short .find
.push:
    call tpl_push
    jnc .cur
    mov al, 0FFh                    ; full: playing, and not on the list
.cur:
    mov [tpl_cur], al
    mov [tpl_sel], al
.out:
    call tpl_refresh
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tpl_play - load and play entry AL (lock held; UI task: a windowed callback,
; a wake, or the fullscreen bracket, all of which may use the file slots)
; -----------------------------------------------------------------------------
tpl_play:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp al, [tpl_n]
    jae .out
    mov [tpl_cur], al
    mov [tpl_sel], al
    call tpl_ent
    cmp byte [mp_loaded], 0         ; THE SONG THAT IS ALREADY IN THE GRANT is
    je .fetch                       ; not read again (SPEC.md 45.22.4): a list
    mov di, tpl_loaded              ; that goes round to the module playing -
    call tpl_same                   ; one entry, or the same file listed twice
    jne .fetch                      ; - restarts the stream and costs no disk
    mov byte [trk_pause], 0
    xor al, al
    call trk_play
    jmp short .out
.fetch:
    call OSAPI_FILE_HERE            ; already there? A GOTO is a REMOUNT - real
    cmp bl, [si + TPL_E_DRV]        ; floppy I/O - so it is only paid for when
    jne .go                         ; the entry is somewhere else
    cmp dx, [si + TPL_E_CLU]
    je .here
.go:
    mov bl, [si + TPL_E_DRV]
    mov dx, [si + TPL_E_CLU]
    call OSAPI_FILE_GOTO
    jnc .here
    push si
    mov si, trk_s_noent
    call tui_msg
    pop si
    jmp short .out
.here:
    push ds
    pop es
    mov di, si                      ; trk_fdone copies the name out of ES:DI,
    xor cx, cx                      ; and DX:CX = 0 is "no size": it asks the
    xor dx, dx                      ; directory (trk_sizeof)
    xor al, al
    mov byte [tpl_inplay], 1        ; so the load keeps [tpl_cur]
    call trk_fdone
    mov byte [tpl_inplay], 0
.out:
    call tpl_refresh
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tpl_pick - AL = the entry after the current one: the next in order, or a
; random other one when Shuffle is on. The list is not empty.
tpl_pick:
    push bx
    push dx
    cmp byte [trk_shuf], 0
    jne .rand
    mov al, [tpl_cur]
    inc al                          ; 0FFh (none) + 1 is entry 0
    cmp al, [tpl_n]
    jb .out
    xor al, al
    jmp short .out
.rand:
    call OSAPI_RAND
    xor dx, dx
    mov bl, [tpl_n]
    xor bh, bh
    div bx
    mov al, dl
    cmp bl, 1
    je .out
    cmp al, [tpl_cur]               ; never the same song twice running
    jne .out
    inc al
    cmp al, bl
    jb .out
    xor al, al
.out:
    pop dx
    pop bx
    ret

; tpl_next / tpl_prev - the transport's >>| and |<<. Each is a DELIBERATE
; start, so the count of songs played since one restarts (SPEC.md 56.8).
; With no list, |<< is the top of this module and >>| does nothing.
tpl_next:
    push ax
    cmp byte [tpl_n], 0
    je .out
    mov byte [tpl_played], 0
    call tpl_pick
    call tpl_play
.out:
    pop ax
    ret

tpl_prev:
    push ax
    cmp byte [tpl_n], 0
    jne .list
    xor al, al
    call tw_seek
    jmp short .out
.list:
    mov byte [tpl_played], 0
    mov al, [tpl_cur]
    dec al
    jns .p                          ; 0FFh - 1 is still "none": the first
    xor al, al
    cmp byte [tpl_cur], 0
    jne .p
    mov al, [tpl_n]                 ; from the first entry, back to the last
    dec al
.p:
    call tpl_play
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_song_over - the song ENDED (the stream is closed, trk_reap did it): the
; Repeat and Shuffle rules, and the one place they are decided (56.2's shape)
; in:  the gfx lock held, on the UI task
;
;   Repeat Song   the replayer loops by itself ([mp_endstop] = 0), so only an
;                 F00 reaches here - and plays the song again
;   no list       stop; the transport's own legend already says so
;   a list        the next entry; Repeat Off stops once the list's worth has
;                 played, Repeat List goes round again
; -----------------------------------------------------------------------------
trk_song_over:
    push ax
    cmp byte [trk_rep], 1
    jne .list
    xor al, al
    call trk_play
    jmp short .out
.list:
    cmp byte [tpl_n], 0
    je .out
    inc byte [tpl_played]
    mov al, [tpl_played]
    cmp al, [tpl_n]
    jb .adv
    cmp byte [trk_rep], 2
    jne .out
    mov byte [tpl_played], 0
.adv:
    call tpl_pick
    call tpl_play
.out:
    pop ax
    ret

; =============================================================================
; The editor window
; =============================================================================

; tpl_visible - CF = 0 the editor exists and is on the glass. Its close box
; hides it without telling us (it is never bound, SPEC.md 56.2), so this ASKS
; rather than keeping a flag that would go stale. Preserves all but flags.
tpl_visible:
    push bx
    push cx
    push dx
    mov bx, [tpl_win]
    or bx, bx
    stc
    jz .out
    call OSAPI_WM_GEOM              ; CF = 1 hidden
.out:
    pop dx
    pop cx
    pop bx
    ret

; tpl_toggle - the PlayList button, the E key and the File menu's row
tpl_toggle:
    push ax
    push bx
    push dx
    push si
    push di
    mov bx, [tpl_win]
    or bx, bx
    jnz .have
    mov ax, [tw_ox]                 ; the first time: make it, beside the
    add ax, TW_W + 8                ; player's right edge - the WM clamps it
    mov [tpl_tpl + WT_X], ax        ; back onto the screen where that is off
    mov ax, [tw_oy]
    sub ax, TITLE_H
    mov [tpl_tpl + WT_Y], ax
    mov si, tpl_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [tpl_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; its rows are runs on the byte grid too
    mov ax, bx
    push bx
    mov bx, tpl_btns
    mov si, tpl_onup
    mov di, tpl_ondrag
    mov dx, tpl_onclick
    call os88ui_btninit
    pop bx
    mov ax, tpl_clickw              ; ...and the press reaches us FIRST, for
    call OSAPI_WM_ONCLICK           ; tw_clickw's reason
    jmp short .show
.have:
    call tpl_visible
    jc .show
    call OSAPI_WM_HIDE
    jmp short .out
.show:
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tpl_org - point the drawing helpers at the EDITOR's content, banking the
; player's origin; tpl_unorg puts it back. The helpers are tw_'s: one bevel,
; one fill, one run, whichever window is being drawn (SPEC.md 56's mppu_org,
; which the whole of that player's three windows drew through).
; -----------------------------------------------------------------------------
tpl_org:
    push ax
    push bx
    push dx
    mov ax, [tw_ox]
    mov [tpl_sx], ax
    mov ax, [tw_oy]
    mov [tpl_sy], ax
    mov bx, [tpl_win]
    call OSAPI_WM_CONTENT
    mov [tw_ox], ax
    mov [tw_oy], dx
    call tpl_rects
    pop dx
    pop bx
    pop ax
    ret
tpl_unorg:
    push ax
    mov ax, [tpl_sx]
    mov [tw_ox], ax
    mov ax, [tpl_sy]
    mov [tw_oy], ax
    pop ax
    ret

; tpl_rects - the six buttons, in SCREEN coordinates (tw_ox/tw_oy are the
; editor's while tpl_org holds them). preserves all
tpl_rects:
    push ax
    push bx
    push cx
    push di
    mov di, tpl_rects_b
    mov bx, [tw_oy]
    add bx, TPL_RY
    mov cx, TPL_NB
.b:
    mov ax, [tw_ox]
    add ax, TPL_BX
    mov [di], ax
    add ax, TPL_BW - 1
    mov [di + 4], ax
    mov [di + 2], bx
    add bx, TPL_BH - 1
    mov [di + 6], bx
    add bx, TPL_BPY - TPL_BH + 1
    add di, 8
    loop .b
    pop di
    pop cx
    pop bx
    pop ax
    ret

; tpl_bstate - which buttons are live: SPEC.md 47's facts, never guesses
tpl_bstate:
    push ax
    push bx
    mov al, [tpl_n]
    xor bx, bx                      ; Add... - while there is room
    cmp al, TPL_MAX
    jb .a
    mov bx, OS88UI_DIS
.a:
    mov [tpl_flags_b], bx
    xor bx, bx                      ; Remove and Play - a selection
    mov ah, [tpl_sel]
    cmp ah, al
    jb .s
    mov bx, OS88UI_DIS
.s:
    mov [tpl_flags_b + 2], bx
    mov [tpl_flags_b + 10], bx
    xor bx, bx                      ; Clear - anything; Shuffle and Sort -
    or al, al                       ; something to reorder
    jnz .c
    mov bx, OS88UI_DIS
.c:
    mov [tpl_flags_b + 4], bx
    xor bx, bx
    cmp al, 2
    jae .o
    mov bx, OS88UI_DIS
.o:
    mov [tpl_flags_b + 6], bx
    mov [tpl_flags_b + 8], bx
    pop bx
    pop ax
    ret

; tpl_row - compose row DI into tw_line (TPL_RCELLS cells) and draw it if its
; hash moved: '>' beside what is playing, INVERTED for the selection (SPEC.md
; 56.4: inversion survives every reduction, a colour does not)
tpl_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, di
    mov di, tw_line
    mov word [di], '  '
    mov al, bl
    cmp al, [tpl_cur]
    jne .nm
    mov byte [di], '>'
.nm:
    add di, 2
    cmp al, [tpl_n]
    jb .ent
    or al, al                       ; the empty list says how to fill it
    jnz .pad
    cmp byte [tpl_n], 0
    jne .pad
    mov si, tpl_s_empty
    call tw_put
    jmp short .pad
.ent:
    call tpl_ent
    call tw_put
.pad:
    cmp di, tw_line + TPL_RCELLS
    jae .t
    mov byte [di], ' '
    inc di
    jmp short .pad
.t:
    mov byte [di], 0
    mov ax, bx                      ; the row's hash: its text, cheaply, and
    mov si, tw_line + 2             ; the two states it is drawn in
    mov cx, TPL_RCELLS - 2
.h:
    mov dl, [si]
    add ah, dl
    rol ax, 1
    inc si
    loop .h
    xor ah, [tw_line]
    mov dl, bl
    cmp dl, [tpl_sel]
    jne .ns
    xor ax, 5A5Ah
.ns:
    or ax, 8000h                    ; never 0 (the forgotten state)
    shl bx, 1
    cmp ax, [tpl_rh + bx]
    je .out
    mov [tpl_rh + bx], ax
    shr bx, 1
    mov ax, (CWHITE << 8) | CBLACK  ; black on white...
    cmp bl, [tpl_sel]
    jne .draw
    cmp bl, [tpl_n]
    jae .draw
    mov ax, (CBLACK << 8) | CWHITE  ; ...or inverted, selected
.draw:
    mov dx, bx
    mov cl, 3
    shl dx, cl
    add dx, TPL_RY
    mov cx, TPL_RX
    mov si, tw_line
    call tw_run
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tpl_draw - the rows and buttons that changed; [tpl_full] = all of it
; (the caller has the lock, the clip, and tpl_org)
tpl_draw:
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [tpl_full], 0
    je .rows
    mov byte [tpl_full], 0
    push ds
    pop es
    mov di, tpl_rh                  ; forget every row and button
    mov cx, TPL_MAX
    xor ax, ax
    cld
    rep stosw
    mov di, tpl_lastf
    mov cx, TPL_NB
    dec ax
    rep stosw
    mov si, CWHITE                  ; the body: the tiles no element owns
    mov bx, tpl_tiles
    mov cx, TPL_NT
.t:
    push cx
    push bx
    mov ax, [bx]
    mov cx, [bx + 2]
    mov dl, [bx + 5]
    xor dh, dh
    mov bl, [bx + 4]
    xor bh, bh
    call tw_fills
    pop bx
    pop cx
    add bx, 6
    loop .t
    mov al, CBLACK                  ; the list's frame
    call tw_pen
    mov ax, TPL_RX - 1
    mov bx, TPL_RY - 1
    mov cx, TPL_RX + TPL_RCELLS * 8
    mov dx, TPL_RY + TPL_MAX * 8
    call tw_box
.rows:
    xor di, di
.r:
    call tpl_row
    inc di
    cmp di, TPL_MAX
    jb .r
    call tpl_bstate
    xor cx, cx
.b:
    mov si, cx
    shl si, 1
    mov ax, [tpl_flags_b + si]
    cmp ax, [tpl_lastf + si]
    je .bn
    mov [tpl_lastf + si], ax
    mov ax, cx
    inc ax
    mov bx, tpl_btns
    call os88ui_btn
.bn:
    inc cx
    cmp cx, TPL_NB
    jb .b
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; tpl_paint - W_PAINT (clip armed by the kernel)
tpl_paint:
    call tpl_org
    mov byte [tpl_full], 1
    call tpl_draw
    call tpl_unorg
    ret

; tpl_refresh - what changed, from a handler: arms the clip on the editor.
; Never inside the fsx bracket: N/B walk the list from there, and the editor
; window still exists under a surface that is not the desktop's (SPEC.md 53.7)
tpl_refresh:
    call trk_endstop_upd            ; the list changed, so may the loop rule
    cmp byte [trk_fs], 0
    jne .out
    call tpl_visible
    jc .out
    push bx
    mov bx, [tpl_win]
    call OSAPI_WM_CLIP_SET
    jc .pop
    call tpl_org
    call tpl_draw
    call tpl_unorg
    call OSAPI_WM_CLIP_CLEAR
.pop:
    pop bx
.out:
    ret

; --- input --------------------------------------------------------------------
tpl_clickw:
    call tpl_org                    ; the rects where the window IS
    call tpl_unorg
    push bx
    mov bx, [tpl_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jmp os88ui_btnclick

tpl_onclick:                        ; not a button: a row. Selecting is one
    push ax                         ; click and PLAYING is a click on the row
    push bx                         ; that is already selected (SPEC.md
    push cx
    push dx                         ; 56.8: no timing, so no gesture that works
    mov bx, [tpl_win]               ; on a fast machine and not a slow one)
    push cx
    push dx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx
    pop dx
    pop cx
    sub cx, ax
    sub dx, bx
    cmp cx, TPL_RX
    jl .out
    cmp cx, TPL_RX + TPL_RCELLS * 8 - 1
    jg .out
    sub dx, TPL_RY
    jl .out
    mov ax, dx
    mov cl, 3
    shr ax, cl
    cmp al, [tpl_n]
    jae .out
    cmp al, [tpl_sel]
    jne .sel
    mov ah, al                      ; ...played as the PLAYER (45.22.3)
    mov al, TPLQ_PLAY
    call tpl_post
    jmp short .ref
.sel:
    mov [tpl_sel], al
.ref:
    call tpl_refresh
    call tw_refresh
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tpl_ondrag:
    push bx
    push cx
    push dx
    mov bx, [tpl_win]
    call OSAPI_WM_CLIP_SET
    pop dx
    pop cx
    mov bx, tpl_btns
    call os88ui_btndrag
    pop bx
    ret

tpl_onup:
    push ax
    push bx
    push cx
    push dx
    mov bx, [tpl_win]
    call OSAPI_WM_CLIP_SET
    pop dx
    pop cx
    push cx
    push dx
    mov bx, tpl_btns
    call tw_prefire                 ; the action first, the release drawn
    call os88ui_btnup               ; once in the state it leaves (tw_prefire)
    call tw_synced
    call tpl_refresh
    call tw_refresh
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tpl_acts: dw tpl_b_add, tpl_b_remove, tpl_b_clear, tpl_b_shuffle
          dw tpl_b_sort, tpl_b_play

; tpl_fire - AX = the editor's button that fired (0-based)
tpl_fire:
    push ax
    push bx
    mov bx, ax
    shl bx, 1
    call [tpl_acts + bx]
    pop bx
    pop ax
    ret

tpl_b_add:
    mov ax, TPLQ_ADD
    jmp short tpl_post
tpl_b_play:
    mov ah, [tpl_sel]
    mov al, TPLQ_PLAY
    ; (falls through)

; -----------------------------------------------------------------------------
; tpl_post - AX = a request for the PLAYER's context (45.22.3): AL = TPLQ_*,
;            AH = its argument. It is run by trk_onwake, one UI pass away.
;            One slot: a second post before the first ran replaces it, which
;            is the last thing the user asked for. preserves all
; -----------------------------------------------------------------------------
tpl_post:
    push bx
    mov [tpl_rq], ax
    mov bx, [trk_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; tpl_run - spend a posted request (trk_onwake: lock held, billed to us)
tpl_run:
    push ax
    push bx
    xor ax, ax
    xchg ax, [tpl_rq]
    cmp al, TPLQ_PLAY
    je .play
    cmp al, TPLQ_ADD
    je .add
    cmp al, TPLQ_KEY
    jne .out
    mov ax, [tpl_rkey]
    mov si, [trk_win]
    call trk_onkey
    jmp short .out
.add:
    mov byte [trk_addpl], 1         ; the completion proc reads it to know
    call trk_do_open                ; which of its callers it is answering
    jmp short .out
.play:
    mov al, ah
    mov byte [tpl_played], 0        ; a deliberate start (56.8)
    call tpl_play
.out:
    pop bx
    pop ax
    ret
tpl_b_clear:
    mov byte [tpl_n], 0
    mov byte [tpl_cur], 0FFh
    mov byte [tpl_sel], 0
    ret

; tpl_b_remove - the selection goes; what plays keeps playing, and the
; current entry's index follows the ones that moved under it
tpl_b_remove:
    push ax
    push cx
    push si
    push di
    push es
    mov al, [tpl_sel]
    cmp al, [tpl_n]
    jae .out
    cmp al, [tpl_cur]
    jne .nc
    mov byte [tpl_cur], 0FFh
    jmp short .mv
.nc:
    ja .mv                          ; (removed AFTER it: nothing moved)
    cmp byte [tpl_cur], 0FFh
    je .mv
    dec byte [tpl_cur]
.mv:
    call tpl_ent
    mov di, si
    add si, TPL_ESZ
    mov cx, tpl_ents + TPL_MAX * TPL_ESZ
    sub cx, si
    push ds
    pop es
    cld
    rep movsb
    dec byte [tpl_n]
    mov al, [tpl_sel]
    cmp al, [tpl_n]
    jb .out
    or al, al
    jz .out
    dec byte [tpl_sel]
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; tpl_swap - swap entries AL and AH, and the current index with them
tpl_swap:
    push ax
    push bx
    push cx
    push si
    push di
    call tpl_ent
    mov di, si
    xchg al, ah
    call tpl_ent
    mov cx, TPL_ESZ
.x:
    mov bl, [si]
    xchg bl, [di]
    mov [si], bl
    inc si
    inc di
    loop .x
    mov bl, [tpl_cur]
    cmp bl, al
    jne .o
    mov [tpl_cur], ah
    jmp short .out
.o:
    cmp bl, ah
    jne .out
    mov [tpl_cur], al
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; tpl_b_shuffle - Fisher-Yates over the entries
tpl_b_shuffle:
    push ax
    push bx
    push dx
    mov bl, [tpl_n]
.i:
    cmp bl, 1
    jbe .out
    call OSAPI_RAND
    xor dx, dx
    push bx
    xor bh, bh
    div bx                          ; DL = 0 .. i-1
    pop bx
    mov al, dl
    mov ah, bl
    dec ah
    call tpl_swap
    dec bl
    jmp short .i
.out:
    pop dx
    pop bx
    pop ax
    ret

; tpl_b_sort - by name, an insertion sort of sixteen at most
tpl_b_sort:
    push ax
    push cx
    push si
    push di
    push es
    push ds                         ; cmpsb reads ES:DI, and a callback is
    pop es                          ; entered with ES = KERNEL_SEG
    mov ah, 1
.i:
    cmp ah, [tpl_n]
    jae .out
    mov al, ah
.j:
    or al, al
    jz .ni
    push ax
    call tpl_ent                    ; SI = entry j
    mov di, si
    dec al
    call tpl_ent                    ; SI = entry j - 1
    mov cx, 13
    push si
    push di
    cld
    xchg si, di                     ; is j-1 > j?
    repe cmpsb
    pop di
    pop si
    pop ax
    jae .ni                         ; in order (or equal): this one is placed
    push ax
    mov ah, al
    dec al
    call tpl_swap
    pop ax
    dec al
    jmp short .j
.ni:
    inc ah
    jmp short .i
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; tpl_onkey - the editor's keys; everything else is the player's
tpl_onkey:
    push ax
    push bx
    mov bx, ax
    or bl, bl
    jnz .asc
    cmp bh, 48h
    je .up
    cmp bh, 50h
    je .dn
    cmp bh, 53h                     ; Del
    je .del
    jmp short .pass
.asc:
    cmp bl, 27
    je .hide
    cmp bl, 13
    je .play
.pass:
    mov [tpl_rkey], ax              ; the player's keys, run as the player
    mov ax, TPLQ_KEY                ; (45.22.3)
    call tpl_post
    jmp short .out
.up:
    cmp byte [tpl_sel], 0
    je .ref
    dec byte [tpl_sel]
    jmp short .ref
.dn:
    mov al, [tpl_sel]
    inc al
    cmp al, [tpl_n]
    jae .ref
    mov [tpl_sel], al
    jmp short .ref
.del:
    call tpl_b_remove
    jmp short .ref
.play:
    call tpl_b_play
    jmp short .ref
.hide:
    call tpl_toggle
.ref:
    call tpl_refresh
    call tw_refresh
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================
tpl_tpl:
    dw 0, 0, TPL_W + 2, TPL_H + 20
    dw tpl_ttl, tpl_paint, tpl_onkey, 0
    OS88UI_BTNREC tpl_btns, tpl_rects_b, tpl_labels, tpl_flags_b, TPL_NB
tpl_labels: dw tpl_s_add, tpl_s_rem, tpl_s_clr, tpl_s_shuf, tpl_s_sort
            dw tpl_s_play
tpl_ttl:     db 'PlayList', 0
tpl_s_add:   db 'Add...', 0
tpl_s_rem:   db 'Remove', 0
tpl_s_clr:   db 'Clear', 0
tpl_s_shuf:  db 'Shuffle', 0
tpl_s_sort:  db 'Sort', 0
tpl_s_play:  db 'Play', 0
tpl_s_empty: db 'Empty - Add...', 0
tpl_s_full:  db 'PlayList is full', 0
tpl_s_added: db 'Added to the PlayList', 0

; the editor's body tiles - generated with the face's, by tools/trkface.py
tpl_tiles:                 ; 22 tiles
    dw 0, 255
    db 0, 2
    dw 169, 255
    db 3, 3
    dw 169, 175
    db 4, 19
    dw 248, 255
    db 4, 19
    dw 169, 255
    db 20, 23
    dw 169, 175
    db 24, 39
    dw 248, 255
    db 24, 39
    dw 169, 255
    db 40, 43
    dw 169, 175
    db 44, 59
    dw 248, 255
    db 44, 59
    dw 169, 255
    db 60, 63
    dw 169, 175
    db 64, 79
    dw 248, 255
    db 64, 79
    dw 169, 255
    db 80, 83
    dw 169, 175
    db 84, 99
    dw 248, 255
    db 84, 99
    dw 169, 255
    db 100, 103
    dw 169, 175
    db 104, 119
    dw 248, 255
    db 104, 119
    dw 0, 6
    db 3, 132
    dw 169, 255
    db 120, 132
    dw 0, 255
    db 133, 135
TPL_NT equ 22

; =============================================================================
; bss
; =============================================================================
    TRKB tpl_n                      ; entries
    TRKB tpl_cur                    ; the one playing, 0FFh = none (the entry
                                    ; proc sets it: bss arrives zeroed)
    TRKB tpl_sel                    ; the editor's selection
    TRKB tpl_played                 ; songs since a deliberate start (56.8)
    TRKB tpl_inplay                 ; trk_fdone is loading a LIST entry
    TRKW tpl_rq                     ; a posted request: TPLQ_*, and its byte
    TRKW tpl_rkey                   ; ...a forwarded key's AX
    TRKBUF tpl_ents, TPL_MAX * TPL_ESZ
    TRKBUF tpl_new, TPL_ESZ         ; tpl_here's entry, for a push or a compare
    TRKBUF tpl_loaded, TPL_ESZ      ; ...and the one whose module is loaded
    TRKW tpl_win                    ; the editor, 0 = not made yet
    TRKW tpl_sx                     ; the player's origin, banked by tpl_org
    TRKW tpl_sy
    TRKB tpl_full                   ; the next tpl_draw is the whole window
    TRKBUF tpl_rects_b, TPL_NB * 8
    TRKBUF tpl_flags_b, TPL_NB * 2
    TRKBUF tpl_lastf, TPL_NB * 2
    TRKBUF tpl_rh, TPL_MAX * 2      ; each row's hash as drawn
