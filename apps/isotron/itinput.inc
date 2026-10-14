; =============================================================================
; apps/isotron/itinput.inc - keys, the turn FIFO and the UI task's mailbox
; (SPEC.md 108.7.1)
;
; TWO TASKS, ONE PRODUCER EACH WAY. W_ONKEY runs on the UI task; the game
; runs on the worker. Turns go through a 3-deep single-producer single-
; consumer ring: the producer writes the slot and THEN its head byte, the
; consumer reads the slot and THEN advances its tail byte, and neither ever
; writes the other's byte - so there is no lock, which matters because a
; worker may not take one (SPEC.md 20.6 rule 7 is this handshake's shape).
; A turn is popped once per PLAYER CELL (itrules.inc), so a fast double tap
; between two cells is two turns - a U-turn - and is never lost.
;
; Boosts are a count the producer increments and the consumer subtracts what
; it took (one read-modify-write instruction each, so neither can tear); F9
; is a flag the consumer takes with XCHG. Commands (start, pause, Esc,
; continue) are flag bytes the worker acts on inside its own lock hold, so
; the game state has exactly one writer.
;
; KEYS BY SCANCODE, LETTERS BY ASCII (SPEC.md 67.11.1's lesson): an extended
; key is tested on AH before anything looks at AL, and AL is never case-
; folded first - `or al, 20h` turns an extended key's 0 into a space. Keypad
; Ins with NumLock on arrives as AL = '0', AH = 52h, and is still Ins.
; =============================================================================

KSC_INS     equ 0x52
KSC_DEL     equ 0x53
KSC_F9      equ 0x43
KSC_F10     equ 0x44

; -----------------------------------------------------------------------------
; it_key - one keystroke from W_ONKEY. AL = ascii, AH = scan. Lock held.
; Clobbers AX, BX.
; -----------------------------------------------------------------------------
it_key:
    cmp ax, KEY_ALTENTER
    je .out                         ; full screen is wave 3's (the menu item
                                    ; says so); the chord is armed already
    cmp byte [it_state], ST_TITLE   ; AT THE TITLE a digit in AL is a game,
    jne .scan                       ; whatever AH says: keypad 1/2/3 with
    cmp al, '1'                     ; NumLock arrive as '1' + 4Fh, '2' + 50h
    jb .scan                        ; (= Down, normal speed) and '3' + 51h,
    cmp al, '3'                     ; and all three must start a game
    jbe .digit
.scan:
    cmp ah, KSC_INS
    je .left
    cmp ah, KSC_LEFT
    je .left
    cmp ah, KSC_DEL
    je .right
    cmp ah, KSC_RIGHT
    je .right
    cmp ah, KSC_F10
    je .boost
    cmp ah, KSC_UP
    je .boost
    cmp ah, KSC_F9
    je .normal
    cmp ah, KSC_DOWN
    je .normal
    cmp ah, KSC_ESC
    je .esc
    cmp ah, KSC_SPACE
    je .cont
    cmp ah, KSC_ENTER
    je .cont
    or al, al
    jz .out                         ; any other extended key: nothing
    cmp byte [it_state], ST_TITLE
    jne .ingame
    ; --- at the title: 1 2 3 with sound, A B C silent ---------------------------
    cmp al, '1'
    jb .ingame
    cmp al, '3'
    jbe .digit
    mov bl, al
    or bl, 0x20                     ; a letter, so folding is safe now
    cmp bl, 'a'
    jb .ingame
    cmp bl, 'c'
    ja .ingame
    sub bl, 'a' - 1                 ; 1..3, silent
    mov [it_qstart], bl
    ret
.digit:
    sub al, '0'
    or al, 0x80                     ; with sound
    mov [it_qstart], al
    ret
.ingame:
    mov bl, al
    or bl, 0x20
    cmp bl, 'z'
    je .left
    cmp bl, 'x'
    je .right
    cmp bl, 'p'
    je .pause
.out:
    ret
.left:
    mov al, K_LEFT
    jmp short .turn
.right:
    mov al, K_RIGHT
.turn:
    call it_turn_ok
    jc .out
    call it_fifo_push
    ret
.boost:
    call it_turn_ok
    jc .out
    inc byte [it_kboost]            ; each keystroke, typematic repeats too: a
    ret                             ; held F10 ramps the speed (SPEC.md 108.4.1)
.normal:
    call it_turn_ok
    jc .out
    mov byte [it_kf9], 1
    ret
.pause:
    mov byte [it_qpause], 1
    ret
.esc:
    mov byte [it_qesc], 1
    ret
.cont:
    mov byte [it_qcont], 1
    ret

; -----------------------------------------------------------------------------
; it_turn_ok - CF = 0 when a steering key may be posted: a round in play, not
; paused, and not a harness game whose bot is the player (it is then the
; FIFO's only producer)
; -----------------------------------------------------------------------------
it_turn_ok:
    cmp byte [it_state], ST_PLAY
    jne .no
    cmp byte [it_paused], 0
    jne .no
    test byte [it_tflags], IT_TF_BOT
    jnz .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; it_fifo_push - post a turn (AL = K_LEFT / K_RIGHT). A full ring drops the
; newest. The PRODUCER's half: slot first, head byte last. Preserves all.
; -----------------------------------------------------------------------------
it_fifo_push:
    push ax
    push bx
    mov bl, [it_fhead]
    mov ah, bl
    sub ah, [it_ftail]
    cmp ah, FIFO_N
    jae .full
    and bx, 3
    mov [it_fifo + bx], al
    inc byte [it_fhead]
.full:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_fifo_pop - take the oldest turn. out: CF = 1 empty, else AL = the turn.
; The CONSUMER's half: slot first, tail byte last. Preserves all but AL.
; -----------------------------------------------------------------------------
it_fifo_pop:
    push bx
    mov bl, [it_ftail]
    cmp bl, [it_fhead]
    je .empty
    and bx, 3
    mov al, [it_fifo + bx]
    inc byte [it_ftail]
    pop bx
    clc
    ret
.empty:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; it_commands - act on what the UI task posted, inside the worker's lock hold
; (the game state's one writer). ES = the claim.
; -----------------------------------------------------------------------------
it_commands:
    xor al, al
    xchg al, [it_qstart]
    or al, al
    jz .esc
    mov ah, al                      ; a new game: tier in bits 0-1, sound bit 7
    and al, 3
    rol ah, 1
    and ah, 1
    mov [it_snd], ah
    xor ah, ah                      ; the clock's seed, not the harness's
    call it_game_start
.esc:
    xor al, al
    xchg al, [it_qesc]
    or al, al
    jz .hold
    cmp byte [it_state], ST_TITLE
    je .hold                        ; Esc at the title does nothing windowed
    call it_to_title                ; ...anywhere else it ends the game
.hold:
    xor al, al
    xchg al, [it_qhold]             ; the About card: pause, never resume
    or al, al
    jz .pause
    cmp byte [it_state], ST_PLAY
    jne .pause
    cmp byte [it_paused], 0
    jne .pause
    mov byte [it_paused], 1
    mov byte [it_msgd], 1
.pause:
    xor al, al
    xchg al, [it_qpause]
    or al, al
    jz .cont
    cmp byte [it_state], ST_PLAY
    jne .cont
    xor byte [it_paused], 1
    mov byte [it_msgd], 1
.cont:
    xor al, al
    xchg al, [it_qcont]
    or al, al
    jz .out
    cmp byte [it_paused], 0
    je .wait
    mov byte [it_paused], 0         ; Space resumes a pause as P does
    mov byte [it_msgd], 1
    ret
.wait:
    cmp byte [it_state], ST_CRASH
    jne .wt
    mov byte [it_qcont], 1          ; Space during the 0.8 s of CRASH is kept,
    ret                             ; not lost: WAIT takes it on arrival
.wt:
    cmp byte [it_state], ST_WAIT
    jne .over
    call it_round_start
    ret
.over:
    cmp byte [it_state], ST_OVER
    jne .out
    call it_to_title
.out:
    ret
