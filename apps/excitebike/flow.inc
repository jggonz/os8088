; The game around the race (SPEC.md 102.4): the title, the mode and course menus, the
; race, the results, the best times, game over and the attract demo, all inside the
; fullscreen bracket and all drawn as TILES (xb_puts: the compiled font, a run of
; byte moves a glyph; nothing here is `font_run`, which is a desktop routine).
;
; THE CAMPAIGN.  A player picks a mode (A: alone against the clock, B: with three
; opponents) and a course and rides it.  The time against the course's par ranks
; the ride (`xb_rank_calc`):
;
;     time <= par                1ST   qualified
;     time <  par + w2           2ND   qualified      w2 = 4.00 s
;     time <  par + w3           3RD   qualified      w3 = 8.00 s
;     otherwise                  NOT QUALIFIED: game over, and the title
;
; A qualified ride goes on to the next course.  After the fifth the campaign starts
; again from the first course on the SECOND PASS (`xb_flag` = 1): the harder obstacle
; set on both laps and its own par.  After the fifth course of the second pass the
; last course REPEATS, and each repeat (`xb_rep`) shrinks the qualify windows by
; XB_WSHRINK2 / XB_WSHRINK3 hundredths (both are gone at the eighth: only a time at
; par or better qualifies).  Esc in a race or on the results is back to the title.
;
; Best times are per course and pass and live in the package's bss, so they last
; as long as the window does (a package may not write a file from here; nothing else in
; the tree keeps a score either).  `EXBTRACK.DAT` beside the package is an optional
; sixth course - a designed stream of at most 224 bytes (xb_custom_load).
;
; Every screen ends its own routine with a code in AL and takes no other screen's
; state: the state machine is xb_game's, and xb_state (for the harness) says where
; it is.

; -----------------------------------------------------------------------------
; xb_game - the bracket's body once the art is loaded.  Returns when the player
; leaves (Esc on the title, Alt+Enter anywhere).
; -----------------------------------------------------------------------------
xb_game:
    SAVE
    call xb_custom_load
    mov byte [xb_attract], 0
.title:
    mov byte [xb_state], XS_TITLE
    mov al, EXBSONG_TITLE
    call xu_song_if
    call xb_scr_title
    cmp al, 1
    je .mode
    cmp al, 2
    je .best
    cmp al, 3
    je .demo
    jmp .leave                      ; 0: Esc / Alt+Enter
.best:
    mov byte [xb_state], XS_BEST
    call xb_scr_best
    or al, al
    jnz .leave
    jmp .title
.demo:
    call xb_run_attract
    cmp al, XR_ALT
    je .leave
    jmp .title
.mode:
    mov byte [xb_state], XS_MODE
    mov al, EXBSONG_SELECT
    call xu_song_if
    call xb_scr_mode
    cmp al, 1
    je .track
    cmp al, 2
    je .leave
    jmp .title
.track:
    mov byte [xb_state], XS_TRACK
    mov al, EXBSONG_SELECT
    call xu_song_if
    call xb_scr_track
    cmp al, 1
    je .start
    cmp al, 2
    je .leave
    jmp .mode
.start:
    mov byte [xb_flag], 0
    mov byte [xb_rep], 0
.race:
    call xb_run_race                ; AL = how it ended
    cmp al, XR_ALT
    je .leave
    cmp al, XR_FINISH
    jne .title                      ; Esc: the campaign is abandoned
    call xb_rank_calc
    mov byte [xb_state], XS_RESULT
    mov al, EXBSONG_RESULTS
    call xu_song_if
    call xb_scr_result
    cmp al, 2
    je .leave
    cmp al, 1
    je .title
    cmp byte [xb_qual], 0
    je .over
    call xb_advance                 ; CF: a custom course has no next
    jc .title
    jmp .race
.over:
    mov byte [xb_state], XS_OVER
    mov al, EXBSONG_GAMEOVER
    call xu_song_if
    call xb_scr_over
    or al, al
    jnz .leave
    jmp .title
.leave:
    RESTORE
    ret

; -----------------------------------------------------------------------------
; xb_run_race - the course [xb_track] on the pass [xb_flag]: load, palette, back end,
; the loop.  AL = [xb_result].
; -----------------------------------------------------------------------------
xb_run_race:
    mov al, XU_NONE                 ; the music stops before the disk reads (an FM note has no lease)
    call xu_song
    mov al, [xb_track]
    call xb_track_load
    jc .fail
    call xb_palette
    xor al, al
    cmp byte [xb_sel], 0
    je .nai
    call xb_nai_count
