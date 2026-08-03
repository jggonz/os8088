; =============================================================================
; os8088 - apps/piano/piano.asm
;
; Piano, the fifth software package (SPEC.md 36): a colorful playable piano
; over the SPEC.md 34 tone tier, all of it through the API table
; (os88api.inc). One window, top to bottom: a note viewer (scrolling
; mini-staff that fills as you play AND during replay), a REPLAY/CLEAR
; button row with a message area, a row of three embedded song buttons,
; and 1.5+ octaves of clickable keys (C4..G5, 12 white + 8 black).
;
;   - Every note goes through OSAPI_SND_TONE (equal temperament, priority
;     40h); live play sounds 5 ticks and holds the key highlight through a
;     4-tick task_sleep inside the callback - bounded blocking, the
;     PCM_EXCL precedent (SPEC.md 34.4).
;   - Played notes are recorded (note id + osapi_get_ticks stamp) into a
;     300-note sequence; at the cap the policy is FULL-STOP: notes still
;     sound but stop being recorded ("SEQ FULL") - never oldest-drop.
;   - REPLAY blocks in the click callback, replaying with the original
;     relative timing (timestamp deltas clamped 2..24 ticks), polling
;     osapi_mouse each slept tick against a release-folded baseline
;     (the kernel's own SPEC.md 34.4 fold): a fresh press aborts. The
;     aborting press is not drained, so it also lands as an ordinary
;     click - which is exactly what makes close-box-during-replay work.
;   - The three songs (public domain: Twinkle Twinkle Little Star, Ode to
;     Joy, Mary Had a Little Lamb) are (note, duration) byte-pair tables;
;     loading one REPLACES the sequence and fills the viewer, so the same
;     REPLAY button plays it.
;
; Piano owns the menu bar while it is frontmost (SPEC.md 12.2): two menus,
; "Play" (Replay, Clear) and "Songs" (the three tables). They are a second
; route to the on-screen buttons, not a replacement - every item calls the
; very routine pn_onclick's button branch calls, guards and messages
; included, so the QWERTY keys and the buttons keep working unchanged.
;
; Colors beyond 0/15 retire [bb_mono] (SPEC.md 32) - supported, expected.
; Window procs run with the gfx lock held and preserve all registers.
; BP is used as a plain register holder only - never dereferenced (SS != DS).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PIANO', pn_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A piano octave: outer frame, 4 white keys with separators, 3 black keys.
; The mask is a solid rectangle one pixel proud of the frame.
;
;   data                          mask
;   ................              ................
;   ................              ################
;   ################              ################
;   #..##..##..##..#              ################
;   #..##..##..##..#              ################
;   #..##..##..##..#              ################
;   #..##..##..##..#              ################
;   #..##..##..##..#              ################
;   #..##..##..##..#              ################
;   #...#...#...#..#              ################
;   #...#...#...#..#              ################
;   #...#...#...#..#              ################
;   #...#...#...#..#              ################
;   ################              ################
;   ................              ################
;   ................              ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0000
    dw 0xFFFF
    dw 0x9999
    dw 0x9999
    dw 0x9999
    dw 0x9999
    dw 0x9999
    dw 0x9999
    dw 0x8889
    dw 0x8889
    dw 0x8889
    dw 0x8889
    dw 0xFFFF
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

; --- geometry (content-relative; the window is 224x177 -> content 222x158) -----
PN_CONT_W   equ 222
PN_CONT_H   equ 158
PN_VW_X1    equ 2                   ; note viewer frame
PN_VW_Y1    equ 2
PN_VW_X2    equ 219
PN_VW_Y2    equ 49
PN_ST_X1    equ 8                   ; staff line span
PN_ST_X2    equ 213
PN_VCOLS    equ 21                  ; note columns, 10 px apart at x=8+col*10
PN_ROW1_Y   equ 52                  ; REPLAY / CLEAR / message row
PN_ROW2_Y   equ 70                  ; songs row
PN_BTN_H    equ 16
PN_MSG_X    equ 112                 ; message text (area white-filled 110..219)
PN_KB_Y1    equ 88                  ; keyboard: white keys y 88..155
PN_KB_Y2    equ 155
PN_BK_Y2    equ 129                 ; black keys y 88..129
PN_WKEYS    equ 12                  ; white keys, 18 px wide at x=2+i*18
PN_BKEYS    equ 8                   ; black keys, 10 px wide at x=18*slot+15

; --- playback parameters --------------------------------------------------------
PN_MAXN     equ 300                 ; sequence cap (policy: full-stop)
PN_LIVEDUR  equ 5                   ; live note duration, ticks
PN_PRIO     equ 0x40                ; tone priority (the package default)

PN_BSS_TOTAL equ 918                ; see the bss layout after OS88_IMAGE_END

; -----------------------------------------------------------------------------
; pn_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader has zeroed our bss (empty sequence); stamp the READY message
; before the first paint can read it, and hand the window our menu set
; (SPEC.md 12.2) - registering here, before the loader shows the window,
; means the first bar drawn is already ours. OSAPI_MENU_SET preserves the
; flags as well as every register (SPEC.md 20.3), so the CF = 0 our contract
; owes the loader is the one the branch below already established.
; -----------------------------------------------------------------------------
pn_entry:
    push si
    mov word [pn_msg], pn_s_ready
    mov si, pn_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out                         ; no window: nothing to attach menus to
    mov si, pn_menus
    call OSAPI_MENU_SET             ; BX = window ptr, SI = the set
.out:
    pop si                          ; pop leaves the flags alone
    retf                            ; far-called by the loader (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; pn_paint - W_PAINT: full content repaint (content arrives white)
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [pn_ox], ax
    mov [pn_oy], dx
    call pn_draw_viewer
    call pn_draw_btns
    call pn_draw_keys
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                            ; far-called W_PAINT (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; pn_onclick - W_ONCLICK: hit-test keys and buttons, act
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [pn_ox], ax
    mov [pn_oy], dx
    pop dx
    pop cx
    sub cx, [pn_ox]                 ; -> content-relative click
    sub dx, [pn_oy]

    ; --- the keyboard ----------------------------------------------------------
    cmp dx, PN_KB_Y1
    jl .rows
    cmp dx, PN_KB_Y2
    jg .out
    cmp dx, PN_BK_Y2                ; black keys sit above their bottom row
    jg .white
    mov bx, 0                       ; test the 8 black keys first (they
.bk:                                ; overlay the whites)
    mov al, [pn_bslot+bx]
    mov ah, 18
    mul ah                          ; AX = slot*18
    add ax, 15                      ; black key left edge
    cmp cx, ax
    jl .bknext
    add ax, 9
    cmp cx, ax
    jg .bknext
    mov al, [pn_bnote+bx]
    call pn_note
    jmp .out
.bknext:
    inc bx
    cmp bx, PN_BKEYS
    jb .bk
.white:
    mov ax, cx
    sub ax, 2                       ; white index = (x-2)/18
    js .out
    mov bl, 18
    div bl                          ; AL = index (AX < 220: quotient fits)
    cmp al, PN_WKEYS
    jae .out
    mov bl, al
    mov bh, 0
    mov al, [pn_wnote+bx]
    call pn_note
    jmp .out

    ; --- the button rows -------------------------------------------------------
.rows:
    cmp dx, PN_ROW1_Y
    jl .out
    cmp dx, PN_ROW1_Y+PN_BTN_H
    jl .row1
    cmp dx, PN_ROW2_Y
    jl .out
    cmp dx, PN_ROW2_Y+PN_BTN_H
    jge .out
    cmp cx, 52                      ; songs row: TWINKLE / ODE / MARY
    jl .out
    cmp cx, 116
    jl .twinkle
    cmp cx, 118
    jl .out
    cmp cx, 158
    jl .ode
    cmp cx, 160
    jl .out
    cmp cx, 208
    jge .out
    mov si, pn_song_mary
    mov di, pn_s_mary
    call pn_load
    jmp .out
.twinkle:
    mov si, pn_song_twk
    mov di, pn_s_twk
    call pn_load
    jmp .out
.ode:
    mov si, pn_song_ode
    mov di, pn_s_ode
    call pn_load
    jmp .out
.row1:
    cmp cx, 2
    jl .out
    cmp cx, 58
    jl .replay
    cmp cx, 60
    jl .out
    cmp cx, 108
    jge .out
    call pn_clear
    jmp .out
.replay:
    call pn_replay
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                            ; far-called W_ONCLICK (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; pn_onkey - W_ONKEY: the QWERTY map printed on the keys themselves
; (whites a s d f g h j k l ; ' ]  -> C4..G5, blacks w e t y u o p [)
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, al                      ; keep the key; wm_content returns AX/DX
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [pn_ox], ax
    mov [pn_oy], dx
    mov si, pn_kmap
.scan:
    mov al, [si]
    or al, al
    jz .out                         ; end of table: not a piano key
    cmp al, cl
    je .hit
    inc si
    inc si
    jmp .scan
.hit:
    mov al, [si+1]                  ; the note id
    call pn_note
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                            ; far-called W_ONKEY (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; pn_oncmd - AM_ONCMD: the menu bar's route to the on-screen buttons
; in:  AL = item index, AH = menu index (0 = Play, 1 = Songs), SI = the
;      window that owns the set, BX = the set; gfx lock held, UI task
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; Called exactly like W_ONCLICK (SPEC.md 12.2), so it is the same job as
; pn_onclick's button branch minus the hit test: stash the content origin
; the pn_c* helpers add, then call the very routine the button calls. Each
; of pn_replay / pn_clear / pn_load already carries its own guards (the
; empty-sequence message, the abort poll) and already repaints the viewer
; and the message area - which is the whole of what these commands change,
; and is required, because the kernel does not repaint after a handler
; returns. The window is not resizable, so no OSAPI_WM_GROW is owed.
;
; The kernel clamps the indices to the counts we declared, so the branches
; below need no range check - only an order that matches the declaration.
; -----------------------------------------------------------------------------
pn_oncmd:
    push ax                         ; wm_content returns in AX/DX
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [pn_ox], ax
    mov [pn_oy], dx
    pop ax
    or ah, ah
    jnz .songs

    or al, al                       ; --- Play: Replay, Clear ---
    jnz .clear
    call pn_replay
    retf                            ; far-called menu handler (SPEC.md 20.5)
.clear:
    call pn_clear
    retf

.songs:                             ; --- Songs: Twinkle, Ode, Mary ---
    cmp al, 1
    je .ode
    ja .mary
    mov si, pn_song_twk
    mov di, pn_s_twk
    call pn_load
    retf
.ode:
    mov si, pn_song_ode
    mov di, pn_s_ode
    call pn_load
    retf
.mary:
    mov si, pn_song_mary
    mov di, pn_s_mary
    call pn_load
    retf

; -----------------------------------------------------------------------------
; pn_note - play one note live: record it (full-stop at the cap), append to
; the viewer, highlight the key, sound the tone, hold, unhighlight
; in:  AL = note id 0..19; [pn_ox]/[pn_oy] stashed; gfx lock held
; out: nothing; preserves all registers (blocks ~4 ticks - SPEC.md 36)
; -----------------------------------------------------------------------------
pn_note:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [pn_cur], al
    mov bx, [pn_cnt]
    cmp bx, PN_MAXN                 ; full-stop policy: keep sounding, stop
    jae .full                       ; recording, say so
    mov [pn_seqn+bx], al
    call OSAPI_GET_TICKS
    shl bx, 1
    mov [pn_seqt+bx], ax
    inc word [pn_cnt]
    mov ax, [pn_cnt]
    mov [pn_vcnt], ax
    jmp .sound
.full:
    mov word [pn_msg], pn_s_full
    call pn_draw_msg
.sound:
    call pn_draw_viewer
    mov al, [pn_cur]
    mov ah, 1
    call pn_key_hl                  ; key lights while it sounds
    mov bl, [pn_cur]
    mov bh, 0
    shl bx, 1
    mov ax, [pn_freq+bx]
    mov cx, PN_LIVEDUR
    mov dl, PN_PRIO
    call OSAPI_SND_TONE             ; self-expires via snd_tick (SPEC.md 34.3)
    call OSAPI_MOUSE                ; hold the highlight; a fresh press cuts
    mov [pn_base], al               ; the hold short (fast playing stays
    mov ax, PN_LIVEDUR-1            ; snappy; the tone expires on its own)
    call pn_wait
    mov al, [pn_cur]
    mov ah, 0
    call pn_key_hl
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_replay - the REPLAY button: block in this callback and play the whole
; sequence back with its original relative timing (deltas clamped 2..24
; ticks), filling the viewer note by note. Pacing is pn_wait's yield loop
; on osapi_get_ticks - NOT task_sleep, which returns immediately from the
; UI task when no other task is ready (sch_switch's nothing-ready fallback
; resumes the sleeper; menu_track's poll pattern is the sanctioned one).
; A fresh mouse press aborts - polled every pass against a release-folded
; baseline (the kernel's SPEC.md 34.4 fold; the press is NOT drained, so
; it lands as an ordinary click afterwards - which is what makes
; close-box-during-replay clean).
; in:  [pn_ox]/[pn_oy] stashed; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
pn_replay:
    cmp word [pn_cnt], 0
    jne .go
    mov word [pn_msg], pn_s_empty
    call pn_draw_msg
    ret
.go:
    mov word [pn_msg], pn_s_replay
    call pn_draw_msg
    call OSAPI_MOUSE                ; AL = buttons: the abort baseline
    mov [pn_base], al               ; (bit 0 may still be held from this
    mov word [pn_vcnt], 0           ; very click - the fold retires it)
    mov si, 0                       ; SI = sequence index
.next:
    mov ax, si
    inc ax
    mov [pn_vcnt], ax               ; the viewer fills during replay
    call pn_draw_viewer
    mov al, [pn_seqn+si]
    mov [pn_cur], al

    mov di, 6                       ; DI = duration: last note gets 6 ticks
    mov ax, si
    inc ax
    cmp ax, [pn_cnt]
    jae .clamped
    mov bx, si
    shl bx, 1
    mov di, [pn_seqt+bx+2]          ; delta to the next note's stamp
    sub di, [pn_seqt+bx]
    cmp di, 2                       ; clamp 2..24: original relative timing,
    jge .lo                         ; thinking pauses compressed to ~1.3 s
    mov di, 2
.lo:
    cmp di, 24
    jle .clamped
    mov di, 24
.clamped:
    mov al, [pn_cur]
    mov ah, 1
    call pn_key_hl
    mov bl, [pn_cur]
    mov bh, 0
    shl bx, 1
    mov ax, [pn_freq+bx]
    mov cx, di                      ; the tone lasts the note's duration
    mov dl, PN_PRIO
    call OSAPI_SND_TONE
    mov ax, di                      ; wait it out; CF=1 = a fresh press
    call pn_wait
    jc .abort
    mov al, [pn_cur]
    mov ah, 0
    call pn_key_hl
    inc si
    cmp si, [pn_cnt]
    jb .next
    mov word [pn_msg], pn_s_done
    call pn_draw_msg
    ret
.abort:
    xor ax, ax                      ; tone off (same priority: allowed)
    xor cx, cx
    mov dl, PN_PRIO
    call OSAPI_SND_TONE
    mov al, [pn_cur]
    mov ah, 0
    call pn_key_hl
    mov ax, [pn_cnt]
    mov [pn_vcnt], ax
    call pn_draw_viewer
    mov word [pn_msg], pn_s_stop
    call pn_draw_msg
    ret

; -----------------------------------------------------------------------------
; pn_wait - wait AX ticks inside a callback, yielding and watching the
; mouse. task_sleep cannot pace this: from the UI task with nothing else
; ready, sch_switch's fallback resumes the sleeper immediately - so this
; is a task_yield loop against osapi_get_ticks (wrap-safe signed compare),
; folding releases into [pn_base] and reporting a fresh press.
; in:  AX = ticks to wait, [pn_base] = button baseline
; out: CF=1 a fresh press arrived (wait cut short), CF=0 waited it out;
;      [pn_base] release-folded; preserves all registers
; -----------------------------------------------------------------------------
pn_wait:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax
    call OSAPI_GET_TICKS            ; AX = now
    add bx, ax                      ; BX = deadline
.poll:
    call OSAPI_TASK_YIELD
    call OSAPI_MOUSE                ; AL = buttons now
    mov ah, [pn_base]
    not ah
    and ah, al                      ; set now but not in the baseline =
    jnz .press                      ; a fresh press
    and [pn_base], al               ; fold releases into the baseline
    call OSAPI_GET_TICKS
    sub ax, bx                      ; wrap-safe: the sign decides
    js .poll
    clc
    jmp .out
.press:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_clear - the CLEAR button: empty the sequence and the viewer
; in:  [pn_ox]/[pn_oy] stashed; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
pn_clear:
    mov word [pn_cnt], 0
    mov word [pn_vcnt], 0
    call pn_draw_viewer
    mov word [pn_msg], pn_s_clear
    call pn_draw_msg
    ret

; -----------------------------------------------------------------------------
; pn_load - load a song table into the sequence buffer (REPLACING it),
; synthesizing timestamps from the duration column, and fill the viewer
; in:  SI -> song table (byte pairs note,dur; 0FFh ends), DI = message ptr
; out: nothing; clobbers AX, BX, DX, SI
; -----------------------------------------------------------------------------
pn_load:
    mov bx, 0                       ; BX = count
    mov dx, 0                       ; DX = running timestamp
.pair:
    mov al, [si]
    cmp al, 0xFF
    je .done
    mov [pn_seqn+bx], al
    push si
    mov si, bx
    shl si, 1
    mov [pn_seqt+si], dx
    pop si
    mov al, [si+1]                  ; duration -> next note's stamp
    mov ah, 0
    add dx, ax
    inc si
    inc si
    inc bx
    jmp .pair
.done:
    mov [pn_cnt], bx
    mov [pn_vcnt], bx
    call pn_draw_viewer
    mov [pn_msg], di
    call pn_draw_msg
    ret

; -----------------------------------------------------------------------------
; pn_key_hl - repaint one key in its sounding or normal state
; in:  AL = note id, AH = 1 highlight / 0 normal
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_key_hl:
    push ax
    push bx
    mov bl, al
    mov bh, 0
    cmp byte [pn_sharp+bx], 0
    jne .black
    mov bl, [pn_kidx+bx]            ; white key index 0..11
    mov bh, ah
    call pn_draw_wkey
    jmp .out
.black:
    mov bl, [pn_kidx+bx]            ; black key index 0..7
    mov bh, ah
    call pn_draw_bkey
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_keys - the whole keyboard: 12 whites (each re-draws its black
; neighbours - harmless overdraw on a full repaint)
; in:  [pn_ox]/[pn_oy] stashed
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_keys:
    push bx
    mov bx, 0
.wk:
    mov bh, 0
    call pn_draw_wkey
    inc bl
    cmp bl, PN_WKEYS
    jb .wk
    pop bx
    ret

; -----------------------------------------------------------------------------
; pn_draw_wkey - one white key: black frame, white (or CLBLUE) face, its
; CBLUE map letter (CWHITE when lit), then the overlapping black
; neighbours re-drawn on top
; in:  BL = white index 0..11, BH = 1 highlight
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_wkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                      ; BP = state:index (register use only)
    mov al, bl
    mov ah, 18
    mul ah                          ; AX = index*18
    add ax, 2
    mov di, ax                      ; DI = key left edge
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, PN_KB_Y1
    mov cx, di
    add cx, 18
    mov dx, PN_KB_Y2
    call pn_cframe
    mov ax, bp                      ; AH = highlight state
    mov al, CWHITE
    or ah, ah
    jz .face
    mov al, CLBLUE                  ; the sounding fill
.face:
    call OSAPI_SET_COLOR
    mov ax, di
    inc ax
    mov bx, PN_KB_Y1+1
    mov cx, di
    add cx, 17
    mov dx, PN_KB_Y2-1
    call pn_cfill
    mov ax, bp                      ; the map letter: CBLUE, CWHITE when lit
    mov al, CBLUE
    or ah, ah
    jz .lcol
    mov al, CWHITE
.lcol:
    call OSAPI_SET_COLOR
    mov bx, bp
    mov bh, 0
    mov al, [pn_wchar+bx]
    mov cx, di
    add cx, 5
    mov dx, 144
    call pn_cchar
    mov cx, 0                       ; re-draw the black keys overlapping
.nb:                                ; this white key (slots idx-1 and idx)
    mov bx, cx
    mov al, [pn_bslot+bx]
    mov ah, al
    inc ah
    mov bx, bp                      ; BL = this white key's index
    cmp al, bl                      ; black after white 'slot' overlaps
    je .redraw                      ; whites slot and slot+1
    cmp ah, bl
    jne .nbnext
.redraw:
    push cx
    mov bx, cx
    mov bh, 0                       ; neighbours re-draw un-highlighted
    call pn_draw_bkey
    pop cx
.nbnext:
    inc cx
    cmp cx, PN_BKEYS
    jb .nb
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_bkey - one black key: black body (CLRED core with a 1px black rim
; when sounding) and its CWHITE map letter
; in:  BL = black index 0..7, BH = 1 highlight
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_bkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                      ; BP = state:index (register use only)
    mov bh, 0
    mov al, [pn_bslot+bx]
    mov ah, 18
    mul ah
    add ax, 15                      ; AX = key left edge (18*slot + 15)
    mov di, ax                      ; DI = x1
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, PN_KB_Y1
    mov cx, di
    add cx, 9
    mov dx, PN_BK_Y2
    call pn_cfill
    mov ax, bp
    or ah, ah
    jz .letter
    mov al, CLRED                   ; the sounding core
    call OSAPI_SET_COLOR
    mov ax, di
    inc ax
    mov bx, PN_KB_Y1+1
    mov cx, di
    add cx, 8
    mov dx, PN_BK_Y2-1
    call pn_cfill
.letter:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, bp
    mov bh, 0
    mov al, [pn_bchar+bx]
    mov cx, di
    inc cx
    mov dx, 116
    call pn_cchar
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_viewer - the mini-staff: CBLUE frame, white field, 5 black staff
; lines, then the last <=21 of the first [pn_vcnt] sequence entries as
; CBLUE dots (black ledger line for middle C, small CLRED sharp glyph
; beside the sharps)
; in:  [pn_ox]/[pn_oy], [pn_vcnt], the sequence
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_viewer:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLUE
    call OSAPI_SET_COLOR
    mov ax, PN_VW_X1
    mov bx, PN_VW_Y1
    mov cx, PN_VW_X2
    mov dx, PN_VW_Y2
    call pn_cframe
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, PN_VW_X1+1
    mov bx, PN_VW_Y1+1
    mov cx, PN_VW_X2-1
    mov dx, PN_VW_Y2-1
    call pn_cfill
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov dx, 16                      ; staff lines at y 16/22/28/34/40
.staff:
    mov ax, PN_ST_X1
    mov bx, PN_ST_X2
    call pn_chline
    add dx, 6
    cmp dx, 46
    jb .staff

    mov si, [pn_vcnt]               ; SI = first entry shown
    or si, si
    jz .out
    mov di, 0                       ; DI = column
    sub si, PN_VCOLS                ; show the newest PN_VCOLS entries
    jnc .note
    mov si, 0
.note:
    cmp si, [pn_vcnt]
    jae .out
    mov bl, [pn_seqn+si]
    mov bh, 0
    mov al, [pn_step+bx]            ; dot center y = 46 - 3*step
    mov ah, 0
    mov cx, ax
    add ax, ax
    add ax, cx
    mov cx, 46
    sub cx, ax
    mov [pn_vy], cx
    mov al, [pn_sharp+bx]
    mov [pn_vsh], al
    mov ax, di                      ; column x = 8 + col*10
    mov cl, 10
    mul cl
    add ax, 8
    mov [pn_vx], ax
    cmp word [pn_vy], 46            ; middle C: black ledger line
    jne .dot
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [pn_vx]
    add ax, 2
    mov bx, [pn_vx]
    add bx, 11
    mov dx, 46
    call pn_chline
.dot:
    mov al, CBLUE
    call OSAPI_SET_COLOR
    mov ax, [pn_vx]                 ; the dot: 4x3 at (colx+5, y-1)
    add ax, 5
    mov bx, [pn_vy]
    dec bx
    mov cx, ax
    add cx, 3
    mov dx, [pn_vy]
    inc dx
    call pn_cfill
    cmp byte [pn_vsh], 0
    jz .cnext
    mov al, CLRED                   ; the small sharp glyph
    call OSAPI_SET_COLOR
    mov ax, [pn_vx]                 ; vlines at colx+1 and colx+3, y-3..y+2
    inc ax
    mov bx, [pn_vy]
    sub bx, 3
    mov dx, [pn_vy]
    add dx, 2
    call pn_cvline
    add ax, 2
    call pn_cvline
    mov ax, [pn_vx]                 ; hlines colx..colx+4 at y-2 and y+1
    mov bx, [pn_vx]
    add bx, 4
    mov dx, [pn_vy]
    sub dx, 2
    call pn_chline
    add dx, 3
    call pn_chline
.cnext:
    inc si
    inc di
    jmp .note
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_btns - REPLAY/CLEAR (black), the message, "SONGS:" and the three
; colored song buttons
; in:  [pn_ox]/[pn_oy], [pn_msg]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_btns:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 2
    mov bx, PN_ROW1_Y
    mov dx, 56
    mov si, pn_l_replay
    mov cl, CBLACK
    call pn_btn
    mov ax, 60
    mov bx, PN_ROW1_Y
    mov dx, 48
    mov si, pn_l_clear
    mov cl, CBLACK
    call pn_btn
    call pn_draw_msg
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, pn_l_songs
    mov cx, 2
    mov dx, PN_ROW2_Y+4
    call pn_cstr
    mov ax, 52
    mov bx, PN_ROW2_Y
    mov dx, 64
    mov si, pn_l_twk
    mov cl, CGREEN
    call pn_btn
    mov ax, 118
    mov bx, PN_ROW2_Y
    mov dx, 40
    mov si, pn_l_ode
    mov cl, CMAGENTA
    call pn_btn
    mov ax, 160
    mov bx, PN_ROW2_Y
    mov dx, 48
    mov si, pn_l_mary
    mov cl, CRED
    call pn_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_msg - white-fill the message area and draw [pn_msg] (CBLACK)
; in:  [pn_ox]/[pn_oy], [pn_msg]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_msg:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, 110
    mov bx, PN_ROW1_Y
    mov cx, PN_CONT_W-3
    mov dx, PN_ROW1_Y+PN_BTN_H-1
    call pn_cfill
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, [pn_msg]
    mov cx, PN_MSG_X
    mov dx, PN_ROW1_Y+4
    call pn_cstr
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_btn - one button: 1px frame + centered label, both in the given color
; in:  AX = content x1, BX = content y1, DX = width, SI = label, CL = color
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                      ; DI = x1
    push bx
    push dx
    mov al, cl
    call OSAPI_SET_COLOR
    pop dx
    pop bx
    mov ax, di
    mov cx, di
    add cx, dx
    dec cx
    push dx
    mov dx, bx
    add dx, PN_BTN_H-1
    call pn_cframe
    pop dx
    push bx                         ; center the label: BX = y1 kept below
    push dx
    call OSAPI_FONT_WIDTH           ; AX = label pixel width
    pop dx                          ; DX = button width
    sub dx, ax
    shr dx, 1
    mov cx, di
    add cx, dx
    pop dx                          ; DX = y1
    add dx, 4
    call pn_cstr
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; content-relative drawing helpers: add the stashed origin, call the API,
; restore. All preserve every register.
; -----------------------------------------------------------------------------
pn_cfill:                           ; AX=x1 BX=y1 CX=x2 DX=y2 (color set)
    push ax
    push bx
    push cx
    push dx
    add ax, [pn_ox]
    add cx, [pn_ox]
    add bx, [pn_oy]
    add dx, [pn_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pn_cframe:                          ; AX=x1 BX=y1 CX=x2 DX=y2 (color set)
    push ax
    push bx
    push cx
    push dx
    add ax, [pn_ox]
    add cx, [pn_ox]
    add bx, [pn_oy]
    add dx, [pn_oy]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pn_chline:                          ; AX=x1 BX=x2 DX=y (color set)
    push ax
    push bx
    push dx
    add ax, [pn_ox]
    add bx, [pn_ox]
    add dx, [pn_oy]
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    pop ax
    ret

pn_cvline:                          ; AX=x BX=y1 DX=y2 (color set)
    push ax
    push bx
    push dx
    add ax, [pn_ox]
    add bx, [pn_oy]
    add dx, [pn_oy]
    call OSAPI_GFX_VLINE
    pop dx
    pop bx
    pop ax
    ret

pn_cchar:                           ; AL=char CX=x DX=y (color set)
    push cx
    push dx
    add cx, [pn_ox]
    add dx, [pn_oy]
    call OSAPI_FONT_CHAR
    pop dx
    pop cx
    ret

pn_cstr:                            ; SI=string CX=x DX=y (color set)
    push cx
    push dx
    add cx, [pn_ox]
    add dx, [pn_oy]
    call OSAPI_FONT_STR
    pop dx
    pop cx
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
pn_tpl:
    dw 250, 100, 224, 177           ; x, y, w, h -> content 222 x 158
    dw pn_ttl, pn_paint, pn_onkey, pn_onclick

pn_ttl:      db 'Piano', 0

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
; Two menus, both mirroring button rows the window already draws: "Play" is
; the REPLAY/CLEAR row, "Songs" is the three song buttons. Titles are kept
; short on purpose - name + titles must clear the clock's hit band at x 434,
; and 'Piano' + 'Play' + 'Songs' ends near x 190.
    OS88_MENUSET pn_menus, pn_ttl, pn_oncmd
        OS88_MENU pn_m_play,  pn_mi_play,  2
        OS88_MENU pn_m_songs, pn_mi_songs, 3
    OS88_MENUSET_END pn_menus

pn_m_play:   db 'Play', 0
pn_mi_play:  dw pn_mi_replay, pn_mi_clear
pn_mi_replay: db 'Replay', 0
pn_mi_clear: db 'Clear', 0
pn_m_songs:  db 'Songs', 0
pn_mi_songs: dw pn_mi_twk, pn_mi_ode, pn_mi_mary
pn_mi_twk:   db 'Twinkle', 0
pn_mi_ode:   db 'Ode to Joy', 0
pn_mi_mary:  db 'Mary', 0

; button labels and messages (message area is 108 px = 13 chars)
pn_l_replay: db 'REPLAY', 0
pn_l_clear:  db 'CLEAR', 0
pn_l_songs:  db 'SONGS:', 0
pn_l_twk:    db 'TWINKLE', 0
pn_l_ode:    db 'ODE', 0
pn_l_mary:   db 'MARY', 0
pn_s_ready:  db 'READY', 0
pn_s_replay: db 'REPLAYING...', 0
pn_s_done:   db 'REPLAY DONE', 0
pn_s_stop:   db 'STOPPED', 0
pn_s_clear:  db 'CLEARED', 0
pn_s_full:   db 'SEQ FULL', 0
pn_s_empty:  db 'EMPTY', 0
pn_s_twk:    db 'TWINKLE READY', 0
pn_s_ode:    db 'ODE READY', 0
pn_s_mary:   db 'MARY READY', 0

; --- note tables (note ids 0..19 = semitones from C4; SPEC.md 36) --------------
pn_freq:                            ; equal temperament, Hz
    dw 262, 277, 294, 311, 330, 349, 370, 392, 415, 440
    dw 466, 494, 523, 554, 587, 622, 659, 698, 740, 784
pn_step:                            ; diatonic degree from C4 (staff height)
    db 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 7, 8, 8, 9, 10, 10, 11
pn_sharp:                           ; 1 = sharp (gets the viewer glyph)
    db 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0
pn_kidx:                            ; white index (naturals) / black (sharps)
    db 0, 0, 1, 1, 2, 3, 2, 4, 3, 5, 4, 6, 7, 5, 8, 6, 9, 10, 7, 11
pn_wnote:                           ; white key index -> note id
    db 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19
pn_bnote:                           ; black key index -> note id
    db 1, 3, 6, 8, 10, 13, 15, 18
pn_bslot:                           ; black key index -> left white key index
    db 0, 1, 3, 4, 5, 7, 8, 10
pn_wchar:                           ; the QWERTY map, printed on the keys
    db 'asdfghjkl;', 0x27, ']'
pn_bchar:
    db 'wetyuop['
pn_kmap:                            ; onkey: char, note id pairs; 0 ends
    db 'a', 0, 's', 2, 'd', 4, 'f', 5, 'g', 7, 'h', 9, 'j', 11
    db 'k', 12, 'l', 14, ';', 16, 0x27, 17, ']', 19
    db 'w', 1, 'e', 3, 't', 6, 'y', 8, 'u', 10, 'o', 13, 'p', 15, '[', 18
    db 0

; --- embedded songs: byte pairs (note id, duration ticks), 0FFh ends -----------
; quarter = 6 ticks (~1/3 s), half = 12 (SPEC.md 36)
pn_song_twk:                        ; Twinkle Twinkle Little Star (42 notes)
    db 0,6, 0,6, 7,6, 7,6, 9,6, 9,6, 7,12
    db 5,6, 5,6, 4,6, 4,6, 2,6, 2,6, 0,12
    db 7,6, 7,6, 5,6, 5,6, 4,6, 4,6, 2,12
    db 7,6, 7,6, 5,6, 5,6, 4,6, 4,6, 2,12
    db 0,6, 0,6, 7,6, 7,6, 9,6, 9,6, 7,12
    db 5,6, 5,6, 4,6, 4,6, 2,6, 2,6, 0,12
    db 0xFF

pn_song_ode:                        ; Ode to Joy, Beethoven (30 notes)
    db 4,6, 4,6, 5,6, 7,6, 7,6, 5,6, 4,6, 2,6
    db 0,6, 0,6, 2,6, 4,6, 4,9, 2,3, 2,12
    db 4,6, 4,6, 5,6, 7,6, 7,6, 5,6, 4,6, 2,6
    db 0,6, 0,6, 2,6, 4,6, 2,9, 0,3, 0,12
    db 0xFF

pn_song_mary:                       ; Mary Had a Little Lamb (26 notes)
    db 4,6, 2,6, 0,6, 2,6, 4,6, 4,6, 4,12
    db 2,6, 2,6, 2,12, 4,6, 7,6, 7,12
    db 4,6, 2,6, 0,6, 2,6, 4,6, 4,6, 4,6, 4,6
    db 2,6, 2,6, 4,6, 2,6, 0,12
    db 0xFF

    OS88_BSS PN_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = empty sequence, empty viewer. pn_msg is stamped by pn_entry
; before the first paint can read it.
pn_ox    equ os88_image_end + 0     ; word: content left (per handler call)
pn_oy    equ os88_image_end + 2     ; word: content top
pn_msg   equ os88_image_end + 4     ; word: current message string ptr
pn_cnt   equ os88_image_end + 6     ; word: recorded notes (cap PN_MAXN)
pn_vcnt  equ os88_image_end + 8     ; word: notes the viewer shows (replay
                                    ;       resets it and walks it back up)
pn_base  equ os88_image_end + 10    ; byte: replay abort baseline (buttons)
pn_cur   equ os88_image_end + 11    ; byte: the note being sounded
pn_vx    equ os88_image_end + 12    ; word: viewer scratch - column x
pn_vy    equ os88_image_end + 14    ; word: viewer scratch - dot center y
pn_vsh   equ os88_image_end + 16    ; byte: viewer scratch - sharp flag
                                    ; +17: pad
pn_seqn  equ os88_image_end + 18    ; 300 bytes: note ids
pn_seqt  equ os88_image_end + 318   ; 300 words: osapi_get_ticks stamps
                                    ; total 918 = PN_BSS_TOTAL
