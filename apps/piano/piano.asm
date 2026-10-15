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
PN_VNONE    equ 0xFF                ; pn_vshow: the column shows no note
PN_ROW1_Y   equ 52                  ; REPLAY / CLEAR / message row
PN_ROW2_Y   equ 70                  ; songs row
PN_BTN_H    equ 16
PN_MSG_X    equ 112                 ; message text (area white-filled 110..219)
PN_KB_Y1    equ 88                  ; keyboard: white keys y 88..155
PN_KB_Y2    equ 155                 ; ...at FULL height, which is now a CEILING
PN_BK_Y2    equ 129                 ; black keys y 88..129, likewise
PN_WLBL_UP  equ 11                  ; the white key's letter, up from its foot
PN_BLBL_UP  equ 13                  ; ...and the black key's
PN_KB_MIN   equ 24                  ; the shortest white key worth drawing: a
                                    ; floor, so a content box too short for a
                                    ; keyboard gets a clipped one rather than
                                    ; none. No adapter here reaches it - CGA's
                                    ; 136 rows of content leave 45
PN_WKEYS    equ 12                  ; white keys, 18 px wide at x=2+i*18
PN_BKEYS    equ 8                   ; black keys, 10 px wide at x=18*slot+15
PN_WK_LEFT  equ 2                   ; pn_draw_wkey: BH flag, left neighbour only

; --- playback parameters --------------------------------------------------------
PN_MAXN     equ 300                 ; sequence cap (policy: full-stop)
PN_LIVEDUR  equ 5                   ; live note duration, ticks
PN_PRIO     equ 0x40                ; tone priority (the package default)

PN_BSS_TOTAL equ 655                ; see the bss layout after OS88_IMAGE_END

; -----------------------------------------------------------------------------
; pn_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
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
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE pn_ret      ; ...and holds no segment word, so the
                                    ; proc is any `ret` (SPEC.md 20.5)
    mov ax, pn_metrics              ; the keyboard's depth follows the box, and
    call OSAPI_WM_ONRESIZE          ; the box is the kernel's (SPEC.md 11.98).
                                    ; pn_metrics IS the handler: it reads DX and
                                    ; keeps every register, which is the whole
                                    ; of what ONRESIZE asks
    mov ax, pn_onup                 ; SPEC.md 13.7/13.8.1: the five buttons
    call OSAPI_WM_ONMOUSEUP         ; fire on the RELEASE and follow the
    mov ax, pn_ondrag               ; pointer between the edges. The KEYS do
    call OSAPI_WM_ONDRAG            ; not - a note is a safe prefix action
    call OSAPI_WM_GEOM              ; CX/DX = the content we were actually
    call pn_metrics                 ; given, which on a CGA is 22 rows short
    mov si, pn_menus
    call OSAPI_MENU_SET             ; BX = window ptr, SI = the set
    mov si, pn_about                ; ...and 'About Piano' above the Close the
    call OSAPI_ABOUT_SET            ; kernel already puts in our pull-down
                                    ; (SPEC.md 12.2). Flags preserved, like
                                    ; menu_win_set above
    mov al, 1                       ; ...and it PROMISES its content stands
    call OSAPI_WM_SAVEU             ; still while it is not drawing (SPEC.md
                                    ; 11.96.1): no worker, and its one polling
                                    ; loop runs INSIDE a click callback, so
                                    ; every pixel it changes is a pixel the
                                    ; user asked for
.out:
    pop si                          ; pop leaves the flags alone
    ret

; -----------------------------------------------------------------------------
; pn_metrics - how deep the keyboard can be in the box we actually have
;
; in:  DX = the content HEIGHT
; out: pn_kby2 / pn_bky2 set; all registers preserved
;
; It is ALSO the W_ONRESIZE handler (SPEC.md 11.98: SI = our window, CX/DX =
; the new content size, the gfx lock held, no drawing), registered directly:
; the wrapper that called it and returned was four bytes of nothing.
;
; The window is 224x177 and its content 222x158, which fits a VGA and a
; Hercules and does not fit a CGA: the desktop band there is 155 rows, so
; wm_fit hands this window 136 rows of content and the keyboard's last 22 rows
; were drawn through the bottom of its own frame onto the desktop - the
; gfx_* primitives clip to the SCREEN, not to the window (SPEC.md 11.3).
;
; SHORTER KEYS rather than a second layout: everything above y=88 is text and
; buttons at fixed rows and none of it can give anything up, so the keyboard
; is the only part with slack in it - and a piano keyboard is a shape that
; reads correctly at any height, which is why this is a scale rather than a
; metric record like apps/arkanoid's.
;
; THE BLACK KEYS KEEP THEIR SHARE, 41/67 of the white key, so the instrument
; stays in proportion instead of the black keys swallowing a short keyboard.
; At full height that arithmetic returns exactly PN_BK_Y2, so a VGA and a
; Hercules draw the identical pixels they always did.
;
; ...and both LETTERS come with them. They were at y=144 and y=116, inside a
; key that ended at 155 - eleven and thirteen rows up from their own key's
; foot - and left where they were they would have printed below a shortened
; keyboard, which is the same bug one level down.
; -----------------------------------------------------------------------------
pn_metrics:
    push ax
    push bx
    push dx
    sub dx, 3                       ; the same 2px margin the full layout leaves
    cmp dx, PN_KB_Y2                ; ...never taller than it was DESIGNED for,
    jbe .wok                        ; which is the constant and never [pn_kby2]:
    mov dx, PN_KB_Y2                ; that word is this routine's own OUTPUT, it
.wok:                               ; is bss so it arrives 0, and 0 fails this
                                    ; test for every box there is - so the first
                                    ; call took the full height whatever it was
                                    ; handed, which is the bug the header above
                                    ; describes, still happening on the one
                                    ; adapter it was written for (11.98.1.1)
    cmp dx, PN_KB_Y1 + PN_KB_MIN    ; ...and never so short there is no key
    jae .wok2
    mov dx, PN_KB_Y1 + PN_KB_MIN
.wok2:
    mov [pn_kby2], dx
    mov al, dl                      ; the black key's share of the white one,
    sub al, PN_KB_Y1                ; in bytes: DX is 112..155 here, so the
    mov bl, PN_BK_Y2 - PN_KB_Y1     ; height is <= 67, 67*41 fits AX and the
    mul bl                          ; quotient is <= 41
    mov bl, PN_KB_Y2 - PN_KB_Y1
    div bl
    add al, PN_KB_Y1
    mov ah, 0
    mov [pn_bky2], ax
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_paint - W_PAINT: full content repaint (content arrives white)
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; Also pn_abdismiss's way back: the card covered all three parts, and with
; [pn_abon] already 0 this draws exactly them and no card.
; -----------------------------------------------------------------------------
pn_paint:
    push ax                         ; the three pn_org writes; every painter
    push bx                         ; below keeps every register
    push dx
    call pn_org
    call pn_draw_viewer
    call pn_draw_btns
    call pn_draw_keys
    cmp byte [pn_abon], 0           ; ...and the About card LAST, over the
    je .out                         ; keyboard it is opaque about (20.5.1)
    push si
    mov si, pn_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
    pop si
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_org - stash the content origin every pn_c* helper adds (internal)
; in:  SI = the window
; out: [pn_ox]/[pn_oy] and AX/DX = the content's left/top; BX = SI
;
; Six handlers fetched it with the same four instructions; this is them once.
; -----------------------------------------------------------------------------
pn_org:
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [pn_ox], ax
    mov [pn_oy], dx