.nai:
    mov [xb_nai], al
    mov byte [xb_attract], 0
    call xb_race_begin
    call xu_race_begin              ; the engine's, and the start lights' beeps
    mov byte [xb_state], XS_RACE
    call xb_race
    call xu_race_end
    mov al, [xb_result]
    ret
.fail:
    mov byte [xb_error], XB_ERR_FILES
    mov byte [xb_result], XR_ESC
    xor al, al
    ret

; AL = the opponents this adapter races: three, or two on the CGA
xb_nai_count:
    mov al, XB_NAI_MAX
    cmp byte [xb_cga], 0
    je .o
    mov al, XB_NAI_CGA
.o:
    ret

; the attract demo: the normal race, Selection B, with the AI driving the rider
xb_run_attract:
    mov al, XU_NONE                 ; the demo is silent
    call xu_song
    mov byte [xb_state], XS_ATTRACT
    mov al, [xb_attrk]
    cmp al, EXB_NTRACKS
    jb .t
    xor al, al
.t:
    mov [xb_attrk], al
    inc byte [xb_attrk]
    mov byte [xb_flag], 0
    call xb_track_load
    jc .fail
    call xb_palette
    call xb_nai_count
    mov [xb_nai], al
    mov byte [xb_attract], 1
    call xb_race_begin
    call xb_race
    mov byte [xb_attract], 0
    mov byte [xb_nai], 0
    mov al, [xb_result]
    ret
.fail:
    xor al, al
    ret

; -----------------------------------------------------------------------------
; xb_rank_calc - the finished ride: time, par, difference, rank, best time, position
; -----------------------------------------------------------------------------
xb_rank_calc:
    call xb_header                  ; BX = the course's header
    mov ax, [bx + 2]
    cmp byte [xb_flag], 0
    je .par
    mov ax, [bx + 18]               ; the second pass has its own par
.par:
    mov [xb_par], ax
    mov dx, [xb_ftime]
    sub dx, ax
    mov [xb_diff], dx               ; signed hundredths: the time less the par
    ; the windows: they shrink with each repeat of the last course
    xor cx, cx
    mov cl, [xb_rep]
    mov ax, XB_WIN2
    mov bx, XB_WIN3
    jcxz .win
.shrink:
    sub ax, XB_WSHRINK2
    jns .a
    xor ax, ax
.a:
    sub bx, XB_WSHRINK3
    jns .b
    xor bx, bx
.b:
    loop .shrink
.win:
    mov cl, XB_RANK_1ST
    or dx, dx
    jle .ranked                     ; at par or better
    mov cl, XB_RANK_2ND
    cmp dx, ax
    jl .ranked
    mov cl, XB_RANK_3RD
    cmp dx, bx
    jl .ranked
    mov cl, XB_RANK_NQ
.ranked:
    mov [xb_rank], cl
    mov byte [xb_qual], 1
    cmp cl, XB_RANK_NQ
    jne .q
    mov byte [xb_qual], 0
.q:
    ; the best time of this course and pass
    mov byte [xb_newbest], 0
    call xb_best_index              ; BX = the slot
    mov ax, [xb_ftime]
    mov dx, [xb_best + bx]
    or dx, dx
    jz .set
    cmp ax, dx
    jae .pos
.set:
    mov [xb_best + bx], ax
    mov byte [xb_newbest], 1
.pos:
    ret

; xb_posn_calc - Selection B: 1 + the opponents ahead of the rider, latched on the
; first frame the rider is seen finished (an opponent already finished is ahead)
xb_posn_calc:
    mov byte [xb_posn], 1
    xor cx, cx
    mov cl, [xb_nai]
    jcxz .out
    mov si, xa_st
.p:
    cmp byte [si + XMO_FIN], 0
    jne .ahead
    mov ax, [si + XMO_POS + 1]
    cmp ax, [xm_pos + 1]
    jbe .n
.ahead:
    inc byte [xb_posn]
.n:
    add si, XM_STATE_SZ
    loop .p
.out:
    ret

; BX = the byte offset in xb_best of the course [xb_track] on the pass [xb_flag]
xb_best_index:
    xor bx, bx
    mov bl, [xb_track]
    cmp byte [xb_flag], 0
    je .p
    add bl, XB_NTRACKS_MAX
.p:
    shl bx, 1
    ret

