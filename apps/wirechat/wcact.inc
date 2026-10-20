; =============================================================================
; os8088 - apps/wirechat/wcact.inc
;
; WHAT HAPPENS, AND WHAT HAPPENS NEXT (SPEC.md 113.4, 113.6): the wake
; handler, the timer, keys, clicks, menus, popups - and the CONDUCTOR,
; wc_next, which every one of them ends by asking "is there a request to
; start?". It is the only caller of wc_start.
;
; THE CONDUCTOR'S ORDER: nothing while a request is in flight or a wait is
; owed; a JOIN when there is a nick and no session; then a queued FLAG, a
; queued SAY, and otherwise a POLL. A message or a flag queued while a poll is
; held does not wait 50 seconds for it: wc_kick abandons the poll (bump, close
; - SPEC.md 71.11) and the conductor starts the message on the same pass.
; =============================================================================

; -----------------------------------------------------------------------------
; wc_next - start the next request, if one is owed
; in:  the UI task, with or without the lock (it draws nothing)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_next:
    cmp byte [wc_rq], RQ_NONE
    jne .out
    cmp word [wc_wait], 0
    jne .out
    cmp byte [wc_link], LK_NONICK
    je .out
    cmp byte [wc_link], LK_KEY
    je .out
    cmp byte [wc_nick], 0
    je .out
    mov al, RQ_JOIN
    cmp byte [wc_tok], 0
    je .go
    mov al, RQ_FLAG
    mov dx, [wc_qflag]
    or dx, [wc_qflag+2]
    jnz .go
    mov al, RQ_SAY
    cmp byte [wc_qsay], 0
    jne .go
    mov al, RQ_POLL
.go:
    mov [wc_rq], al
    mov word [wc_age], 0
    cmp al, RQ_POLL                     ; a request we MADE lights activity's
    je .start                           ; lamp (wc_lamps); a poll is waiting
    or word [wc_dirty], WCD_STAT
.start:
    call wc_compose
    call wc_start
.out:
    ret

; --- wc_kick - something the user did wants the wire NOW ---------------------
; A held poll is abandoned for it; a message, a flag or a join in flight is
; left to finish, and the conductor carries the change on the next request.
wc_kick:
    cmp byte [wc_rq], RQ_POLL
    jne .idle
    call wc_abandon
    mov byte [wc_rq], RQ_NONE
.idle:
    cmp byte [wc_rq], RQ_NONE
    jne .out
    cmp byte [wc_link], LK_LIVE         ; a wait the server asked for stands;
    jne .out                            ; a back-off after a failure does too
    mov word [wc_wait], 0
    jmp wc_next
.out:
    ret

; =============================================================================
; wc_onwake - OSAPI_WM_ONWAKE (SPEC.md 74.1): the UI task, the lock NOT held
; in:  SI = our window
;
; Three reasons to be here: the first, self-kicked wake (read the file); a
; save owed (write the file - a callback may not, so it asks for a wake); and
; the worker's one byte.
; =============================================================================
wc_onwake:
    cmp byte [wc_cfgld], 0
    jne .saved
    mov byte [wc_cfgld], 1
    call wc_cfgload
    call wc_firststate
.saved:
    cmp byte [wc_save], 0
    je .wake
    mov byte [wc_save], 0
    call wc_cfgsave
.wake:
    mov al, [wc_wake]                   ; read the byte, then clear it: the
    mov byte [wc_wake], WW_NONE         ; worker wrote it LAST
    cmp al, WW_NONE
    je .next
    push ax
    mov bx, [wc_win]                    ; are we in front? (for the divider and
    call OSAPI_WM_TOP                   ; the chirp, SPEC.md 113.6.5)
    xor al, al
    cmp bx, [wc_win]
    jne .f
    inc ax
.f:
    mov [wc_front], al
    pop ax
    cmp al, WW_DONE
    jne .fail
    call wc_ondone
    jmp short .next
.fail:
    call wc_onfail
.next:
    call wc_next
    ; --- and paint what changed, under a lock of our own ---------------------
    cmp word [wc_dirty], 0
    je .out
    call OSAPI_GFX_LOCK
    push es
    mov ax, KERNEL_SEG                  ; the window record is the kernel's,
    mov es, ax                          ; and the file cells above moved ES
    mov bx, [wc_win]
    test byte [es:bx+W_FLAGS], 2        ; still visible?
    pop es
    jz .unlock
    call OSAPI_WM_CLIP_SET
    jc .unlock
    call wc_flush
    mov bx, [wc_win]
    call OSAPI_WM_GROW                  ; a self-initiated repaint ends with it
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    ret

; --- wc_firststate - the file is read: a nick to join with, or ask for one --
wc_firststate:
    mov word [wc_dirty], WCD_ALL
    cmp byte [wc_nick], 0
    je .ask
    mov byte [wc_link], LK_JOIN
    mov si, wc_s_connecting
    jmp wc_setnote
.ask:
    mov byte [wc_link], LK_NONICK
    mov al, MD_NICK
    call wc_setmode
    mov si, wc_s_picknick
    jmp wc_setnote

; -----------------------------------------------------------------------------
; wc_ondone - the worker read a reply: what it says, and what follows
; -----------------------------------------------------------------------------
wc_ondone:
    mov al, RQ_NONE
    xchg al, [wc_rq]
    mov [wc_rqdone], al
    call wc_reply                       ; the stores, [wc_ercode], [wc_erwait]
    test word [wc_dirty], WCD_PANE | WCD_TAIL | WCD_LEFT | WCD_RIGHT | WCD_NOTE
    jz .quiet
    mov byte [wc_act], 2                ; it brought something: the lamp, for a
.quiet:                                 ; second or two (wc_ontimer)
    or word [wc_dirty], WCD_HDR | WCD_STAT
    mov ax, [wc_ercode]
    cmp ax, 0xFFFF
    je wc_onfail                        ; not WireChat's: a failure like any
    or ax, ax
    jnz .er
    ; --- OK --------------------------------------------------------------------
    mov byte [wc_link], LK_LIVE
    mov word [wc_back], 0
    mov ax, [wc_erwait]
    mov [wc_wait], ax
    mov al, [wc_rqdone]
    cmp al, RQ_SAY
    jne .nsay
    mov byte [wc_qsay], 0