pn_ret:                             ; OS88_REGION_MOVABLE's proc: any `ret`
    ret

; -----------------------------------------------------------------------------
; pn_onclick - W_ONCLICK: hit-test keys and buttons, act
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; THE KEYBOARD IS ARITHMETIC, not a table walk. A black key is 10 px at
; x = 18*slot + 15, so (x - 15) / 18 is the only slot a point can be on and
; the remainder says whether it is on it (0..9); [pn_bchs] says whether that
; slot HAS a black key, and its note is the white key's below it plus one.
; It used to try all eight black keys with a multiply each before falling to
; the white test, which is the same answer eight times slower.
; -----------------------------------------------------------------------------
pn_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pn_abdismiss               ; the credits are up: this click is spent
    jc .out                         ; taking them down
    call pn_pt                      ; -> content-relative click, origin stashed

    ; --- the keyboard ----------------------------------------------------------
    cmp dx, PN_KB_Y1
    jl .rows
    cmp dx, [pn_kby2]
    jg .out
    mov bl, 18
    cmp dx, [pn_bky2]               ; black keys sit above their bottom row
    jg .white
    mov ax, cx                      ; the black keys first (they overlay the
    sub ax, 15                      ; whites): slot = (x - 15) / 18
    jb .white
    div bl                          ; AL = slot, AH = x into it (AX < 220)
    cmp ah, 9                       ; ...past the key's 10 px: a white
    ja .white
    cmp al, 11
    ja .white
    xchg ax, bx                     ; BL = slot (and BH = the remainder, which
    xor bh, bh                      ; this clears)
    cmp byte [pn_bchs+bx], 0        ; no black key on this slot
    xchg ax, bx                     ; (BL = 18 again; xchg keeps the flags)
    je .white
    mov ah, 1                       ; the semitone above the white key
    jmp short .note
.white:
    mov ax, cx
    sub ax, 2                       ; white index = (x-2)/18
    jb .out
    div bl                          ; AL = index (AX < 220: quotient fits)
    cmp al, PN_WKEYS
    jae .out
    xor ah, ah
.note:
    mov bl, al
    xor bh, bh
    mov al, [pn_wnote+bx]
    add al, ah
    call pn_note
    jmp short .out

    ; --- the button rows -------------------------------------------------------
    ; SPEC.md 13.6/13.8: Replay plays, Clear throws a recording away and the
    ; three song buttons replace it - not one has a safe prefix action, so the
    ; press only ARMS and DRAWS and pn_onup acts. The KEYS are untouched: a
    ; piano key sounds a note, which is exactly the safe prefix action 13.6 is
    ; about, and a note that waited for the release would be unplayable.
.rows:
    call pn_bhit
    or al, al
    jz .out
    xor ah, ah
    call os88ui_arm
    call pn_setdown
.out:
    jmp short pn_pop6               ; the shared six-register exit, below

; -----------------------------------------------------------------------------
; pn_onkey - W_ONKEY: the QWERTY map printed on the keys themselves
; (whites a s d f g h j k l ; ' ]  -> C4..G5, blacks w e t y u o p [)
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The map IS the two letter tables the keys are drawn with - [pn_wchar], by
; white index, a 0, then [pn_bchs], by black slot: 25 contiguous bytes - so
; the letter printed on a key and the key it plays cannot disagree. It was a
; third, separate (char, note) table of its own, walked a pair at a time;
; `repne scasb` walks this one at 15 cycles a byte, which is why every key
; reaches its note sooner than it did (the scan was 75 cycles an entry).
; -----------------------------------------------------------------------------
pn_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pn_abdismiss               ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    or al, al                       ; no ASCII: not a piano key - and the empty
    jz .out                         ; slots in the map are 0, so never match
    push es                         ; ES is the KERNEL's in a callback (SPEC.md
    push ds                         ; 20): scasb reads ES:DI, and the map is
    pop es                          ; ours
    mov di, pn_wchar
    mov cx, PN_KMAPN
    cld
    repne scasb
    pop es
    jne .out                        ; not a piano key
    lea bx, [di-pn_wchar-1]         ; BX = where: 0..11 a white index, 13..24
    mov al, [pn_wnote+bx]           ; a black slot + 13
    sub bx, 13
    jb .play                        ; a white key: its own note
    mov al, [pn_wnote+bx]           ; a black one: a semitone above the white
    inc ax                          ; key it sits on
.play:
    push ax
    call pn_org
    pop ax
    call pn_note
.out:
pn_pop6:                            ; ...pn_onclick and pn_onup leave by here
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_oncmd - AM_ONCMD: the menu bar's route to the on-screen buttons
; in:  AL = item index, AH = menu index (0 = Play, 1 = Songs), SI = the
;      window that owns the set, BX = the set; gfx lock held, UI task
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; Called exactly like W_ONCLICK (SPEC.md 12.2), so it is the same job as
; pn_onclick's button branch minus the hit test: stash the content origin
; the pn_c* helpers add, then call the very routine the button calls -
; pn_bact, with the menu item turned into the button's number: Play's two
; items ARE buttons 1 and 2 and Songs' three are 3..5, in the same order.
; Each of pn_replay / pn_clear / pn_load already carries its own guards (the
; empty-sequence message, the abort poll) and already repaints the viewer
; and the message area - which is the whole of what these commands change,
; and is required, because the kernel does not repaint after a handler
; returns. The window is not resizable, so no OSAPI_WM_GROW is owed.
;
; The kernel clamps the indices to the counts we declared, so the mapping
; below needs no range check - only an order that matches the declaration.
; -----------------------------------------------------------------------------
pn_oncmd:
    push ax                         ; pn_org returns in AX/DX
    call pn_abdismiss               ; a menu pick takes the credits down first,
    call pn_org                     ; and then does what it says
    pop ax
    inc ax                          ; Play: item + 1
    or ah, ah
    jz pn_bact
    add al, 2                       ; Songs: item + 3
                                    ; ...and FALLS INTO pn_bact