; after a qualified ride: which course and pass next.  CF=1: none (a custom course)
xb_advance:
    mov al, [xb_track]
    cmp al, EXB_NTRACKS
    jae .none
    inc al
    cmp al, EXB_NTRACKS
    jb .next                        ; the next course of the pass
    cmp byte [xb_flag], 0
    jne .repeat
    mov byte [xb_flag], 1           ; the fifth is done: round again, the harder set
    mov byte [xb_rep], 0
    xor al, al
    jmp .next
.repeat:
    dec al                          ; the last course again, with less time to spare
    inc byte [xb_rep]
.next:
    mov [xb_track], al
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; xb_flow_frame - once a frame, after the steps: the lap flash and the GO! running
; down, and the end of the ride.  The player's block is live.
; -----------------------------------------------------------------------------
xb_flow_frame:
    test byte [xm_ev], XEV_LAP
    jz .nolap
    mov word [xb_flash], XB_FLASH_STEPS
    mov byte [xb_flashk], 1
.nolap:
    mov ax, [xb_flash]
    or ax, ax
    jz .noflash
    sub ax, [xb_ksteps]
    jns .f
    xor ax, ax
.f:
    mov [xb_flash], ax
.noflash:
    cmp byte [xm_fin], 0
    je .notfin
    cmp word [xb_fint], 0
    jne .latched
    call xb_posn_calc
.latched:
    mov ax, [xb_cs]
    mov [xb_ftime], ax
    mov ax, [xb_fint]
    add ax, [xb_ksteps]
    mov [xb_fint], ax
    cmp ax, XB_FIN_STEPS
    jb .notfin
    mov byte [xb_end], XR_FINISH + 1
    ret
.notfin:
    cmp byte [xb_attract], 0
    je .out
    mov ax, [xb_attn]
    add ax, [xb_ksteps]
    mov [xb_attn], ax
    cmp ax, XB_ATTRACT_MAX
    jb .out
    mov byte [xb_end], XR_DONE + 1
.out:
    ret

; -----------------------------------------------------------------------------
; the screens.  Each returns a code in AL and CF clear.
; -----------------------------------------------------------------------------

; clear the card's state after a race and draw a black screen: the split off, the start
; address home, the palette the menus' (the meadow theme)
xb_menu_begin:
    call xb_backend_menu
    call xb_screen_clear
    mov byte [xb_theme], 0
    call xb_palette
    ret

; the typed keys away, and the Enter that got us here let go of (bounded)
xb_flush:
.d:
    mov ah, 1
    int 16h
    jz .rel
    xor ah, ah
    int 16h
    jmp .d
.rel:
    mov cx, 40
.r:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jnc .out
    push cx
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    pop cx
    loop .r
.out:
    ret

; one frame's wait, then the typed key: CF=1 Alt+Enter, else AX = the key (0 = none)
xb_getkey:
    call os88alt_edge
    jc .alt
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    call xu_menu_tick               ; the menus' music, once a frame
    mov ah, 1
    int 16h
    jz .none
    xor ah, ah
    int 16h
    push ax
    or al, 20h
    cmp al, 'm'
    pop ax
    jne .clc
    call xu_toggle                  ; M: sound on / off on every screen
    xor ax, ax
.clc:
    clc
    ret
.none:
    xor ax, ax
    clc
    ret
.alt:
    ret

; DS:SI = a string, AL = row, AH = column, in the ink [xb_ink] (a palette slot: the
; planes of a VGA glyph; the CGA has one ink)
xb_pt:
    push ax
    push dx
    cmp byte [xb_cga], 0
    jne .go
    mov dx, 3c4h
    mov ah, [xb_ink]
    mov al, 2
    out dx, ax
.go:
    pop dx
    pop ax
    call xb_puts
    cmp byte [xb_cga], 0
    jne .out
    mov dx, 3c4h
    mov ax, 0f02h
    out dx, ax
.out:
    ret

; SI = a list of (row, column, ink, word string) ending in a row of 0FFh
xb_list:
.n:
    mov al, [si]
    cmp al, 0ffh
    je .out
    mov ah, [si + 1]
    mov cl, [si + 2]
    mov [xb_ink], cl
    mov bx, [si + 3]
    add si, 5
    push si
    mov si, bx
    call xb_pt
    pop si
    jmp .n
.out:
    ret

; a blank run of `CX` cells at AL = row, AH = column, to erase a line
xb_blank:
    push si
    mov si, xb_s_blank
    mov byte [xb_ink], 15
    call xb_pt
    pop si
    ret