.nsay:
    cmp al, RQ_FLAG
    jne .nflag
    xor ax, ax
    mov [wc_qflag], ax
    mov [wc_qflag+2], ax
.nflag:
    cmp byte [wc_rqdone], RQ_JOIN
    jne .out
    cmp byte [wc_rqnote], 0             ; the server said something of its own
    jne .out                            ; (a mute, say): that stands
    mov si, wc_s_joined
    jmp wc_setnote
.er:
    cmp ax, 401                         ; the session is gone: join again,
    jne .n401                           ; quietly, with the same nick and
    mov byte [wc_tok], 0                ; whatever was queued
    mov byte [wc_link], LK_JOIN
    mov word [wc_wait], 0
    mov byte [wc_hist], 1
    ret
.n401:
    cmp ax, 503
    jne .n503
    mov byte [wc_link], LK_REST         ; resting, or full: wait as asked, at
    mov ax, [wc_erwait]                 ; most an hour, at least half a minute
    cmp ax, 3600
    jbe .w1
    mov ax, 3600
.w1:
    cmp ax, 30
    jae .w2
    mov ax, 30
.w2:
    mov [wc_wait], ax
    ret
.n503:
    mov dl, [wc_rqdone]
    cmp dl, RQ_JOIN
    jne .njoin
    ; --- a join refused: the nick is the problem (400, 409, 422), or it is
    ; owned and the code is missing or wrong (428) --------------------------
    mov byte [wc_link], LK_NONICK
    cmp ax, 428
    jne .nkey
    mov byte [wc_link], LK_KEY
    mov byte [wc_key], 0                ; a wrong code is no use kept
    mov al, MD_KEY
    call wc_setmode
    mov di, wc_empty
    call wc_fieldset
    ret
.nkey:
    mov al, MD_NICK
    call wc_setmode
    mov di, wc_nick                     ; ...so it is offered back to edit
    call wc_fieldset
    ret
.njoin:
    mov bx, [wc_erwait]
    mov [wc_wait], bx
    cmp dl, RQ_FLAG
    jne .nfl
    xor ax, ax
    mov [wc_qflag], ax
    mov [wc_qflag+2], ax
    ret
.nfl:
    cmp dl, RQ_SAY
    jne .out
    mov byte [wc_qsay], 0               ; a refused message goes back in the
    cmp word [wc_ln+LN_LEN], 0          ; field to be edited - unless the user
    jne .out                            ; has started another
    mov di, wc_qtext
    jmp wc_fieldset
.out:
    ret

; -----------------------------------------------------------------------------
; wc_onfail - the request failed below the protocol: no driver, no answer
; -----------------------------------------------------------------------------
wc_onfail:
    mov byte [wc_rq], RQ_NONE
    or word [wc_dirty], WCD_HDR | WCD_STAT
    cmp byte [wc_nodrv], 0
    je .net
    mov byte [wc_link], LK_NODRV        ; SPEC.md 113.9: say so, and look again
    mov word [wc_wait], 60              ; in a minute (a driver may be loaded
    mov si, wc_s_nodrv                  ; from the Control Panel meanwhile)
    jmp wc_setnote
.net:
    mov byte [wc_link], LK_OFF
    mov ax, [wc_back]                   ; 5, 10, 20, 40, 60 (SPEC.md 113.4)
    shl ax, 1
    jnz .b
    mov ax, 5
.b:
    cmp ax, 60
    jbe .c
    mov ax, 60
.c:
    mov [wc_back], ax
    mov [wc_wait], ax
    ret

; =============================================================================
; wc_ontimer - W_ONTIMER, once a second (SPEC.md 13.9): the waits, a request
;              that has taken too long, and auto-away
; in:  SI = our window; the lock held, the clip NOT armed
; =============================================================================
wc_ontimer:
    mov bx, si
    mov ax, 18
    call OSAPI_WM_TIMER                 ; one-shot: re-armed first
    cmp byte [wc_act], 0                ; activity's lamp goes out
    je .wait
    dec byte [wc_act]
    jnz .wait
    or word [wc_dirty], WCD_STAT
.wait:
    ; --- a wait owed ---------------------------------------------------------
    cmp word [wc_wait], 0
    je .age
    dec word [wc_wait]
    cmp byte [wc_link], LK_LIVE         ; the bar counts a back-off down
    je .w0
    or word [wc_dirty], WCD_STAT
.w0:
    cmp word [wc_wait], 0
    jne .age
    call wc_next
    ; --- the request in flight, against its deadline -------------------------
.age:
    cmp byte [wc_rq], RQ_NONE
    je .idle
    inc word [wc_age]
    cmp word [wc_age], WC_HOLD
    jb .idle
    call wc_abandon                     ; nothing came back: give it up and
    mov byte [wc_nodrv], 0              ; treat it as the failure it is
    call wc_onfail
    ; --- auto-away (SPEC.md 113.6.5) -----------------------------------------
.idle:
    cmp word [wc_idle], 0xFFFF
    je .draw
    inc word [wc_idle]
    cmp word [wc_idle], WC_AWAYS
    jb .draw
    cmp byte [wc_st], ST_ONLINE
    jne .draw
    cmp byte [wc_link], LK_LIVE
    jne .draw
    mov byte [wc_st], ST_AWAY
    mov byte [wc_auto], 1
    or word [wc_dirty], WCD_HDR
    call wc_kick
.draw:
    jmp wc_cbflush

; --- wc_activity - a key or a press: not idle, and back from auto-away ------
wc_activity:
    mov word [wc_idle], 0
    cmp byte [wc_auto], 0
    je .out
    mov byte [wc_auto], 0
    cmp byte [wc_st], ST_AWAY
    jne .out
    mov byte [wc_st], ST_ONLINE
    or word [wc_dirty], WCD_HDR
    jmp wc_kick