; -----------------------------------------------------------------------------
; pn_bact - do what button AL says (SPEC.md 13.8)
; in:  AL = 1..PN_NBTN; [pn_ox]/[pn_oy] stashed; the gfx lock is held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
pn_bact:
    cmp al, 2
    jb pn_replay
    je pn_clear
    sub al, 3                       ; 3..5: a song - its table carries the
    cbw                             ; message it loads with
    xchg bx, ax
    add bx, bx
    mov si, [pn_songs+bx]
    jmp pn_load

; -----------------------------------------------------------------------------
; pn_replay - the REPLAY button: block in this callback and play the whole
; sequence back with its original relative timing (deltas clamped 2..24
; ticks, at record time - SPEC.md 36), filling the viewer note by note.
; Pacing is pn_wait's yield loop
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
    mov ax, pn_s_empty
    jmp short pn_say
.go:
    mov ax, pn_s_replay
    call pn_say
    call OSAPI_MOUSE                ; AL = buttons: the abort baseline
    mov [pn_base], al               ; (bit 0 may still be held from this
                                    ; very click - the fold retires it)
    xor si, si                      ; SI = sequence index
.next:
    lea ax, [si+1]
    mov [pn_vcnt], ax               ; the viewer fills during replay
    call pn_vsync
    mov di, 6                       ; DI = duration: last note gets 6 ticks
    cmp ax, [pn_cnt]
    jae .last
    mov al, [pn_seqd+si]            ; ...the others the delta to the next
    cbw                             ; note, clamped when it was recorded
    xchg di, ax
.last:
    mov al, [pn_seqn+si]
    mov [pn_cur], al
    mov cx, di                      ; the tone lasts the note's duration
    call pn_play
    mov ax, di                      ; wait it out; CF=1 = a fresh press
    call pn_wait
    jc .abort
    call pn_key_off
    inc si
    cmp si, [pn_cnt]
    jb .next
    mov ax, pn_s_done
    jmp short pn_say
.abort:
    xor ax, ax                      ; tone off (same priority: allowed)
    xor cx, cx
    mov dl, PN_PRIO
    call OSAPI_SND_TONE
    call pn_key_off
    mov ax, [pn_cnt]
    mov [pn_vcnt], ax
    call pn_vsync
    mov ax, pn_s_stop
    jmp short pn_say

; -----------------------------------------------------------------------------
; pn_clear - the CLEAR button: empty the sequence and the viewer
; in:  [pn_ox]/[pn_oy] stashed; gfx lock held
; out: nothing; clobbers AX
; -----------------------------------------------------------------------------
pn_clear:
    xor ax, ax
    mov [pn_cnt], ax
    mov [pn_vcnt], ax
    call pn_vsync
    mov ax, pn_s_clear
                                    ; ...and FALLS INTO pn_say