; AX = hundredths -> "m:ss.cc" at ES:DI (seven characters), 0:00.00 to 9:59.99
xb_fmt_time:
    xor dx, dx
    mov cx, 6000
    div cx
    cmp al, 9
    jbe .m
    mov al, 9
.m:
    add al, '0'
    stosb
    mov al, ':'
    stosb
    mov ax, dx
    xor dx, dx
    mov cx, 100
    div cx
    push dx
    aam
    add ax, 3030h
    xchg al, ah
    stosb
    mov al, ah
    stosb
    mov al, '.'
    stosb
    pop ax
    aam
    add ax, 3030h
    xchg al, ah
    stosb
    mov al, ah
    stosb
    ret

; AX = a time (0 = none) -> "m:ss.cc" or "-:--.--" in xb_tbuf, terminated
xb_time_str:
    push ds
    pop es
    mov di, xb_tbuf
    or ax, ax
    jnz .t
    mov si, xb_s_none
    mov cx, 8
    rep movsb
    ret
.t:
    call xb_fmt_time
    xor al, al
    stosb
    ret

; AX = a signed difference in hundredths -> "+s.cc" / "-s.cc" in xb_tbuf
xb_diff_str:
    push ds
    pop es
    mov di, xb_tbuf
    mov bl, '+'
    or ax, ax
    jns .p
    neg ax
    mov bl, '-'
.p:
    mov [di], bl
    inc di
    xor dx, dx
    mov cx, 100
    div cx
    cmp ax, 99
    jbe .s
    mov ax, 99
.s:
    push dx
    aam
    add ax, 3030h
    or ah, ah
    xchg al, ah
    cmp al, '0'
    jne .two
    mov al, ' '                     ; no leading zero on the seconds
.two:
    stosb
    mov al, ah
    stosb
    mov al, '.'
    stosb
    pop ax
    aam
    add ax, 3030h
    xchg al, ah
    stosb
    mov al, ah
    stosb
    xor al, al
    stosb
    ret

; AL = a number 0..99 -> two digits at ES:DI (no terminator)
xb_fmt_2:
    aam
    add ax, 3030h
    xchg al, ah
    stosb
    mov al, ah
    stosb
    ret

; ---- the title ----------------------------------------------------------------
; -> AL 0 leave, 1 start, 2 best times, 3 the attract demo
xb_scr_title:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_ti_list
    call xb_list
    mov word [xb_idle], 0
.k:
    call xb_getkey
    jc .leave
    or ax, ax
    jnz .key
    inc word [xb_idle]
    cmp word [xb_idle], XB_IDLE_ATTRACT
    jb .k
    mov al, 3
    ret
.key:
    mov word [xb_idle], 0
    cmp al, 27
    je .leave
    cmp al, 13
    je .go
    or al, 20h
    cmp al, 'b'
    je .best
    jmp .k
.go:
    mov al, 1
    ret
.best:
    mov al, 2
    ret
.leave:
    xor al, al
    ret

; ---- the mode ---------------------------------------------------------------------
; -> AL 0 back, 1 chosen ([xb_sel]), 2 leave
xb_scr_mode:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_mo_list
    call xb_list
    mov al, [xb_sel]
    mov [xb_cur], al
    call xb_mode_cursor
.k:
    call xb_getkey
    jc .leave
    or ax, ax
    jz .k
    cmp al, 27
    je .back
    cmp al, 13
    je .go
    cmp ah, 48h
    je .flip
    cmp ah, 50h
    je .flip
    or al, 20h
    cmp al, 'a'
    je .a
    cmp al, 'b'
    je .b
    jmp .k
.a:
    mov byte [xb_cur], 0
    jmp .draw
.b:
    mov byte [xb_cur], 1
    jmp .draw
.flip:
    xor byte [xb_cur], 1
.draw:
    call xb_mode_cursor
    jmp .k
.go:
    mov al, [xb_cur]
    mov [xb_sel], al
    mov al, 1
    ret
.back:
    xor al, al
    ret
.leave:
    mov al, 2
    ret

xb_mode_cursor:
    mov byte [xb_ink], 11
    mov si, xb_s_nocursor
    cmp byte [xb_cur], 0
    jne .a
    mov si, xb_s_cursor
.a:
    mov al, 8
    mov ah, 4
    call xb_pt
    mov si, xb_s_nocursor
    cmp byte [xb_cur], 0
    je .b
    mov si, xb_s_cursor
.b:
    mov al, 13
    mov ah, 4
    call xb_pt
    ret