.out:
    ret

; --- wc_cbflush - paint what is dirty, from a callback (lock held, no clip) -
wc_cbflush:
    cmp word [wc_dirty], 0
    je .out
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wc_flush
    mov bx, [wc_win]
    call OSAPI_WM_GROW
.out:
    ret

; --- wc_repaint - erase the content and paint it all, from a callback --------
; The About card and a popup are drawn OVER the regions, so taking either
; down is this: the content white and every region again.
wc_repaint:
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wc_geom
    jc .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wc_ox]
    mov bx, [wc_oy]
    mov cx, ax
    add cx, [wc_cw]
    dec cx
    mov dx, bx
    add dx, [wc_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [wc_win]
    call wc_paint
    mov bx, [wc_win]
    call OSAPI_WM_GROW
.out:
    ret

; =============================================================================
; THE VIEW
; =============================================================================

; -----------------------------------------------------------------------------
; wc_setview - the view becomes room AL, or (AL = WV_DM) the conversation
;              with the nick at SI
; Its messages are fetched again whole (h=1), so the store is emptied now and
; the pane says so at once rather than showing the last room's for a request.
; -----------------------------------------------------------------------------
wc_setview:
    cmp al, WV_DM
    jne .room
    push ax
    mov di, wc_vdm
    mov bx, si
    mov cx, WC_NICKN
    call wc_ncopy
    mov bx, wc_vdm                      ; its conversation is read now
    call wc_dmconv
    mov byte [di+WC_DUNR], 0
    pop ax
.room:
    mov [wc_view], al
    cmp al, WC_NROOM                    ; a room looked at is a mention read
    jae .nm
    xor ah, ah
    mov di, ax
    mov byte [wc_ment+di], 0
.nm:
    inc byte [wc_vgen]
    mov byte [wc_hist], 1
    call wc_mclear
    call wc_markseen
    mov byte [wc_note], 0
    mov word [wc_dirty], WCD_ALL & ~WCD_TAIL
    jmp wc_kick

; --- wc_scrollby - AX = lines, signed: move the pane's top ------------------
wc_scrollby:
    add ax, [wc_top]
    jge .lo
    xor ax, ax
.lo:
    push ax
    call wc_maxtop
    mov dx, ax
    pop ax
    cmp ax, dx
    jle .hi
    mov ax, dx
.hi:
    cmp ax, [wc_top]
    je .out
    mov [wc_top], ax
    or word [wc_dirty], WCD_PANE
.out:
    ret

; --- wc_onwheel - W_ONWH (SPEC.md 13.18): AX = signed notches, SI = window;
; W_ONKEY's environment. WHEEL_STEPS lines a notch through wc_scrollby, the
; arrows' path, and one flush; the About card or a popup comes down first and
; spends the turn, as it does a key's
wc_onwheel:
    push ax
    call wc_activity
    pop ax
    cmp byte [wc_abon], 0
    je .nab
    mov byte [wc_abon], 0
    jmp wc_repaint
.nab:
    cmp byte [wc_pop], PP_NONE
    je .npop
    call wc_popdown
    jmp wc_repaint
.npop:
    mov dx, WHEEL_STEPS
    imul dx                             ; AX = lines, signed
    call wc_scrollby
    jmp wc_cbflush

; =============================================================================
; THE FIELD AND ITS MODES (SPEC.md 113.6.1)
; =============================================================================

; --- wc_setmode - AL = MD_*: the prompt, and a draft set aside -------------
; Leaving MESSAGE mode keeps what was being typed in wc_draft, and coming back
; puts it back: a search must not cost the reply half written.
wc_setmode:
    cmp al, [wc_mode]
    je .out
    push ax
    cmp byte [wc_mode], MD_MSG
    jne .from
    mov si, wc_lbuf                     ; MESSAGE -> anything: bank the draft
    mov di, wc_draft
    call wc_strcpy
.from:
    pop ax
    mov [wc_mode], al
    mov di, wc_empty
    cmp al, MD_MSG
    jne .set
    mov di, wc_draft                    ; anything -> MESSAGE: the draft back
.set:
    call wc_fieldset
    mov byte [wc_find], 0xFF
    or word [wc_dirty], WCD_FIELD | WCD_NOTE
.out:
    ret

; --- wc_fieldset - the field holds the NUL string at DI, the caret at its end
wc_fieldset:
    push si
    mov si, di
    mov di, wc_lbuf
    call wc_strcpy
    mov si, wc_ln
    call os88line_resync
    pop si
    or word [wc_dirty], WCD_FIELD
    ret

; --- wc_strcpy - SI -> DI, at most WC_TEXTN-1 characters. clobbers AL, CX --
wc_strcpy:
    push di
    mov cx, WC_TEXTN - 1
.c:
    lodsb
    or al, al
    jz .end
    mov [di], al
    inc di
    loop .c
.end:
    mov byte [di], 0
    pop di
    ret

; -----------------------------------------------------------------------------
; wc_enter - Enter in the field: what it means depends on the mode
; -----------------------------------------------------------------------------
wc_enter:
    mov al, [wc_mode]
    cmp al, MD_NICK
    je wc_enick
    cmp al, MD_FIND
    je wc_efind
    cmp al, MD_FRIEND
    je wc_efriend
    cmp al, MD_KEY
    je wc_ekey
    ; --- send ---------------------------------------------------------------
wc_send:
    cmp word [wc_ln+LN_LEN], 0
    je .out
    cmp byte [wc_qsay], 0               ; one in the queue at a time: it is
    je .q                               ; short, and the server's limit is
    mov si, wc_s_sending                ; one per eight seconds anyway
    jmp wc_setnote
.q:
    mov si, wc_lbuf
    mov di, wc_qtext
    call wc_strcpy
    mov byte [wc_qsay], 1
    mov di, wc_empty
    call wc_fieldset
    mov byte [wc_note], 0
    or word [wc_dirty], WCD_NOTE
    cmp byte [wc_divi], 0xFF            ; sending is having read: the divider
    je .k                               ; goes
    mov byte [wc_divi], 0xFF
    mov byte [wc_divn], 0
    or word [wc_dirty], WCD_PANE
.k:
    call wc_buttons_send
    jmp wc_kick
.out:
    ret

; --- wc_enick - the nick is the field: check it, keep it, join with it -----
wc_enick:
    mov si, wc_lbuf
    call wc_nickok                      ; CF = 1 not a nick
    jnc .ok
    mov si, wc_s_badnick
    jmp wc_setnote
.ok:
    mov si, wc_lbuf                     ; another nick: the code was the old
    mov di, wc_nick                     ; one's
    call wc_streqci
    je .same
    mov byte [wc_key], 0
.same:
    mov si, wc_lbuf
    mov di, wc_nick
    mov cx, WC_NICKN
    mov bx, si
    call wc_ncopy
wc_rejoin:
    mov byte [wc_tok], 0                ; a new session for a new nick; the
    call wc_abandon                     ; old one expires on the server
    mov byte [wc_rq], RQ_NONE
    mov byte [wc_link], LK_JOIN
    mov word [wc_wait], 0
    mov byte [wc_hist], 1
    mov al, MD_MSG
    call wc_setmode
    mov si, wc_s_connecting
    call wc_setnote
    call wc_wantsave
    jmp wc_next

; --- wc_ekey - the code for an owned nick is the field: keep it, join ------
wc_ekey:
    cmp word [wc_ln+LN_LEN], 0
    je .out
    mov bx, wc_lbuf
    mov di, wc_key
    mov cx, WC_KEYN
    call wc_ncopy
    jmp wc_rejoin
.out:
    ret

; --- wc_nickok - CF = 0 when the NUL string at SI is a nick (SPEC.md 113.2.2)
; 2..12 of [A-Za-z0-9_-], beginning with a letter. clobbers AX, CX, SI
wc_nickok:
    mov al, [si]
    call wc_lower
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
    xor cx, cx
.c:
    lodsb
    or al, al
    jz .end
    call wc_nickch
    jc .no
    inc cx
    jmp short .c
.end:
    cmp cx, 2
    jb .no
    cmp cx, WC_NICKN - 1
    ja .no
    clc
    ret
.no:
    stc
    ret

; --- wc_efind - the next older message containing the field, case-blind ----
wc_efind:
    cmp word [wc_ln+LN_LEN], 0
    je .out
    mov bl, [wc_find]                   ; from the one before the last hit, or
    cmp bl, 0xFF                        ; the newest
    jne .from
    mov bl, [wc_mn]
.from:
    xor bh, bh
.prev:
    or bx, bx
    jz .none
    dec bx
    push bx
    call wc_mrec
    lea si, [bx+WC_MTEXT]
    call wc_hasci                       ; CF = 0: the field is in it
    jnc .hit
    lea si, [bx+WC_MNICK]
    call wc_hasci
    pop bx
    jc .prev
    push bx
.hit:
    pop bx
    mov [wc_find], bl
    mov [wc_sel], bl
    ; the hit's first line at the pane's top, as far as the end allows
    xor ax, ax
    xor cx, cx
.sum:
    cmp cx, bx
    jae .top
    push ax
    push bx
    push cx
    mov bx, cx
    call wc_mtot
    mov dx, ax
    pop cx
    pop bx
    pop ax
    add ax, dx
    inc cx
    jmp short .sum
.top:
    sub ax, [wc_top]
    call wc_scrollby
    or word [wc_dirty], WCD_PANE
    mov byte [wc_note], 0
    or word [wc_dirty], WCD_NOTE
    ret
.none:
    mov byte [wc_find], 0xFF
    mov si, wc_s_notfound
    jmp wc_setnote
.out:
    ret

; --- wc_hasci - CF = 0 when the field's text occurs in the NUL string at SI,
; case-blind. clobbers AX, SI
wc_hasci:
    push di
.at:
    cmp byte [si], 0
    je .no
    push si
    mov di, wc_lbuf
.c:
    mov al, [di]
    or al, al
    jz .yes
    mov ah, [si]
    or ah, ah
    jz .miss
    call wc_lower
    xchg al, ah
    call wc_lower
    cmp al, ah
    jne .miss
    inc si
    inc di
    jmp short .c
.miss:
    pop si
    inc si
    jmp short .at
.yes:
    pop si
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; --- wc_efriend - the field is a nick to add to the friends ------------------
wc_efriend:
    mov si, wc_lbuf
    call wc_nickok
    jnc .ok
    mov si, wc_s_badnick
    jmp wc_setnote
.ok:
    mov si, wc_lbuf
    call wc_frfind                      ; already one?
    jnc .done
    cmp byte [wc_fn], WC_NFR
    jae .done
    mov al, [wc_fn]
    call wc_frrec                       ; SI = the free record
    mov di, si
    mov bx, wc_lbuf
    mov cx, WC_NICKN
    call wc_ncopy
    inc byte [wc_fn]
    call wc_wantsave
    or word [wc_dirty], WCD_LEFT
.done:
    mov al, MD_MSG
    call wc_setmode
    ret

; --- wc_frfind - the nick at SI a friend? CF = 0 and AL = which. clobbers AX
wc_frfind:
    push si
    push di
    mov di, si
    xor ax, ax
.f:
    cmp al, [wc_fn]
    jae .no
    call wc_frrec                       ; SI = friend AL
    call wc_streqci
    je .yes
    inc ax
    jmp short .f
.no:
    pop di
    pop si
    stc
    ret
.yes:
    pop di
    pop si
    clc
    ret

; --- wc_frdel - forget friend AL ------------------------------------------------
wc_frdel:
    push ax
    call wc_frrec
    mov di, si
    add si, WC_NICKN
    mov cl, [wc_fn]
    sub cl, al
    dec cl
    mov al, WC_NICKN
    mul cl
    mov cx, ax
    push es
    push ds
    pop es
    rep movsb
    pop es
    dec byte [wc_fn]
    pop ax
    call wc_wantsave
    or word [wc_dirty], WCD_LEFT
    ret

; --- wc_wantsave - WIRECHAT.CFG is owed a write: the wake handler does it ----
wc_wantsave:
    mov byte [wc_save], 1
    push bx
    mov bx, [wc_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; =============================================================================
; KEYS (SPEC.md 113.6.3) - W_ONKEY: AL = ascii, AH = scan, SI = window;
; the lock held, the clip NOT armed
; =============================================================================
wc_onkey:
    push ax
    call wc_activity
    pop ax
    cmp byte [wc_abon], 0               ; a key takes the About card down
    je .nab
    mov byte [wc_abon], 0
    jmp wc_repaint
.nab:
    cmp byte [wc_pop], PP_NONE          ; ...and a popup
    je .npop
    call wc_popdown
    jmp wc_repaint
.npop:
    cmp al, 9                           ; Tab: an @nick, or the next room
    je .tab
    mov byte [wc_cpl], 0                ; any other key ends a completion run
    cmp ah, 0x49                        ; PgUp
    je .pgup
    cmp ah, 0x51                        ; PgDn
    je .pgdn
    cmp ah, KSC_UP
    je .up
    cmp ah, KSC_DOWN
    je .down
    ; --- the field's (the deskcal.asm idiom) ---------------------------------
    mov si, wc_ln
    push word [si+LN_VIEW]
    push word [si+LN_LEN]
    push word [si+LN_CAR]
    call os88line_key
    pop dx
    pop bx
    pop cx
    jc .mine
    push bx
    mov ax, cx
    push ax
    push dx
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    pop dx
    pop ax
    pop bx
    jc .out
    mov si, wc_ln
    call os88line_edit
    ; the Send button greys and ungreys with the field's emptiness
    cmp bx, 0
    je .grey
    cmp word [wc_ln+LN_LEN], 0
    jne .out
.grey:
    call wc_buttons_send
.out:
    ret
.mine:
    cmp al, 13
    je .enter
    cmp al, 27
    jne .out
    cmp byte [wc_mode], MD_NICK         ; Esc: back to messages, or empty the
    je .out                             ; field - and nick mode has no back
    cmp byte [wc_mode], MD_KEY          ; ...but code mode goes back to it,
    jne .nkey                           ; for another nick
    mov byte [wc_link], LK_NONICK
    mov al, MD_NICK
    call wc_setmode
    mov di, wc_nick
    call wc_fieldset
    mov si, wc_s_picknick
    call wc_setnote
    call wc_geom
    jmp wc_cbflush
.nkey:
    cmp byte [wc_mode], MD_MSG
    je .clear
    mov al, MD_MSG
    call wc_setmode
    jmp wc_cbflush
.clear:
    mov di, wc_empty
    call wc_fieldset
    call wc_buttons_send
    jmp wc_cbflush
.enter:
    call wc_enter
    jmp wc_cbflush
.pgup:
    mov ax, [wc_rows]
    neg ax
    inc ax
    jmp short .scroll
.pgdn:
    mov ax, [wc_rows]
    dec ax
    jmp short .scroll
.up:
    mov ax, -1
    jmp short .scroll
.down:
    mov ax, 1
.scroll:
    call wc_scrollby
    jmp wc_cbflush
.tab:
    call wc_complete                    ; CF = 0: it was an @nick, completed
    jnc .out
    cmp byte [wc_view], WV_DM
    mov al, 0
    je .t
    mov al, [wc_view]
    inc al
    cmp al, WC_NROOM
    jb .t
    xor al, al
.t:
    call wc_setview
    jmp wc_cbflush

; -----------------------------------------------------------------------------
; wc_complete - Tab right after '@' and part of a nick: the nick, off the
;               roster (SPEC.md 113.6.3)
; in:  the lock held, no clip. out: CF = 0 the field was completed and drawn;
;      CF = 1 the caret is not after an @ and a nick's characters, or nothing
;      on the roster begins that way - Tab then does what it always did
;
; Tab AGAIN goes round the others that begin the same way: [wc_cpl] keeps the
; length of the prefix the user typed and [wc_cpi] the roster record the last
; Tab took, and any other key clears the first. Never ourselves: a mention of
; our own nick notifies nobody.
; -----------------------------------------------------------------------------
wc_complete:
    cmp byte [wc_mode], MD_MSG
    jne .no
    mov di, wc_lbuf
    add di, [wc_ln+LN_CAR]              ; DI = the caret
    mov si, di
.back:
    cmp si, wc_lbuf                     ; back over nick characters to the @
    je .no
    mov al, [si-1]
    cmp al, '@'
    je .at
    call wc_nickch
    jc .no
    dec si
    jmp short .back
.at:
    mov cx, di
    sub cx, si                          ; CX = what is typed after the @
    mov [wc_cpart], cx
    xor ax, ax
    mov al, [wc_cpl]
    or al, al
    jz .fresh
    cmp ax, cx                          ; a run: the prefix is the first CPL
    ja .fresh                           ; characters, the search after the last
    mov cx, ax
    mov dl, [wc_cpi]
    inc dl
    jmp short .find
.fresh:
    or cx, cx                           ; '@' alone is not a start
    jz .no
    xor dl, dl
.find:
    ; DL = the record to try first, CX = the prefix's length; at most a whole
    ; lap of the roster
    xor dh, dh
.try:
    cmp dh, [wc_rn]
    jae .no
    cmp dl, [wc_rn]
    jb .in
    xor dl, dl
.in:
    push cx
    push si
    mov al, dl
    mov bl, WC_RREC
    mul bl
    add ax, wc_ros
    mov bx, ax                          ; BX = the record's nick
    call wc_isme
    je .skip
    mov di, bx
.cmp:
    jcxz .hit
    mov al, [si]
    call wc_lower
    mov ah, al
    mov al, [di]
    call wc_lower
    cmp al, ah
    jne .skip
    inc si
    inc di
    dec cx
    jmp short .cmp
.skip:
    pop si
    pop cx
    inc dl
    inc dh
    jmp short .try
.hit:
    pop si
    pop cx
    mov [wc_cpl], cl                    ; the run's prefix, and where it is
    mov [wc_cpi], dl
    ; --- the typed part out, the nick in -------------------------------------
    push bx
    mov cx, [wc_cpart]
    jcxz .ins
.bs:
    push cx
    mov ax, 8
    mov si, wc_ln
    call os88line_key
    pop cx
    loop .bs
.ins:
    pop bx
.ch:
    mov al, [bx]
    or al, al
    jz .draw
    push bx
    xor ah, ah
    mov si, wc_ln
    call os88line_key
    pop bx
    inc bx
    jmp short .ch
.draw:
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .done
    mov si, wc_ln
    call os88line_draw
    call wc_buttons_send
.done:
    clc
    ret
.no:
    stc
    ret

; --- wc_buttons_send - Send again, its grey from the predicate (SPEC.md 47) -
wc_buttons_send:
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wc_bflags_set
    mov al, WB_SEND + 1
    call wc_btn
.out:
    ret

; =============================================================================
; PRESSES (SPEC.md 113.6.2) - the library's chain for a press that was not a
; button's: CX, DX = the point (SCREEN), SI = the window; the lock held, the
; clip NOT armed. out: CF = 1, the press is spent.
; =============================================================================
wc_onclick:
    call wc_activity
    cmp byte [wc_abon], 0
    je .nab
    mov byte [wc_abon], 0
    call wc_repaint
    stc
    ret
.nab:
    cmp byte [wc_pop], PP_NONE
    je .npop
    call wc_pophit                      ; a pick, or a dismissal
    stc
    ret
.npop:
    call wc_geom
    jc .spent
    ; --- the field ---------------------------------------------------------
    mov si, wc_ln
    call os88line_click
    jc .nf
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .spent
    mov si, wc_ln
    call os88line_draw
    jmp short .spent
.nf:
    ; --- the scroll bar ------------------------------------------------------
    mov bx, wc_sb
    call os88ui_sbhit
    cmp al, OS88UI_SBNONE
    je .nsb
    mov bx, -1
    cmp al, OS88UI_SBUP
    je .sbgo
    mov bx, 1
    cmp al, OS88UI_SBDOWN
    je .sbgo
    mov bx, [wc_rows]
    dec bx
    cmp al, OS88UI_SBPGDN
    je .sbgo
    neg bx
    cmp al, OS88UI_SBPGUP
    jne .spent
.sbgo:
    mov ax, bx
    call wc_scrollby
    jmp short .flush
.nsb:
    call wc_where                       ; AL = WH_*, AH/BX = which
    cmp al, WH_PANE
    je .pane
    cmp al, WH_LEFT
    je .left
    cmp al, WH_RIGHT
    je .right
    cmp al, WH_NOTIFY
    jne .spent
    xor byte [wc_notify], 1
    call wc_menulabels
    call wc_wantsave
    or word [wc_dirty], WCD_STAT
    jmp short .flush
.pane:
    cmp [wc_sel], ah                    ; a press selects; a press on the
    jne .sel                            ; selected one deselects it
    mov ah, 0xFF
.sel:
    mov [wc_sel], ah
    or word [wc_dirty], WCD_PANE
    jmp short .flush
.left:
    call wc_leftpick
    jmp short .flush
.right:
    call wc_rightnick                   ; SI = the member, CF none
    jc .spent
    call wc_opendm
.flush:
    call wc_cbflush
.spent:
    stc
    ret

; --- wc_opendm - the conversation with the nick at SI, unless it is ours ----
wc_opendm:
    mov bx, si
    call wc_isme
    je .out
    mov al, WV_DM
    jmp wc_setview
.out:
    ret

; --- wc_leftpick - a press on left band AH/BX's item (wc_where's answer) ----
wc_leftpick:
    mov cx, bx
    call wc_lband                       ; AL = what, AH = which
    cmp al, LB_ROOM
    jne .dm
    mov al, ah
    jmp wc_setview
.dm:
    cmp al, LB_DM
    jne .fr
    mov al, ah
    xor ah, ah
    mov cl, 4                           ; WC_DREC = 16
    shl ax, cl
    add ax, wc_dms
    mov si, ax
    jmp short wc_opendm
.fr:
    cmp al, LB_FRIEND
    jne .out
    mov al, ah
    call wc_frrec
    jmp short wc_opendm
.out:
    ret

; -----------------------------------------------------------------------------
; wc_where - which region is the point CX, DX (SCREEN) in?
; out: AL = WH_*; for WH_PANE AH = the message (0xFF none); for WH_LEFT and
;      WH_RIGHT BX = the band. clobbers DX, SI, DI
; -----------------------------------------------------------------------------
WH_NONE     equ 0
WH_PANE     equ 1
WH_LEFT     equ 2
WH_RIGHT    equ 3
WH_NOTIFY   equ 4

wc_where:
    sub cx, [wc_ox]                     ; content-relative from here
    sub dx, [wc_oy]
    ; --- the status bar's right end: Notifications --------------------------
    cmp dx, [wc_sy]
    jb .nbar
    mov ax, [wc_cw]
    sub ax, WC_NOTIFYC * 8
    cmp cx, ax
    jb .no
    mov al, WH_NOTIFY
    ret
.nbar:
    cmp dx, WC_HDRH + 3
    jb .no
    ; --- the left pane ---------------------------------------------------------
    cmp word [wc_lx2], 0
    je .nleft
    cmp cx, [wc_lx2]
    jae .nleft
    mov ax, dx
    sub ax, WC_HDRH + 3
    xor dx, dx
    mov bx, WC_LH
    div bx
    mov bx, ax
    mov al, WH_LEFT
    ret
.nleft:
    ; --- the right pane --------------------------------------------------------
    cmp word [wc_rx1], 0
    je .nright
    cmp cx, [wc_rx1]
    jbe .nright
    mov ax, dx
    sub ax, WC_HDRH + 3
    xor dx, dx
    mov bx, WC_LH
    div bx
    mov bx, ax
    mov al, WH_RIGHT
    ret
.nright:
    ; --- the message pane ----------------------------------------------------
    cmp cx, [wc_tx]
    jb .no
    mov ax, dx
    sub ax, [wc_py1]
    jb .no
    xor dx, dx
    mov bx, WC_LH
    div bx
    cmp ax, [wc_rows]
    jae .no
    add ax, [wc_top]
    call wc_lfind                       ; BX = the message
    mov ah, 0xFF
    jc .pane
    mov ah, bl
.pane:
    mov al, WH_PANE
    ret
.no:
    mov al, WH_NONE
    ret

; --- wc_rightnick - right band BX's member -> SI = the nick, CF = 1 none ----
wc_rightnick:
    or bx, bx
    jz .no                              ; the title band
    dec bx
    mov cx, bx                          ; the CX'th member
    xor bx, bx
.c:
    cmp bl, [wc_rn]
    jae .no
    call wc_ismember
    jc .n
    jcxz .yes
    dec cx
.n:
    inc bx
    jmp short .c
.yes:
    mov ax, WC_RREC
    mul bx
    add ax, wc_ros
    mov si, ax
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; RIGHT PRESSES - OSAPI_WM_ONRCLICK (SPEC.md 13.11): CX, DX, SI = window;
; W_ONCLICK's environment
; =============================================================================
wc_onrclick:
    call wc_activity
    cmp byte [wc_abon], 0
    jne .out
    cmp byte [wc_pop], PP_NONE
    jne .out
    call wc_geom
    jc .out
    call wc_where
    cmp al, WH_PANE
    je .pane
    cmp al, WH_LEFT
    je .left
    cmp al, WH_RIGHT
    jne .out
    call wc_rightnick                   ; a member: add as a friend?
    jc .out
    mov bx, si
    call wc_isme
    je .out
    call wc_frfind
    jnc .out                            ; a friend already
    mov di, wc_askn
    mov cx, WC_NICKN
    call wc_ncopy
    mov si, wc_s_askadd1
    mov di, wc_s_askadd2
    mov byte [wc_askwhat], 1
    jmp short .ask
.left:
    mov cx, bx
    call wc_lband
    cmp al, LB_FRIEND                   ; a friend: remove?
    jne .out
    mov al, ah
    call wc_frrec
    mov bx, si
    mov di, wc_askn
    mov cx, WC_NICKN
    call wc_ncopy
    mov si, wc_s_askrem1
    mov di, wc_s_askrem2
    mov byte [wc_askwhat], 2
    jmp short .ask
.pane:
    cmp ah, 0xFF                        ; a message: select it, flag it?
    je .out
    mov [wc_sel], ah
    or word [wc_dirty], WCD_PANE
    call wc_cbflush
    jmp wc_flagask
.ask:
    call wc_askcompose                  ; wc_askbuf = SI, the nick, DI
    mov al, OS88UI_AYESNO
    mov bx, [wc_win]
    mov si, wc_askbuf
    mov di, wc_answer
    call os88ui_ask
.out:
    ret

; --- wc_askcompose - wc_askbuf = the string at SI, wc_askn, the string at DI
wc_askcompose:
    push di
    mov di, wc_askbuf
    mov bx, wc_askbuf + 39
    call wc_put
    push si
    mov si, wc_askn
    call wc_put
    pop si
    pop si
    call wc_put
    mov byte [di], 0
    ret

; --- wc_flagask - the selected message: flag it? ------------------------------
wc_flagask:
    mov bl, [wc_sel]
    cmp bl, 0xFF
    jne .one
    mov si, wc_s_selfirst
    call wc_setnote
    jmp wc_cbflush
.one:
    xor bh, bh
    call wc_mrec
    test byte [bx+WC_MFLG], WCM_MINE
    jz .theirs
    mov si, wc_s_ownmsg
    call wc_setnote
    jmp wc_cbflush
.theirs:
    mov byte [wc_askwhat], 3
    mov al, OS88UI_AYESNO
    mov bx, [wc_win]
    mov si, wc_s_askflag
    mov di, wc_answer
    jmp os88ui_ask

; --- wc_answer - the alert's completion: AL = the button (0 = Yes) ----------
; in: SI = our window, the lock held
wc_answer:
    or al, al
    jnz .out                            ; No, or cancelled
    mov al, [wc_askwhat]
    cmp al, 1
    je .add
    cmp al, 2
    je .rem
    cmp al, 3
    jne .out
    ; --- flag the selected message ------------------------------------------
    mov bl, [wc_sel]
    cmp bl, 0xFF
    je .out
    xor bh, bh
    call wc_mrec
    mov ax, [bx+WC_MSEQ]
    mov [wc_qflag], ax
    mov ax, [bx+WC_MSEQ+2]
    mov [wc_qflag+2], ax
    mov si, wc_s_flagging
    call wc_setnote
    call wc_kick
    jmp wc_cbflush
.add:
    cmp byte [wc_fn], WC_NFR
    jae .out
    mov al, [wc_fn]
    call wc_frrec
    mov di, si
    mov bx, wc_askn
    mov cx, WC_NICKN
    call wc_ncopy
    inc byte [wc_fn]
    call wc_wantsave
    or word [wc_dirty], WCD_LEFT
    jmp wc_cbflush
.rem:
    mov si, wc_askn
    call wc_frfind
    jc .out
    call wc_frdel
    jmp wc_cbflush
.out:
    ret

; =============================================================================
; THE BUTTONS (SPEC.md 20.5.1.3): fired on the RELEASE
; =============================================================================
wc_ondrag:
    mov bx, wc_btrec
    jmp os88ui_btndrag

wc_onup:
    mov bx, wc_btrec
    call os88ui_btnup                   ; AX = what fired, index + 1; 0 cancel
    dec ax
    js .out
    call wc_bok
    jc .out                             ; a greyed button explains itself
    cmp byte [wc_abon], 0
    jne .out
    cmp al, WB_STATUS
    je .status
    cmp al, WB_EMO
    je .emo
    cmp al, WB_SEARCH
    je .search
    cmp al, WB_FRIEND
    je .friend
    call wc_enter                       ; Send: what Enter does in this mode
    jmp wc_cbflush
.status:
    mov al, PP_STATUS
    jmp short .pop
.emo:
    mov al, PP_EMO
.pop:
    cmp [wc_pop], al                    ; its own button again: shut it
    je .shut
    mov [wc_pop], al
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wc_geom
    jmp wc_popdraw
.shut:
    call wc_popdown
    jmp wc_repaint
.search:
    mov al, MD_FIND
    jmp short .mode
.friend:
    mov al, MD_FRIEND
.mode:
    cmp byte [wc_mode], MD_NICK         ; nick mode is not left by a button,
    je .out                             ; nor code mode
    cmp byte [wc_mode], MD_KEY
    je .out
    call wc_setmode
    call wc_geom                        ; the prompt moves the field's left
    jmp wc_cbflush
.out:
    ret

; =============================================================================
; THE POPUPS
; =============================================================================
; --- wc_popdown - no popup (the caller repaints) ------------------------------
wc_popdown:
    mov byte [wc_pop], PP_NONE
    ret

; --- wc_pophit - a press with a popup up: a pick, or a dismissal ------------
; in: CX, DX = the point (SCREEN)
wc_pophit:
    push cx
    push dx
    call wc_geom
    call wc_popgeom                     ; CX = its rows
    mov bx, cx
    pop dx
    pop cx
    cmp cx, [wc_poprc+0]
    jl .dismiss
    cmp cx, [wc_poprc+4]
    jg .dismiss
    mov ax, dx
    sub ax, [wc_poprc+2]
    sub ax, 2
    jl .dismiss
    xor dx, dx
    mov cx, WC_LH
    div cx
    cmp ax, bx
    jae .dismiss
    mov bl, [wc_pop]                    ; which popup it was - held on the
    push bx                             ; stack, because the repaint below
    push ax                             ; spends every register
    call wc_popdown
    call wc_repaint
    pop ax
    pop bx
    cmp bl, PP_STATUS
    je .st
    ; --- an emoticon, typed at the caret ---------------------------------------
    mov bx, ax
    shl bx, 1
    mov si, [wc_emos+bx]
.type:
    lodsb
    or al, al
    jz .typed
    push si
    xor ah, ah
    mov si, wc_ln
    call os88line_key
    pop si
    jmp short .type
.typed:
    mov si, wc_ln
    mov bx, [wc_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, wc_ln
    call os88line_draw
    jmp wc_buttons_send
.st:
    mov [wc_st], al                     ; a status the user chose: not auto
    mov byte [wc_auto], 0
    or word [wc_dirty], WCD_HDR
    call wc_kick
    jmp wc_cbflush
.dismiss:
    call wc_popdown
    jmp wc_repaint
.out:
    ret

; =============================================================================
; THE MENUS (SPEC.md 113.6.4) - AM_ONCMD: AL = item, AH = menu, SI = window;
; the lock held, no clip
; =============================================================================
wc_oncmd:
    push ax
    cmp byte [wc_pop], PP_NONE
    je .np
    call wc_popdown
    call wc_repaint
.np:
    pop ax
    cmp ah, WM_ROOMS
    je .room
    cmp ah, WM_STAT
    je .stat
    cmp ah, WM_FRND
    je .frnd
    ; --- Chat --------------------------------------------------------------------
    cmp al, 0
    je .nick
    cmp al, 1
    je .flag
    cmp al, 2
    je .notify
    ; Reconnect: a fresh session, now
    call wc_abandon
    mov byte [wc_rq], RQ_NONE
    mov byte [wc_tok], 0
    mov word [wc_wait], 0
    mov word [wc_back], 0
    mov byte [wc_hist], 1
    cmp byte [wc_nick], 0
    je .done
    mov byte [wc_link], LK_JOIN
    mov si, wc_s_connecting
    call wc_setnote
    call wc_next
    jmp short .done
.nick:
    cmp byte [wc_link], LK_KEY          ; a code was being asked for: now a
    jne .nick1                          ; nick is
    mov byte [wc_link], LK_NONICK
.nick1:
    mov al, MD_NICK
    call wc_setmode
    mov di, wc_nick
    call wc_fieldset
    mov si, wc_s_picknick
    call wc_setnote
    call wc_geom
    jmp short .done
.flag:
    jmp wc_flagask
.notify:
    xor byte [wc_notify], 1
    call wc_menulabels
    call wc_wantsave
    or word [wc_dirty], WCD_STAT
    jmp short .done
.room:
    call wc_setview
    jmp short .done
.stat:
    mov [wc_st], al
    mov byte [wc_auto], 0
    or word [wc_dirty], WCD_HDR
    call wc_kick
    jmp short .done
.frnd:
    cmp al, 0
    jne .rem
    mov al, MD_FRIEND
    call wc_setmode
    call wc_geom
    jmp short .done
.rem:
    cmp byte [wc_view], WV_DM           ; Remove Friend: the conversation's
    jne .remno                          ; partner, when they are one
    mov si, wc_vdm
    call wc_frfind
    jc .remno
    call wc_frdel
    jmp short .done
.remno:
    mov si, wc_s_remhow
    call wc_setnote
.done:
    jmp wc_cbflush

; --- wc_menulabels - the Chat menu's Notifications item says what it is ----
wc_menulabels:
    mov ax, wc_it_notoff
    cmp byte [wc_notify], 0
    jne .on
    mov ax, wc_it_noton
.on:
    mov [wc_i_chat + 4], ax
    ret

; =============================================================================
; RESIZE - OSAPI_WM_ONRESIZE: the content changed; a full paint follows
; =============================================================================
wc_onresize:
    call wc_dirtylines                  ; every wrap is for the old width
    ret