; -----------------------------------------------------------------------------
; pn_say - the message area says AX now (internal)
; in:  AX = the NUL-terminated message; [pn_ox]/[pn_oy] stashed
; out: [pn_msg] = AX, drawn; preserves all registers
; -----------------------------------------------------------------------------
pn_say:
    mov [pn_msg], ax
                                    ; ...and FALLS INTO pn_draw_msg

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
; pn_load - load a song table into the sequence buffer (REPLACING it) and
; fill the viewer
; in:  SI -> song table: the message word, then one byte a note (SPEC.md 36:
;      note id in bits 0-4, duration / 3 - 1 in bits 5-6), 0FFh ends
; out: nothing; clobbers AX, BX, CX, DX, SI
;
; [pn_last] is left at the sum of the durations, which is the stamp the old
; synthesized timeline ended on - so a note played after the load records
; the same delta to the song's last note that it always did.
; -----------------------------------------------------------------------------
pn_load:
    cld
    lodsw                           ; AX = the message (DS:SI, the package's)
    push ax
    xor bx, bx                      ; BX = count
    xor dx, dx                      ; DX = running stamp
    mov cl, 5
.pair:
    lodsb
    cmp al, 0xFF
    je .done
    mov ah, al
    and al, 0x1F
    mov [pn_seqn+bx], al
    shr ah, cl                      ; AH = duration / 3 - 1
    inc ah
    mov al, ah
    add al, al
    add al, ah                      ; AL = duration, 3..12
    mov [pn_seqd+bx], al
    cbw
    add dx, ax
    inc bx
    jmp short .pair
.done:
    mov [pn_cnt], bx
    mov [pn_vcnt], bx
    mov [pn_last], dx
    call pn_vsync
    pop ax
    jmp short pn_say

; -----------------------------------------------------------------------------
; pn_note - play one note live: record it (full-stop at the cap), SOUND IT,
; then highlight the key, append to the viewer, hold, unhighlight
; in:  AL = note id 0..19; [pn_ox]/[pn_oy] stashed; gfx lock held
; out: nothing; preserves all registers (blocks ~4 ticks - SPEC.md 36)
;
; THE TONE COMES FIRST (SPEC.md 36). It used to wait behind the viewer and
; the key highlight - 40 to 90 ms of drawing on a 4.77 MHz 8088 between the
; key and the speaker - and nothing about the drawing needs to precede it:
; the tone is the kernel's from here on and expires on its own.
;
; What is recorded is the DELTA from the previous note, clamped to 2..24 now
; rather than at replay (SPEC.md 36): pn_seqd[i] is written when note i+1
; arrives, and only [pn_last] remembers a stamp.
; -----------------------------------------------------------------------------
pn_note:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = 1: the sequence is full, say so
    mov [pn_cur], al
    mov bx, [pn_cnt]
    cmp bx, PN_MAXN                 ; full-stop policy: keep sounding, stop
    jae .full                       ; recording, say so
    mov [pn_seqn+bx], al
    call OSAPI_GET_TICKS            ; AX = now
    xchg ax, [pn_last]              ; AX = the previous note's stamp
    dec bx                          ; BX = the previous note
    js .first                       ; ...there is none
    neg ax
    add ax, [pn_last]               ; AX = now - then: wrap-safe, the sign
    cmp ax, 2                       ; decides, and the clamp is 2..24 -
    jge .lo                         ; thinking pauses compressed to ~1.3 s
    mov al, 2
.lo:
    cmp ax, 24
    jle .hi
    mov al, 24
.hi:
    mov [pn_seqd+bx], al
.first:
    inc word [pn_cnt]
    mov ax, [pn_cnt]
    mov [pn_vcnt], ax
    jmp short .sound
.full:
    inc si
.sound:
    mov cx, PN_LIVEDUR
    call pn_tone                    ; self-expires via snd_tick (SPEC.md 34.3)
    mov ah, 1                       ; ...and the key lights while it sounds
    call pn_key_hl
    call pn_vsync                   ; the new note, appended
    or si, si
    jz .hold
    mov ax, pn_s_full
    call pn_say
.hold:
    call OSAPI_MOUSE                ; hold the highlight; a fresh press cuts
    mov [pn_base], al               ; the hold short (fast playing stays
    mov ax, PN_LIVEDUR-1            ; snappy; the tone expires on its own)
    call pn_wait
    call pn_key_off
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_play - light [pn_cur]'s key and start its tone (internal; replay's order)
; pn_tone - ...start the tone alone (live play's order: the tone FIRST)
; in:  CX = the tone's duration in ticks, [pn_cur] = the note
; out: AX, BX, DX clobbered
; -----------------------------------------------------------------------------
pn_play:
    mov ah, 1
    call pn_key_hl
pn_tone:
    mov bl, [pn_cur]
    xor bh, bh
    add bx, bx
    mov ax, [pn_freq+bx]
    mov dl, PN_PRIO
    call OSAPI_SND_TONE
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
    push cx                         ; OSAPI_MOUSE answers in CX/DX too
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
    stc                             ; a fresh press
    jnz .out
    and [pn_base], al               ; fold releases into the baseline
    call OSAPI_GET_TICKS
    sub ax, bx                      ; wrap-safe: the sign decides - which
    js .poll                        ; is also why CF needs setting: the sub
    clc                             ; borrows across the counter's wrap
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_key_hl - repaint [pn_cur]'s key in its sounding or normal state
; in:  AH = 1 highlight / 0 normal (pn_key_off: normal), [pn_cur] = the note
; out: nothing; preserves all registers (pn_key_off: AH = 0)
;
; A natural's diatonic step IS its white key's index, and a sharp's IS the
; slot of the black key on it - the white key below it (SPEC.md 36) - so
; [pn_step], which the viewer needs anyway, is the whole key lookup.
; -----------------------------------------------------------------------------
pn_key_off:
    xor ah, ah
pn_key_hl:
    push bx
    mov bl, [pn_cur]
    xor bh, bh
    cmp byte [pn_sharp+bx], 0
    mov bl, [pn_step+bx]            ; (mov keeps the flags)
    mov bh, ah
    jne .black
    call pn_draw_wkey
    pop bx
    ret
.black:
    call pn_draw_bkey
    pop bx
    ret

; -----------------------------------------------------------------------------
; pn_draw_keys - the whole keyboard: 12 whites, left to right, each drawing
; its LEFT black neighbour only - slot i-1, which white i-1 and white i have
; both covered by then - so every black key is drawn ONCE (SPEC.md 36). It
; was both neighbours, so eight black keys were drawn twice a paint.
; in:  [pn_ox]/[pn_oy] stashed
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_keys:
    push bx
    mov bx, PN_WK_LEFT << 8
.wk:
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
; in:  BL = white index 0..11, BH = 1 highlight, or PN_WK_LEFT: unlit, and
;      redraw the left neighbour only (pn_draw_keys' walk)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_wkey:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov bp, bx                      ; BP = state:index (register use only)
    mov al, 18
    mul bl                          ; AX = index*18
    add ax, 2
    mov di, ax                      ; DI = key left edge
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, PN_KB_Y1
    lea cx, [di+18]
    mov dx, [pn_kby2]
    call pn_cframe
    mov ax, bp                      ; AH = highlight state
    mov al, CWHITE
    test ah, 1
    jz .face
    mov al, CLBLUE                  ; the sounding fill
.face:
    call OSAPI_SET_COLOR
    mov ax, di
    inc ax
    mov bx, PN_KB_Y1+1
    lea cx, [di+17]
    mov dx, [pn_kby2]
    dec dx
    call pn_cfill
    mov ax, bp                      ; the map letter: CBLUE, CWHITE when lit
    mov al, CBLUE
    test ah, 1
    jz .lcol
    mov al, CWHITE
.lcol:
    call OSAPI_SET_COLOR
    mov bx, bp
    mov bh, 0
    mov al, [pn_wchar+bx]
    lea cx, [di+5]
    mov dx, [pn_kby2]               ; the letter rides PN_WLBL_UP above the
    sub dx, PN_WLBL_UP              ; key's foot, wherever that is
    call pn_cchar
    mov bx, bp                      ; re-draw the black keys overlapping
    xor bh, bh                      ; this white key: the ones on slots idx-1
    dec bx                          ; and idx, where there are any - and
    mov cx, 2                       ; neighbours re-draw un-highlighted (BH 0).
    mov ax, bp                      ; ...or slot idx-1 alone, in a whole
    test ah, PN_WK_LEFT             ; keyboard's walk
    jz .nb
    dec cx
.nb:                                ; Slot -1 is the 0 byte before [pn_bchs],
    cmp byte [pn_bchs+bx], 0        ; there for this; slot 11 is [pn_bchs]'s
    je .nbnext                      ; own last 0
    call pn_draw_bkey
.nbnext:
    inc bx
    loop .nb
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_bkey - one black key: black body (CLRED core with a 1px black rim
; when sounding) and its CWHITE map letter
; in:  BL = its SLOT (the white key it sits on: 0 1 3 4 5 7 8 10), BH = 1
;      highlight
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_draw_bkey:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov bp, bx                      ; BP = state:slot (register use only)
    mov al, 18
    mul bl
    add ax, 15                      ; AX = key left edge (18*slot + 15)
    mov di, ax                      ; DI = x1
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, PN_KB_Y1
    lea cx, [di+9]
    mov dx, [pn_bky2]
    call pn_cfill
    mov ax, bp
    or ah, ah
    jz .letter
    mov al, CLRED                   ; the sounding core
    call OSAPI_SET_COLOR
    mov ax, di
    inc ax
    mov bx, PN_KB_Y1+1
    lea cx, [di+8]
    mov dx, [pn_bky2]
    dec dx
    call pn_cfill
.letter:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, bp
    mov bh, 0
    mov al, [pn_bchs+bx]
    mov cx, di
    inc cx
    mov dx, [pn_bky2]
    sub dx, PN_BLBL_UP
    call pn_cchar
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_draw_viewer - the mini-staff, WHOLE: CBLUE frame, white field, 5 black
; staff lines, then every column through pn_vsync from an empty pn_vshow
; in:  [pn_ox]/[pn_oy], [pn_vcnt], the sequence
; out: nothing; preserves all registers
;
; W_PAINT's only (and pn_abdismiss's, through it). A NOTE never comes here:
; it is pn_vsync's, which touches the columns that changed and nothing else.
; -----------------------------------------------------------------------------
pn_draw_viewer:
    push ax
    push bx
    push cx
    push dx
    push di
    call pn_inkb
    mov ax, PN_VW_X1
    mov bx, PN_VW_Y1
    mov cx, PN_VW_X2
    mov dx, PN_VW_Y2
    call pn_cframe
    call pn_inkw
    mov ax, PN_VW_X1+1
    mov bx, PN_VW_Y1+1
    mov cx, PN_VW_X2-1
    mov dx, PN_VW_Y2-1
    call pn_cfill
    call pn_inkk
    mov dx, 16                      ; staff lines at y 16/22/28/34/40
.staff:
    mov ax, PN_ST_X1
    mov bx, PN_ST_X2
    call pn_chline
    add dx, 6
    cmp dx, 46
    jb .staff
    push es                         ; every column shows nothing now
    push ds
    pop es
    mov di, pn_vshow
    mov cx, PN_VCOLS
    mov al, PN_VNONE
    cld
    rep stosb
    pop es
    call pn_vsync
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_vsync - bring the viewer's columns to the last <=21 of the first
; [pn_vcnt] entries, touching ONLY the columns whose note changed
; in:  [pn_ox]/[pn_oy], [pn_vcnt], the sequence, pn_vshow
; out: pn_vshow updated; preserves all registers
;
; A live note is one column - or, once the viewer is full, the columns the
; scroll moved a DIFFERENT note into (a repeated note is skipped). A changed
; column's old marks are drawn over in exactly what lies under them and its
; new ones drawn, so nothing fills a ground first (PERFORMANCE.md rule 2):
; the staff and the other columns never blank. It used to fill the whole
; field white and redraw the staff and all 21 columns for every note.
;
; The marks, column c at colx = 8 + 10c, step s, y = 46 - 3s (SPEC.md 36):
;   L  ledger, s = 0:   hline colx+2..colx+11 at 46 - the one mark that
;                       reaches the NEXT column (and, at c = 20, the frame)
;   D  dot:             fill colx+5..colx+8, y-1..y+1
;   S  sharp:           vlines colx+1, colx+3, y-3..y+2; hlines
;                       colx..colx+4 at y-2 and y+1
; drawn L, D, S, columns left to right - so where they meet, a later one
; wins, and an erase restores what that order put there: a C#4's vline at
; colx+1 owns row 46 over the ledger of the column before it.
;
; Registers through the walk: SI = the first entry shown, DI = the column,
; BP = its x (a plain register, never dereferenced - SS != DS).
; -----------------------------------------------------------------------------
pn_vsync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, [pn_vcnt]               ; SI = first entry shown: the newest
    sub si, PN_VCOLS                ; PN_VCOLS of them
    jnc .first
    xor si, si
.first:
    xor di, di
    mov bp, 8
.col:
    call pn_vwant                   ; AL = what column DI should show
    mov ah, [pn_vshow+di]           ; AH = what it shows
    cmp al, ah
    je .next
    mov [pn_vshow+di], al
    xchg dx, ax                     ; DL = new, DH = old
    cmp dh, PN_VNONE
    je .put                         ; nothing to take down
    cmp dl, PN_VNONE
    je .rub                         ; nothing to put up
    mov bl, dl
    xor bh, bh
    mov cl, [pn_step+bx]
    mov bl, dh
    cmp cl, [pn_step+bx]
    jne .rub
    mov byte [pn_vkeep], 1          ; SAME STEP: a natural and its sharp, so
    mov al, dh                      ; the ledger and the dot stay and only
    cmp byte [pn_sharp+bx], 0       ; the sharp glyph comes or goes
    jne .unsharp
    mov al, dl
    call pn_vputs
    jmp short .next
.unsharp:
    call pn_vsrubn
    jmp short .next
.rub:
    mov byte [pn_vkeep], 0
    mov al, dh
    push dx
    call pn_vrub
    pop dx
    cmp dl, PN_VNONE
    je .next
.put:
    mov al, dl
    call pn_vput
.next:
    inc di
    add bp, 10
    cmp di, PN_VCOLS
    jb .col
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pn_vwant - AL = the note column DI should show, or PN_VNONE (SI = first);
; BX clobbered
pn_vwant:
    mov bx, si
    add bx, di
    mov al, PN_VNONE
    cmp bx, [pn_vcnt]
    jae .r
    mov al, [pn_seqn+bx]
.r:
    ret

; pn_vinfo - AL = a note: [pn_vyc] = its y, [pn_vrs] = the staff row inside
; its sharp's y-3..y+2 (0 for none - there is at most one, and it is y or
; y-3), [pn_vstp] = its step, [pn_vsh] = sharp; AX, BX clobbered
pn_vinfo:
    mov bl, al
    xor bh, bh
    mov al, [pn_sharp+bx]
    mov [pn_vsh], al
    mov al, [pn_step+bx]
    mov [pn_vstp], al
    cbw
    mov bx, 46
    sub bx, ax
    sub bx, ax
    sub bx, ax
    mov [pn_vyc], bx
    test al, 1
    jnz .odd
    or al, al                       ; even: the staff row is y itself - but
    jnz .row                        ; step 0's y is 46, which is no staff line
    jmp short .none