; ---- the course ---------------------------------------------------------------------
; -> AL 0 back, 1 chosen ([xb_track]), 2 leave
xb_scr_track:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_tr_list
    call xb_list
    call xb_ncourses
    mov ah, [xb_track]
    cmp ah, al
    jb .cur
    xor ah, ah
.cur:
    mov [xb_cur], ah
    xor cl, cl
.rows:
    call xb_track_row               ; CL = the row's course
    inc cl
    call xb_ncourses
    cmp cl, al
    jb .rows
    call xb_track_cursor
.k:
    call xb_getkey
    jc .leave
    or ax, ax
    jz .k
    cmp al, 27
    je .back
    cmp al, 13
    je .go
    cmp ah, 48h
    je .up
    cmp ah, 50h
    je .down
    jmp .k
.up:
    cmp byte [xb_cur], 0
    je .k
    dec byte [xb_cur]
    jmp .draw
.down:
    call xb_ncourses
    dec al
    cmp [xb_cur], al
    jae .k
    inc byte [xb_cur]
.draw:
    call xb_track_cursor
    jmp .k
.go:
    mov al, [xb_cur]
    mov [xb_track], al
    mov al, 1
    ret
.back:
    xor al, al
    ret
.leave:
    mov al, 2
    ret

; AL = the courses on offer: the five, and EXBTRACK.DAT's when it was found
xb_ncourses:
    mov al, EXB_NTRACKS
    cmp byte [xb_custom], 0
    je .o
    inc al
.o:
    ret

; the cursor column: '>' at the current course's row, blanks on the others
xb_track_cursor:
    mov byte [xb_ink], 11
    xor cl, cl
.r:
    mov si, xb_s_nocursor
    cmp cl, [xb_cur]
    jne .d
    mov si, xb_s_cursor
.d:
    mov al, cl
    shl al, 1
    add al, 7
    mov ah, 2
    push cx
    call xb_pt
    pop cx
    inc cl
    call xb_ncourses
    cmp cl, al
    jb .r
    ret

; CL = a course: its line "n NAME        PAR   BEST" at row 7 + 2n
xb_track_row:
    push cx
    mov al, [xb_track]
    push ax                         ; (xb_header reads it: the row borrows it)
    mov [xb_track], cl
    xor ch, ch
    push ds
    pop es
    mov di, xb_line
    mov al, cl
    add al, '1'
    stosb
    mov al, ' '
    stosb
    call xb_header
    mov si, [bx + 16]
    mov cx, 14
.name:
    lodsb
    or al, al
    jz .pad
    stosb
    loop .name
    jmp .par
.pad:
    mov al, ' '
    rep stosb
.par:
    mov al, ' '
    stosb
    mov ax, [bx + 2]
    call xb_fmt_time
    mov al, ' '
    stosb
    stosb
    call xb_best_index
    mov ax, [xb_best + bx]
    push di
    call xb_time_str
    pop di
    mov si, xb_tbuf
    mov cx, 7
    rep movsb
    xor al, al
    stosb
    mov cl, [xb_track]              ; (the course: the row borrowed xb_track)
    mov al, cl
    shl al, 1
    add al, 7
    mov ah, 4
    mov si, xb_line
    mov byte [xb_ink], 9
    call xb_pt
    pop ax
    mov [xb_track], al
    pop cx
    ret

; ---- the results -----------------------------------------------------------------------
; -> AL 0 continue (Enter), 1 Esc (the title), 2 leave
xb_scr_result:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_re_list
    call xb_list
    ; the course
    call xb_header
    push ds
    pop es
    mov di, xb_line
    mov al, [xb_track]
    inc al
    call xb_fmt_2
    mov al, ' '
    stosb
    stosb
    mov si, [bx + 16]
    mov cx, 16
.nm:
    lodsb
    or al, al
    jz .nmd
    stosb
    loop .nm
.nmd:
    xor al, al
    stosb
    mov si, xb_line
    mov al, 3
    mov ah, 8
    mov byte [xb_ink], 11
    call xb_pt
    mov si, xb_s_pass1
    cmp byte [xb_flag], 0
    je .pass
    mov si, xb_s_pass2
.pass:
    mov al, 5
    mov ah, 8
    mov byte [xb_ink], 9
    call xb_pt
    ; time, par, difference
    mov ax, [xb_ftime]
    call xb_time_str
    mov si, xb_tbuf
    mov al, 8
    mov ah, 20
    mov byte [xb_ink], 15
    call xb_pt
    mov ax, [xb_par]
    call xb_time_str
    mov si, xb_tbuf
    mov al, 10
    mov ah, 20
    call xb_pt
    mov ax, [xb_diff]
    call xb_diff_str
    mov si, xb_tbuf
    mov al, 12
    mov ah, 20
    mov byte [xb_ink], 9
    call xb_pt
    ; the rank
    xor bx, bx
    mov bl, [xb_rank]
    shl bx, 1
    mov si, [xb_rankstr + bx]
    mov al, 14
    mov ah, 20
    mov byte [xb_ink], 11
    cmp byte [xb_qual], 0
    jne .rk
    mov byte [xb_ink], 10
.rk:
    call xb_pt
    cmp byte [xb_nai], 0
    je .nopos
    mov si, xb_s_posn               ; POSITION n OF 4
    mov al, [xb_posn]
    add al, '0'
    mov [xb_s_posn + 9], al
    mov al, [xb_nai]
    add al, '1'
    mov [xb_s_posn + 14], al
    mov al, 16
    mov ah, 8
    mov byte [xb_ink], 9
    call xb_pt
.nopos:
    cmp byte [xb_newbest], 0
    je .nonew
    mov si, xb_s_newbest
    mov al, 18
    mov ah, 12
    mov byte [xb_ink], 11
    call xb_pt
.nonew:
    mov si, xb_s_next
    mov byte [xb_ink], 9
    cmp byte [xb_qual], 0
    jne .nx
    mov si, xb_s_gameover
    mov byte [xb_ink], 10
.nx:
    mov al, 21
    mov ah, 9
    call xb_pt
    cmp byte [xb_rep], 0
    je .wait
    mov si, xb_s_repeat
    mov al, 23
    mov ah, 6
    mov byte [xb_ink], 15
    call xb_pt
.wait:
    call xb_getkey
    jc .leave
    or ax, ax
    jz .wait
    cmp al, 27
    je .esc
    cmp al, 13
    jne .wait
    xor al, al
    ret
.esc:
    mov al, 1
    ret
.leave:
    mov al, 2
    ret

; ---- game over ---------------------------------------------------------------------------
; -> AL 0 the title, 2 leave
xb_scr_over:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_go_list
    call xb_list
    call xb_best_table
.k:
    call xb_getkey
    jc .leave
    or ax, ax
    jz .k
    cmp al, 13
    je .out
    cmp al, 27
    jne .k
.out:
    xor al, al
    ret
.leave:
    mov al, 2
    ret

; ---- the best times ----------------------------------------------------------------------
; -> AL 0 back, 2 leave
xb_scr_best:
    call xb_flush                    ; (before the screen is drawn: a key typed while it is being
    call xb_menu_begin              ; drawn is kept)
    mov si, xb_be_list
    call xb_list
    call xb_best_table
.k:
    call xb_getkey
    jc .leave
    or ax, ax
    jz .k
    cmp al, 13
    je .out
    cmp al, 27
    jne .k
.out:
    xor al, al
    ret
.leave:
    mov al, 2
    ret

; the table: per course its best time on each pass, from row 8
xb_best_table:
    mov al, [xb_flag]
    push ax
    mov al, [xb_track]
    push ax
    xor cl, cl
.r:
    push cx
    mov [xb_track], cl
    push ds
    pop es
    mov di, xb_line
    mov al, cl
    add al, '1'
    stosb
    mov al, ' '
    stosb
    call xb_header
    mov si, [bx + 16]
    mov cx, 14
.name:
    lodsb
    or al, al
    jz .pad
    stosb
    loop .name
    jmp .go
.pad:
    mov al, ' '
    rep stosb
.go:
    mov al, ' '
    stosb
    mov byte [xb_flag], 0
    call xb_best_index
    mov ax, [xb_best + bx]
    push di
    call xb_time_str
    pop di
    mov si, xb_tbuf
    mov cx, 7
    rep movsb
    mov al, ' '
    stosb
    stosb
    mov byte [xb_flag], 1
    call xb_best_index
    mov ax, [xb_best + bx]
    push di
    call xb_time_str
    pop di
    mov si, xb_tbuf
    mov cx, 7
    rep movsb
    xor al, al
    stosb
    pop cx
    push cx
    mov al, cl
    shl al, 1
    add al, 8
    mov ah, 3
    mov si, xb_line
    mov byte [xb_ink], 9
    call xb_pt
    pop cx
    inc cl
    cmp cl, EXB_NTRACKS
    jb .r
    pop ax
    mov [xb_track], al
    pop ax
    mov [xb_flag], al
    ret