.odd:
    sub bx, 3                       ; odd: y-3, up to step 9 (row 16)
    cmp al, 9
    jbe .row
.none:
    xor bx, bx
.row:
    mov [pn_vrs], bx
    ret

; -----------------------------------------------------------------------------
; pn_vput - draw note AL's marks in column DI (L, D, S - the full paint's
; order); pn_vputs - its sharp glyph alone. AX, BX, CX, DX clobbered
; -----------------------------------------------------------------------------
pn_vputs:
    call pn_vinfo
    jmp short pn_vput.s
pn_vput:
    call pn_vinfo
    cmp byte [pn_vstp], 0
    jne .d
    call pn_inkk                    ; middle C: the ledger line, to colx+11 -
    lea bx, [bp+11]                 ; or colx+10 when the next column will
                                    ; show a C#4, whose vline owns that pixel
    cmp di, PN_VCOLS-1
    jae .lx
    inc di
    push bx
    call pn_vwant
    pop bx
    dec di
    cmp al, 1
    jne .lx
    dec bx
.lx:
    lea ax, [bp+2]
    mov dx, 46
    call pn_chline
.d:
    call pn_inkb
    lea ax, [bp+5]                  ; the dot: 4x3 at (colx+5, y-1)
    lea cx, [bp+8]
    mov dx, [pn_vyc]
    mov bx, dx
    dec bx
    inc dx
    call pn_cfill
.s:
    cmp byte [pn_vsh], 0
    je .r
    mov al, CLRED                   ; the small sharp glyph
    call OSAPI_SET_COLOR
    mov dx, [pn_vyc]                ; vlines at colx+1 and colx+3, y-3..y+2
    lea ax, [bp+1]
    mov bx, dx
    sub bx, 3
    inc dx
    inc dx
    call pn_cvline
    inc ax
    inc ax
    call pn_cvline
    mov ax, bp                      ; hlines colx..colx+4 at y-2 and y+1
    lea bx, [bp+4]
    sub dx, 4
    call pn_chline
    add dx, 3
    call pn_chline
.r:
    ret

; -----------------------------------------------------------------------------
; pn_vrub - draw over note AL's marks in column DI with what lies under them
; (the new note is a different step, or none); pn_vsrubn - its sharp glyph
; alone, [pn_vkeep] = 1 (the same step's ledger and dot stay). AX..DX
; clobbered
; -----------------------------------------------------------------------------
pn_vrub:
    call pn_vinfo
    cmp byte [pn_vstp], 0
    jne .d
    call pn_inkw                    ; the ledger: white, but not the pixel a
    lea ax, [bp+2]                  ; C#4 in the next column owns - and at
                                    ; the last column its end is the FRAME
    lea bx, [bp+11]
    mov dx, 46
    cmp di, PN_VCOLS-1
    jb .lmid
    dec bx
    call pn_chline
    call pn_inkb
    mov ax, PN_VW_X2
    mov bx, ax
    call pn_chline
    jmp short .d
.lmid:
    cmp byte [pn_vshow+di+1], 1
    jne .lh
    dec bx
.lh:
    call pn_chline
.d:
    call pn_inkw
    lea ax, [bp+5]
    mov dx, [pn_vyc]
    cmp dx, [pn_vrs]
    je .online
    lea cx, [bp+8]                  ; off the staff: white, one fill
    mov bx, dx
    dec bx
    inc dx
    call pn_cfill
    jmp short .s
.online:                            ; centred on a staff line: the rows above
    lea bx, [bp+8]                  ; and below white, its own row the line's
    dec dx                          ; black - which ends at x 213, inside the
    call pn_chline                  ; last column's dot
    add dx, 2
    call pn_chline
    dec dx
    call pn_inkk
    cmp di, PN_VCOLS-1
    jb .blk
    mov bx, PN_ST_X2
    call pn_chline
    call pn_inkw
    mov ax, PN_ST_X2+1
    lea bx, [bp+8]
.blk:
    call pn_chline
.s:
    cmp byte [pn_vsh], 0
    je pn_vsrub.r
    jmp short pn_vsrub
pn_vsrubn:
    call pn_vinfo
pn_vsrub:
    call pn_inkw                    ; the two hlines are never on a staff
                                    ; line (y-2 and y+1 against y = 46-3s)
    mov ax, bp
    lea bx, [bp+4]
    mov dx, [pn_vyc]
    dec dx
    dec dx
    call pn_chline
    add dx, 3
    call pn_chline
    mov cx, [pn_vrs]                ; vline colx+1: the staff row it crosses,
    cmp byte [pn_vstp], 0           ; or - for a C#4 - row 46, which is the
    jne .v1                         ; ledger's of the column before if that
    or di, di                       ; one is middle C
    jz .v1
    cmp byte [pn_vshow+di-1], 1
    ja .v1
    mov cx, 46
.v1:
    lea ax, [bp+1]
    call pn_vcol
    mov cx, [pn_vrs]                ; vline colx+3: likewise, and row 46 is
    cmp byte [pn_vstp], 0           ; this column's OWN ledger if it stays
    jne .v3
    cmp byte [pn_vkeep], 0
    je .v3
    mov cx, 46
.v3:
    lea ax, [bp+3]
    call pn_vcol
.r:
    ret

; pn_vcol - a sharp's vline at x AX, rows y-3..y+2, drawn over in white
; except row CX (0 = none), which is black: a staff line or a ledger. Each
; pixel is written once. AX..DX clobbered
pn_vcol:
    call pn_inkw
    mov bx, [pn_vyc]
    mov dx, bx
    sub bx, 3
    inc dx
    inc dx
    jcxz .all
    push dx
    mov dx, cx                      ; above the black row
    dec dx
    cmp dx, bx
    jl .na
    call pn_cvline
.na:
    pop dx
    mov bx, cx                      ; below it
    inc bx
    cmp bx, dx
    jg .nb
    call pn_cvline
.nb:
    call pn_inkk
    mov bx, cx
    mov dx, cx
.all:
    jmp pn_cvline

; pn_inkw / pn_inkk / pn_inkb - the pen white / black / blue, AX kept (the
; viewer's erases change pen a dozen times; this is 3 bytes a change)
pn_inkw:
    push ax
    mov al, CWHITE
    jmp short pn_ink
pn_inkk:
    push ax
    mov al, CBLACK
    jmp short pn_ink
pn_inkb:
    push ax
    mov al, CBLUE
pn_ink:
    call OSAPI_SET_COLOR
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_onup - W_ONMOUSEUP (SPEC.md 13.7): the five buttons fire HERE
; in:  CX = x, DX = y (SCREEN), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
pn_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    xchg di, ax
    call pn_pt                  ; CX/DX content-relative
    call pn_bhit
    push ax
    xor al, al                  ; upright FIRST whatever happens: a cancel
    call pn_setdown             ; wants it, and pn_bact may play a whole song
    pop ax                      ; with the button not held down for it
    xor ah, ah
    cmp ax, di                  ; a different button, or none: CANCELLED
    jne .out
    call pn_bact
.out:
    jmp pn_pop6

; -----------------------------------------------------------------------------
; pn_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press still down
; -----------------------------------------------------------------------------
pn_ondrag:
    push ax
    push bx
    push cx
    push dx
    push di
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    xchg di, ax
    call pn_pt
    call pn_bhit
    xor ah, ah
    cmp ax, di
    je .set
    xor al, al                  ; off it: nothing is down
.set:
    call pn_setdown             ; ...which draws only if that CHANGED
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pn_pt - a SCREEN point to a content-relative one (internal)
; in:  CX = x, DX = y, SI = the window
; out: CX/DX content-relative, [pn_ox]/[pn_oy] stashed; AX, BX clobbered
; -----------------------------------------------------------------------------
pn_pt:
    push dx
    call pn_org
    pop dx
    sub cx, ax
    sub dx, [pn_oy]
    ret

; --- SPEC.md 13.8: the five buttons, ONE description --------------------------
; They were five literal argument sets in the painter against five bands of
; literals in pn_onclick - two descriptions of one row of buttons, agreeing by
; hand, which is SPEC.md 22's fm_hit discipline broken in a package. The table
; is both now, and it is also what lets ONE button be redrawn for the down
; state instead of the whole pair of rows.
;
; It is the shared control's own record (SPEC.md 20.5.1.3.2), all five: the
; INCLUSIVE rects in CONTENT coordinates, which is what os88ui_bhit hit-tests
; (a content-relative point against a content-relative rect is the same
; question), the labels, and the flag words - the three coloured captions'
; OS88UI_INK with the colour in the high byte (SPEC.md 47: decoration, not
; state). The one thing the control cannot take as it stands is the rect,
; which it draws in SCREEN coordinates: pn_btn1 stages the one being drawn.
PN_NBTN     equ 5
%define PN_BR(x, y, w) dw x, y, x+w-1, y+PN_BTN_H-1
pn_brc:
    PN_BR(2,   PN_ROW1_Y, 56)       ; REPLAY
    PN_BR(60,  PN_ROW1_Y, 48)       ; CLEAR
    PN_BR(52,  PN_ROW2_Y, 64)       ; TWINKLE
    PN_BR(118, PN_ROW2_Y, 40)       ; ODE
    PN_BR(160, PN_ROW2_Y, 48)       ; MARY
pn_blabels:
    dw pn_l_replay, pn_l_clear, pn_l_twk, pn_l_ode, pn_l_mary
pn_bflags:                          ; black is the control's own ink, so the
    dw 0, 0                         ; two plain buttons ask for none at all
    dw OS88UI_INK | (CGREEN << 8)
    dw OS88UI_INK | (CMAGENTA << 8)
    dw OS88UI_INK | (CRED << 8)
    OS88UI_BTNREC pn_btrec, 0, pn_blabels, pn_bflags, PN_NBTN
; Which button is DRAWN pressed, 1-based, 0 = none - and it IS the record's
; OS88UI_BT_DOWN, so os88ui_btn folds the pressed look in itself and a W_PAINT
; mid-gesture agrees with it for free (SPEC.md 13.8). Under OS88UI_NOGEST the
; library never writes the word (no os88ui_btninit, no gesture), which is
; what makes it the package's to write - Word does the same (word.asm).
pn_down     equ pn_btrec + OS88UI_BT_DOWN

; -----------------------------------------------------------------------------
; pn_btn1 - draw button AL (1..PN_NBTN), pressed if it is the held one
; in:  AL = the button; [pn_ox]/[pn_oy] stashed
; out: nothing; preserves all registers
;
; The drawing is os88ui_btn's (apps/os88ui.inc) and the record is pn_btrec;
; what is adapted here is the ORIGIN - Piano works in content-relative
; coordinates and the shared control takes screen ones. The rect is staged
; in [pn_brect] and the record's rect pointer aimed so that THIS index lands
; on it: RECTS + (AL-1)*8 = pn_brect.
;
; THE COLOUR IS WHY THIS CONVERSION NEEDED A FEATURE. Three of the five
; buttons are CGREEN/CMAGENTA/CRED - SPEC.md 47's own example of a colour
; that is decoration rather than state - and the shared control knew only
; live and disabled, so converting as-is would have turned them black on
; VGA. OS88UI_INK is that, and it is the first feature added to the shared
; control since it existed: one edit, and the file dialog, the Control
; Panel, the Timer and every package can have a coloured caption.
; -----------------------------------------------------------------------------
pn_btn1:
    push bx
    push cx
    push si
    push ax
    xor ah, ah
    xchg bx, ax
    dec bx
    mov cl, 3
    shl bx, cl                      ; BX = the entry's offset, (AL-1)*8
    lea si, [pn_brc+bx]
    neg bx
    add bx, pn_brect
    mov [pn_btrec+OS88UI_BT_RECTS], bx
    mov bx, pn_brect
    mov cx, 2                       ; two corners, each (x, y) + the origin
.corner:
    lodsw
    add ax, [pn_ox]
    mov [bx], ax
    lodsw
    add ax, [pn_oy]
    mov [bx+2], ax
    add bx, 4
    loop .corner
    pop ax
    mov bx, pn_btrec
    call os88ui_btn                 ; AL = the index PLUS ONE, as it takes
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pn_bhit - which button is this CONTENT-relative point in? (0 = none)
; in:  CX = x, DX = y (content-relative)
; out: AL = 1..PN_NBTN or 0; AH clobbered
; -----------------------------------------------------------------------------
pn_bhit:
    push bx
    mov bx, pn_brc
    mov al, 1
.next:
    call os88ui_bhit                ; CF = 0 inside
    jnc .out
    add bx, 8
    inc ax
    cmp al, PN_NBTN
    jbe .next
    xor al, al
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; pn_setdown - AL becomes the button drawn pressed (0 = none), and it DRAWS
; -----------------------------------------------------------------------------
; Only if it CHANGED. The old one is redrawn first and is no longer the record's
; down one by then, so it comes back upright; os88ui_btn draws nothing for 0.
pn_setdown:
    push ax
    xor ah, ah
    cmp ax, [pn_down]
    je .out
    xchg ax, [pn_down]
    call pn_btn1                    ; the old one, upright
    mov ax, [pn_down]
    call pn_btn1                    ; the new one, pressed
.out:
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
    push cx
    push dx
    push si
    mov al, 1
.each:
    call pn_btn1
    inc ax
    cmp al, 2
    jbe .each
    call pn_draw_msg
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, pn_l_songs
    mov cx, 2
    mov dx, PN_ROW2_Y+4
    call pn_cstr
    mov al, 3
.each2:
    call pn_btn1
    inc ax
    cmp al, PN_NBTN
    jbe .each2
    pop si
    pop dx
    pop cx
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
    call OSAPI_FONT_CHAR_XPARENT
    pop dx
    pop cx
    ret

pn_cstr:                            ; SI=string CX=x DX=y (color set)
    push cx
    push dx
    add cx, [pn_ox]
    add dx, [pn_oy]
    call OSAPI_FONT_STR_XPARENT
    pop dx
    pop cx
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; =============================================================================
; 'About Piano' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; The card is os88ui.inc's; what is here is the flag, the painter drawing it
; last, and the three handlers taking it down. State, not a modal loop: a
; loop would hold the gfx lock against every background task for as long as
; the reader left the credits up.

; -----------------------------------------------------------------------------
; pn_about - the OSAPI_ABOUT_SET handler (slot 0x018A)
; in:  SI = our window ptr; the UI task, gfx lock HELD
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pn_about:
    push bx
    push si
    mov byte [pn_abon], 1
    mov bx, si
    mov si, pn_ablines
    call os88ui_about               ; arms the clip itself: a menu dispatch
    pop si                          ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; pn_abdismiss - take the card down if it is up
; in:  SI = our window ptr; gfx lock held
; out: CF = 1 the click or key was spent doing it; preserves every register
;
; The way back is the whole content, which is what pn_paint draws: the card
; covered the viewer, the buttons and the keyboard.
; -----------------------------------------------------------------------------
pn_abdismiss:
    cmp byte [pn_abon], 0
    je .none
    push bx
    mov byte [pn_abon], 0
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; nothing armed a region for a click or a
    jc .gone                        ; key (SPEC.md 11.3)
    call pn_paint                   ; ...and the card is down, so this is the
.gone:                              ; three parts it covered, at the origin the
    pop bx                          ; window has NOW - it may have been dragged
    stc                             ; since the card went up
    ret
.none:
    clc
    ret

; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
; SHORT: the card is clamped to the content box and this window's is 222px on
; VGA and narrower on a CGA, so the credit is two lines rather than one.
pn_ablines:
    dw pn_ab1, pn_ab2, pn_ab3, pn_ab4, 0
pn_ab1:     db 'Piano for os8088', 0
pn_ab2      equ $ - 1               ; the empty line: pn_ab1's own NUL
pn_ab3:     db 'Contributed by', 0
pn_ab4:     db 'Jorge Gonzalez', 0

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
; The keys are identified by POSITION, which is all the drawing and the hit
; test need: a white key by its index 0..11 (x = 2 + 18i), a black one by its
; SLOT, the white key it sits on (x = 18*slot + 15). A note's [pn_step] is
; both - a natural's diatonic step IS its white index and a sharp's IS its
; slot - and a black key's note is its slot's white note plus one. So two
; 12-byte letter tables replace five tables of key indices, and the QWERTY
; map pn_onkey walks is those same letters (SPEC.md 36).
pn_wchar:                           ; white key letter by index - and pn_onkey
    db 'asdfghjkl;', 0x27, ']'      ; walks pn_wchar..pn_bchs as one map
    db 0                            ; "slot -1" to pn_draw_wkey's neighbour
                                    ; walk: no black key left of the first
pn_bchs:                            ; black key letter by SLOT; 0 = no key there
    db 'w', 'e', 0, 't', 'y', 'u', 0, 'o', 'p', 0, '[', 0
PN_KMAPN    equ $ - pn_wchar        ; 25: the map, holes and all
pn_wnote:                           ; white key index -> note id
    db 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19

; --- embedded songs: the message word, then one byte a note, 0FFh ends --------
; (SPEC.md 36) note id in bits 0-4, duration / 3 - 1 in bits 5-6: quarter = 6
; ticks (~1/3 s), half = 12, and 3/9 for the dotted pairs. PN_SONG takes the
; (note, duration) pairs the tables have always been written as and refuses
; a duration the byte cannot hold.
%macro PN_SONG 2-*
  %rep %0 / 2
    %if (%2) != 3 && (%2) != 6 && (%2) != 9 && (%2) != 12
      %error "PN_SONG: a duration is 3, 6, 9 or 12 ticks"
    %endif
    %if (%1) > 19
      %error "PN_SONG: a note id is 0..19"
    %endif
    db (%1) | (((%2) / 3 - 1) << 5)
    %rotate 2
  %endrep
%endmacro
pn_songs:   dw pn_song_twk, pn_song_ode, pn_song_mary

pn_song_twk:                        ; Twinkle Twinkle Little Star (42 notes)
    dw pn_s_twk
    PN_SONG 0,6, 0,6, 7,6, 7,6, 9,6, 9,6, 7,12
    PN_SONG 5,6, 5,6, 4,6, 4,6, 2,6, 2,6, 0,12
    PN_SONG 7,6, 7,6, 5,6, 5,6, 4,6, 4,6, 2,12
    PN_SONG 7,6, 7,6, 5,6, 5,6, 4,6, 4,6, 2,12
    PN_SONG 0,6, 0,6, 7,6, 7,6, 9,6, 9,6, 7,12
    PN_SONG 5,6, 5,6, 4,6, 4,6, 2,6, 2,6, 0,12
    db 0xFF

pn_song_ode:                        ; Ode to Joy, Beethoven (30 notes)
    dw pn_s_ode
    PN_SONG 4,6, 4,6, 5,6, 7,6, 7,6, 5,6, 4,6, 2,6
    PN_SONG 0,6, 0,6, 2,6, 4,6, 4,9, 2,3, 2,12
    PN_SONG 4,6, 4,6, 5,6, 7,6, 7,6, 5,6, 4,6, 2,6
    PN_SONG 0,6, 0,6, 2,6, 4,6, 2,9, 0,3, 0,12
    db 0xFF

pn_song_mary:                       ; Mary Had a Little Lamb (26 notes)
    dw pn_s_mary
    PN_SONG 4,6, 2,6, 0,6, 2,6, 4,6, 4,6, 4,12
    PN_SONG 2,6, 2,6, 2,12, 4,6, 7,6, 7,12
    PN_SONG 4,6, 2,6, 0,6, 2,6, 4,6, 4,6, 4,6, 4,6
    PN_SONG 2,6, 2,6, 4,6, 2,6, 0,12
    db 0xFF

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT            ; ...and the standard About card beside the
%define OS88UI_NOGLYPH          ; no check box or radio drawn here (SPEC.md 13.8.9)
%define OS88UI_NOGEST           ; presses routed by hand: no gesture install (SPEC.md 20.5.1.3.4)
%define OS88UI_NOBFIND          ; hit-tests its own rects: no os88ui_bfind (SPEC.md 20.5.1.3.4)
%include "os88ui.inc"           ; buttons this already drew

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
pn_vsh   equ os88_image_end + 12    ; byte: viewer scratch - sharp flag
pn_abon  equ os88_image_end + 13    ; byte: the About card is up
; --- the keyboard's depth, derived from the LIVE content box (SPEC.md 11.98) --
pn_kby2  equ os88_image_end + 14    ; word: the white keys' last row
pn_bky2  equ os88_image_end + 16    ; word: ...and the black keys'
                                    ; (the letters' rows are these less
                                    ; PN_WLBL_UP / PN_BLBL_UP, at the draw)
pn_last  equ os88_image_end + 18    ; word: the last recorded note's stamp
pn_brect equ os88_image_end + 20    ; 4 words: the button being drawn, in
                                    ;       SCREEN coordinates (pn_btn1)
pn_seqn  equ os88_image_end + 28    ; 300 bytes: note ids
pn_seqd  equ os88_image_end + 328   ; 300 bytes: tick delta to the NEXT note,
                                    ;       clamped 2..24 (SPEC.md 36)
; --- pn_vsync's: what the viewer SHOWS, and one column's scratch -------------
pn_vshow equ os88_image_end + 628   ; 21 bytes: the note each column shows,
                                    ;       PN_VNONE for none (W_PAINT sets
                                    ;       them all before any note can come)
pn_vstp  equ os88_image_end + 649   ; byte: the column's note's step
pn_vyc   equ os88_image_end + 650   ; word: ...its dot's centre y
pn_vrs   equ os88_image_end + 652   ; word: ...the staff row its sharp's
                                    ;       vlines cross, 0 for none
pn_vkeep equ os88_image_end + 654   ; byte: the column keeps its ledger
                                    ; total 655 = PN_BSS_TOTAL
%if pn_vkeep + 1 - os88_image_end != PN_BSS_TOTAL
  %error "PN_BSS_TOTAL does not match the bss layout"
%endif