; -----------------------------------------------------------------------------
; xb_custom_load - EXBTRACK.DAT beside the package, if there is one and it is whole:
;   +0   'EXBT'          +4  word par (hundredths)   +6  word S, the stream's bytes
;   +8   S bytes of (column id, run) pairs ending 0, 0
;   then word N (at most 15) and N x (word lap-relative column, word script id)
; at most 224 bytes.  Both laps are the stream.  The header lands in xb_chdr in the
; layout of exbtracks.inc and the file is REFUSED (silently: it is optional) on any
; of: the size, the magic, an odd or unterminated stream, a column id outside the
; dictionary, a lap under 24 or over EXB_LAP_MAX columns, a trigger outside the lap
; or naming a script that does not exist.  Course 6 then exists ([xb_custom]).
; -----------------------------------------------------------------------------
xb_custom_load:
    SAVE
    mov byte [xb_custom], 0
    mov ax, 1
    call OSAPI_MEM_CLAIM
    jc .out
    mov [xb_cseg], dx
    mov es, dx
    mov si, xb_custom_name
    xor bx, bx
    xor dx, dx
    mov cx, 1024
    call OSAPI_FILE_READ
    jc .free
    or dx, dx
    jnz .free
    cmp ax, 12
    jb .free
    cmp ax, 224
    ja .free
    mov [xb_cn], ax
    mov cx, ax
    push ds
    mov ds, [cs:xb_cseg]
    xor si, si
    mov di, xb_cbuf
    push cs
    pop es
    cld
    rep movsb
    pop ds
    call xb_custom_check
.free:
    mov dx, [xb_cseg]
    call OSAPI_MEM_FREE
.out:
    RESTORE
    ret

; xb_cbuf holds the file ([xb_cn] bytes): validate it and build the header
xb_custom_check:
    cmp word [xb_cbuf], 'EX'
    jne .no
    cmp word [xb_cbuf + 2], 'BT'
    jne .no
    mov cx, [xb_cbuf + 6]           ; S
    cmp cx, 4
    jb .no
    test cl, 1
    jnz .no
    mov ax, cx
    add ax, 10                      ; the header, the stream and N
    cmp ax, [xb_cn]
    ja .no
    mov si, xb_cbuf + 8
    add si, cx
    cmp word [si - 2], 0
    jne .no                         ; the stream ends 0, 0
    ; the runs
    mov si, xb_cbuf + 8
    xor dx, dx                      ; the lap's columns
    shr cx, 1
    dec cx                          ; (the terminator is not a run)
    jz .no
.run:
    lodsb
    cmp al, EXB_NCOLS
    jae .no
    lodsb
    or al, al
    jz .no
    xor ah, ah
    add dx, ax
    loop .run
    cmp dx, 24
    jb .no
    cmp dx, EXB_LAP_MAX
    ja .no
    mov [xb_clap], dx
    ; the triggers
    mov si, xb_cbuf + 8
    add si, [xb_cbuf + 6]
    mov cx, [si]
    cmp cx, 15
    ja .no
    add si, 2
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, si
    sub ax, xb_cbuf
    cmp ax, [xb_cn]
    ja .no
    mov di, xb_ctrig
    jcxz .term
.tr:
    lodsw
    cmp ax, [xb_clap]
    jae .no
    cmp di, xb_ctrig
    je .first
    cmp ax, [di - 4]                ; the columns must ascend
    jb .no
.first:
    stosw
    lodsw
    or ax, ax
    jz .no
    cmp ax, EXB_NSCRIPTS
    ja .no
    stosw
    loop .tr
.term:
    mov ax, 0ffffh
    stosw
    ; the header
    mov di, xb_chdr
    mov word [di], 0200h            ; theme 0, two laps
    mov ax, [xb_cbuf + 4]
    mov [di + 2], ax
    mov [di + 18], ax
    mov ax, [xb_clap]
    mov [di + 4], ax
    mov [di + 6], ax
    mov word [di + 8], xb_cbuf + 8
    mov word [di + 10], xb_cbuf + 8
    mov word [di + 12], xb_ctrig
    mov word [di + 14], xb_ctrig
    mov word [di + 16], xb_s_custom
    mov byte [xb_custom], 1
.no:
    ret

; ---- the text ------------------------------------------------------------------------------
xb_custom_name: db 'EXBTRACK.DAT', 0
xb_s_blank: db '                                        ', 0
xb_s_none: db '-:--.--', 0
xb_s_cursor: db '>', 0
xb_s_nocursor: db ' ', 0
xb_s_custom: db 'CUSTOM COURSE', 0
xb_s_pass1: db 'FIRST PASS', 0
xb_s_pass2: db 'SECOND PASS: HARDER OBSTACLES', 0
xb_s_posn: db 'POSITION 1 OF 4', 0
xb_s_newbest: db 'NEW BEST TIME!', 0
xb_s_next: db 'ENTER  NEXT COURSE', 0
xb_s_gameover: db 'ENTER  GAME OVER', 0
xb_s_repeat: db 'LAST COURSE AGAIN: WINDOW SHRINKS', 0
xb_r_1: db 'FIRST PLACE', 0
xb_r_2: db 'SECOND PLACE', 0
xb_r_3: db 'THIRD PLACE', 0
xb_r_nq: db 'NOT QUALIFIED', 0
xb_rankstr: dw xb_r_1, xb_r_2, xb_r_3, xb_r_nq

xb_ti_list:
    db 3, 10, 11
    dw xb_t_name
    db 5, 9, 9
    dw xb_t_sub
    db 10, 12, 9
    dw xb_t_start
    db 12, 12, 9
    dw xb_t_best
    db 14, 12, 9
    dw xb_t_leave
    db 16, 6, 15
    dw xb_t_c1
    db 18, 6, 15
    dw xb_t_c2
    db 20, 6, 15
    dw xb_t_c3
    db 23, 5, 15
    dw xb_t_orig
    db 0ffh
xb_t_name: db 'E X C I T E B I K E', 0
xb_t_sub: db 'NATIVE 8088 MOTOCROSS', 0
xb_t_start: db 'ENTER  START', 0
xb_t_best: db 'B      BEST TIMES', 0
xb_t_leave: db 'ESC    LEAVE', 0
xb_t_c1: db 'UP/DOWN LANE   LEFT/RIGHT LEAN', 0
xb_t_c2: db 'Z THROTTLE      X TURBO (HEATS)', 0
xb_t_c3: db 'ENTER PAUSE  C COLOURS  M SOUND', 0
xb_t_orig: db 'ORIGINAL ART, SOUND AND CODE', 0

xb_mo_list:
    db 4, 13, 11
    dw xb_m_title
    db 8, 6, 9
    dw xb_m_a
    db 10, 9, 15
    dw xb_m_a2
    db 13, 6, 9
    dw xb_m_b
    db 15, 9, 15
    dw xb_m_b2
    db 21, 5, 15
    dw xb_m_keys
    db 0ffh
xb_m_title: db 'SELECT MODE', 0
xb_m_a: db 'A  SOLO TIME TRIAL', 0
xb_m_a2: db 'RIDE ALONE AGAINST THE CLOCK', 0
xb_m_b: db 'B  CPU RIVALS', 0
xb_m_b2: db 'RACE AGAINST CPU RIDERS', 0
xb_m_keys: db 'UP/DOWN  ENTER  ESC', 0

xb_tr_list:
    db 3, 13, 11
    dw xb_c_title
    db 5, 6, 15
    dw xb_c_head
    db 21, 4, 15
    dw xb_c_keys
    db 0ffh
xb_c_title: db 'SELECT COURSE', 0
xb_c_head: db 'COURSE         PAR      BEST', 0
xb_c_keys: db 'UP/DOWN  ENTER  ESC', 0

xb_re_list:
    db 8, 8, 15
    dw xb_e_time
    db 10, 8, 15
    dw xb_e_par
    db 12, 8, 15
    dw xb_e_diff
    db 14, 8, 15
    dw xb_e_rank
    db 0ffh
xb_e_time: db 'TIME', 0
xb_e_par: db 'PAR', 0
xb_e_diff: db 'OVER/UNDER', 0
xb_e_rank: db 'RESULT', 0

xb_go_list:
    db 3, 15, 10
    dw xb_g_over
    db 6, 13, 9
    dw xb_g_best
    db 21, 10, 9
    dw xb_g_enter
    db 0ffh
xb_g_over: db 'GAME OVER', 0
xb_g_best: db 'BEST TIMES', 0
xb_g_enter: db 'ENTER  TITLE', 0

xb_be_list:
    db 3, 13, 11
    dw xb_g_best
    db 6, 5, 15
    dw xb_b_head
    db 21, 10, 9
    dw xb_b_enter
    db 0ffh
xb_b_head: db 'COURSE         PASS 1   PASS 2', 0
xb_b_enter: db 'ENTER  BACK', 0
