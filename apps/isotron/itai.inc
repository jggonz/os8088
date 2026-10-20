; =============================================================================
; apps/isotron/itai.inc - the computer, and the harness's player (SPEC.md 108.6)
;
; tools/isotronsim.py's ai_move is NORMATIVE and this is its transcription:
; the same rules in the same order, the same caps on every run, so the two
; agree decision for decision. The first rule that turns ends the decision:
;
;   1. OPENING   the opening head, not yet turned, heading E or W, leg = 32,
;                and the way S is more than LOOK free: turn S
;   2. BLOCKED   the way ahead is LOOK or less: turn to the side with the
;                longer run; a tie goes by r & 1 (0 = left); both 0 = straight
;   3. U-TURN    one armed by a wander counts down; at 0 it turns that side
;                again if more than 16 is free there
;   4.           a leg under 8 goes straight
;   5. INTERCEPT r < ICPT: a side whose ray crosses the player's forward ray
;                at M, t cells for us and k for the player, with
;                t + 2 < ceil(6k / p) <= 120 and the t cells free: turn there
;   6. WANDER    (r XOR A5h) < WANDER: turn side r & 1 (1 = left) if more
;                than 16 is free, and arm a U-turn 8 + (r & 7) moves on
;
; Register contract: DS = the package, ES = the claim.
; =============================================================================

; -----------------------------------------------------------------------------
; it_run - free cells from grid index SI heading DL, at most CX.
; it_run_hd - the same from the head at BX (its H_IDX), one call level less.
; out: AX = the count; CX is CLOBBERED (what the scan left - a caller asking
; twice sets the cap twice). Preserves everything else; DF is left clear.
;
; MEASURED on MartyPC's 5150, minima per count: the first version's 13-byte
; loop was fetch-bound at 195 + 82.5 cycles a cell. Now a N/S run is a
; 7-byte `add / cmp / loope` loop, 250 + 54 a cell - or, when the cap is one
; the ring always beats (101 or more), an unrolled scan at ~25 a cell and one
; DIV for the count (wave 2) - and an E/W run - the grid's own byte order -
; is one `repe scasb`, DF set for W, 228 + 15.6 a cell. Either way the count is the cap less what CX has left, less one when
; the scan stopped ON a wall (ZF = 0): a scan that ran out with ZF = 1 found
; every one of its cap cells free. (SPEC.md 108.6 has the worst decision.)
; -----------------------------------------------------------------------------
it_run_hd:
    push si
    mov si, [bx + H_IDX]
    jmp short it_run_go
it_run:
    push si
it_run_go:
    push di
    push bx
    mov bx, cx                      ; BX = the cap
    xor ax, ax                      ; AL = 0, the free cell's value
    jcxz .out                       ; AX = 0
    test dl, 1
    jnz .ew
    mov di, GW                      ; N: +217...
    test dl, 2
    jz .ns
    neg di                          ; ...S: -217
.ns:
    add si, IT_GRID
    cmp bx, GH                      ; A CAP THE RING BEATS: a N/S run meets the
    jae .nsf                        ; outline within 99 cells, so a cap of 101
.lp:                                ; or more (rule 2's 255) never binds
    add si, di
    cmp [es:si], al
    loope .lp
    jmp short .tally

.ew:
    lea di, [si + IT_GRID + 1]      ; E: the next byte up...
    test dl, 2
    jz .scan
    sub di, 2                       ; ...W: the next byte down, scanning down
    std
.scan:
    repe scasb
    cld
.tally:
    mov ax, bx                      ; (mov, jz, cld and pop leave ZF alone)
    jz .all
    dec ax                          ; the cell that stopped it is not free
.all:
    sub ax, cx
.out:
    pop bx
    pop di
    pop si
    ret
.nsf:
    ; ...and needs no counter: an unrolled scan to the first set cell (7
    ; bytes, ~25 cycles a cell against the loop's 54) and the count from how
    ; far it went, one DIV. Rule 2 on a blocked E/W head measures both its
    ; N/S sides to the ring this way (SPEC.md 108.10.3's head-step).
    push dx
    mov cx, si                      ; CX = the head's cell
.nf:
%rep 8
    add si, di
    cmp [es:si], al
    jne .nhit
%endrep
    jmp short .nf
.nhit:
    mov ax, si
    sub ax, cx                      ; +-217 * (free cells + 1)
    jns .npos
    neg ax
.npos:
    xor dx, dx
    mov cx, GW
    div cx
    dec ax                          ; AX = the free cells
    pop dx
    jmp short .out

; -----------------------------------------------------------------------------
; LOOK_EW n - rule 2's look-ahead for an E or W head (DL = its heading, SI =
; its cell, AX = 0): ANY of the n cells ahead taken jumps to .bl, else on to
; .upn. An E/W run is the grid's own byte order, so the cells go a WORD at a
; time - E the pairs from SI + 1 up, W from SI - 2 down - and an odd n ends
; on a byte: half the compares of the N/S cell-by-cell walk, the same cells
; and only those (a run that reaches the ring stops on it either way).
; -----------------------------------------------------------------------------
%macro LOOK_EW 1
    cmp dl, HD_E
    jne %%w
  %assign %%k 1
  %rep %1 / 2
    cmp [es:si + IT_GRID + %%k], ax
    jne .bl
    %assign %%k %%k + 2
  %endrep
  %if %1 % 2
    cmp [es:si + IT_GRID + %%k], al
    jne .bl
  %endif
    jmp .upn
%%w:
  %assign %%k 2
  %rep %1 / 2
    cmp [es:si + IT_GRID - %%k], ax
    jne .bl
    %assign %%k %%k + 2
  %endrep
  %if %1 % 2
    cmp [es:si + IT_GRID - %%k + 1], al
    jne .bl
  %endif
    jmp .upn
%endmacro

; -----------------------------------------------------------------------------
; it_ai - decide for the computer head at BX, AL = its RNG byte. A turn sets
; H_HD, zeroes H_LEG and sets H_TURNED. Preserves BX; clobbers AX, CX, DX,
; SI, DI. (The head is its RECORD: the opening rule's is it_openp.)
; -----------------------------------------------------------------------------
it_ai:
    mov [it_r], al
    ; --- 1. the opening ---------------------------------------------------------
    cmp bx, [it_openp]
    jne .blk
    cmp byte [bx + H_TURNED], 0
    jne .blk
    test byte [bx + H_HD], 1        ; E = 1, W = 3: the axis bit
    jz .blk
    cmp byte [bx + H_LEG], OPEN_LEG
    jne .blk
    mov dl, HD_S
    call it_look_blk                ; run(S, LOOK+1) <= LOOK: no opening
    jc .blk
    mov al, HD_S
    jmp .turn
    ; --- 2. blocked --------------------------------------------------------------
.blk:
    ; run(ahead, LOOK + 1) <= LOOK - any of the LOOK + 1 cells ahead taken -
    ; is it_look_blk's question, asked INLINE here because rule 2 asks it on
    ; every move (its call, pushes and pops were ~80 cycles of the step)
    mov dl, [bx + H_HD]
    xor dh, dh
    mov di, dx
    shl di, 1
    shl di, 1
    mov di, [it_mv + di]            ; DI = the heading's step in the grid
    mov si, [bx + H_IDX]
    xor ax, ax                      ; AX = 0: a free cell, a free pair
    cmp byte [it_look], 3
    je .b3
    ja .b6
    test dl, 1                      ; Rookie: LOOK 1, two cells
    jnz .b1ew                      ; (N/S falls through: no taken jump)
%rep 2
    add si, di
    cmp [es:si + IT_GRID], al
    jne .bl
%endrep
    jmp .upn
.b1ew:
    LOOK_EW 2
.b3:
    test dl, 1                      ; Veteran: LOOK 3, four cells
    jnz .b3ew                      ; (N/S falls through: no taken jump)
%rep 4
    add si, di
    cmp [es:si + IT_GRID], al
    jne .bl
%endrep
    jmp .upn
.b3ew:
    LOOK_EW 4
.b6:
    test dl, 1                      ; Lunatic: LOOK 6, seven cells
    jnz .b6ew                      ; (N/S falls through: no taken jump)
%rep 7
    add si, di
    cmp [es:si + IT_GRID], al
    jne .bl
%endrep
    jmp .upn
.b6ew:
    LOOK_EW 7
.bl:
    mov cx, RUN_CAP
    mov dl, [bx + H_HD]
    add dl, 3                       ; left
    and dl, 3
    call it_run_hd
    mov si, ax                      ; SI = the left run
    mov cx, RUN_CAP                 ; (it_run leaves CX spent)
    mov dl, [bx + H_HD]
    inc dl                          ; right
    and dl, 3
    call it_run_hd                  ; AX = the right run
    cmp si, ax
    ja .left
    jb .right
    or ax, ax
    jz .out                         ; both 0: straight on, into the crash
    test byte [it_r], 1
    jz .left
.right:
    mov al, [bx + H_HD]
    inc al
    and al, 3
    jmp .turn
.left:
    mov al, [bx + H_HD]
    add al, 3
    and al, 3
    jmp .turn
    ; --- 3. a U-turn pending -------------------------------------------------------
.upn:
    cmp byte [bx + H_UPEND], 0
    je .short
    dec byte [bx + H_UPEND]
    jnz .short
    mov dl, [bx + H_HD]
    add dl, 3                       ; left...
    cmp byte [bx + H_USIDE], 0
    je .us
    add dl, 2                       ; ...or right (h + 5 = h + 1)
.us:
    and dl, 3
    mov [it_side], dl
    mov cx, U_TURN_MIN + 1
    call it_run_hd
    cmp ax, U_TURN_MIN
    jbe .short
    mov al, [it_side]
    jmp .turn
    ; --- 4. a short leg -----------------------------------------------------------
.short:
    cmp byte [bx + H_LEG], 8
    jb .out
    ; --- 5. intercept --------------------------------------------------------------
    mov al, [it_r]
    cmp al, [it_icpt]
    jae .wand
    cmp byte [IT_PLAYER + H_ALIVE], 0
    je .wand
    mov dl, [bx + H_HD]             ; OUR SIDES ARE PARALLEL TO THE PLAYER when
    xor dl, [IT_PLAYER + H_HD]      ; our axis is not the player's: then the
    test dl, 1                      ; model skips both, and so is asked once
    jnz .wand                       ; here instead of in two calls
    mov dl, [bx + H_HD]
    add dl, 3                       ; left first, then right - ONE try: the
    and dl, 3                       ; two sides share k and their t differ in
    call it_icpt_try                ; sign, so at most one can pass, and the
    jnc .wand                       ; try picks it (DL comes back the side)
    mov al, dl
    jmp .turn
    ; --- 6. wander ------------------------------------------------------------------
.wand:
    mov al, [it_r]
    xor al, 0xA5
    cmp al, [it_wander]
    jae .out
    mov dl, [bx + H_HD]
    inc dl                          ; r & 1 = 0: right
    test byte [it_r], 1
    jz .ws
    add dl, 2                       ; r & 1 = 1: left (h + 3)
.ws:
    and dl, 3
    mov [it_side], dl
    mov cx, U_TURN_MIN + 1
    call it_run_hd
    cmp ax, U_TURN_MIN
    jbe .out
    mov al, [it_side]
    mov [bx + H_HD], al
    mov byte [bx + H_LEG], 0
    mov byte [bx + H_TURNED], 1
    mov al, [it_r]
    and al, 7
    add al, 8
    mov [bx + H_UPEND], al          ; the U-turn, 8..15 moves on...
    mov al, [it_r]
    and al, 1
    xor al, 1
    mov [bx + H_USIDE], al          ; ...the same side (0 left, 1 right)
    jmp short .out
.turn:
    mov [bx + H_HD], al
    mov byte [bx + H_LEG], 0
    mov byte [bx + H_TURNED], 1
.out:
    ret

; -----------------------------------------------------------------------------
; it_look_blk - THE LOOK-AHEAD, the question rules 1 and 2 ask every move:
; CF = 1 when run(dir, LOOK + 1) <= LOOK, i.e. when ANY of the LOOK + 1 cells
; from the head at BX heading DL is occupied - exactly that, and nothing of
; the count. One unrolled run per tier (LOOK is 1, 3 or 6), a cell a 10-byte
; `add / cmp / jne` at ~30 cycles: the Lunatic's 7 cells N/S were 628 cycles
; through it_run's counting loop and are ~260 here, the Rookie's 2 ~120
; against 358 (SPEC.md 108.10.3's head-step). Preserves everything but AX.
; -----------------------------------------------------------------------------
it_look_blk:
    push bx
    push si
    push di
    mov si, [bx + H_IDX]
    mov bl, dl
    and bx, 3
    shl bx, 1
    shl bx, 1
    mov di, [it_mv + bx]            ; DI = the heading's step in the grid
    xor al, al
    cmp byte [it_look], 3
    je .l3
    ja .l6
%rep 2                              ; Rookie: LOOK 1, two cells
    add si, di
    cmp [es:si + IT_GRID], al
    jne .blk
%endrep
    jmp short .free
.l3:
%rep 4                              ; Veteran: LOOK 3, four cells
    add si, di
    cmp [es:si + IT_GRID], al
    jne .blk
%endrep
    jmp short .free
.l6:
%rep 7                              ; Lunatic: LOOK 6, seven cells
    add si, di
    cmp [es:si + IT_GRID], al
    jne .blk
%endrep
.free:
    clc
    jmp short .out
.blk:
    stc
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_icpt_try - rule 5, BOTH sides in one. in: BX = the head, DL = its LEFT
; side's heading, on the axis the player is NOT on (it_ai has asked). out:
; CF = 1 turn to DL - which is then the side that passed, left or right.
; Preserves BX and DH; clobbers AX, CX, SI, DI.
;
; M is where a side's ray meets the player's forward ray: t = cells for us,
; k = cells for the player, both signed by the rays' directions. The right
; side is the left one reversed, so k is the SAME for both and t only changes
; sign: at most one side has t > 0, and the model's "left, then right" is
; exactly "the side whose t is positive" - no try of the other could pass.
; (It was two calls, each working out k and t again: ~390 cycles a try,
; and a Lunatic head-step made one in ten pairs of them - SPEC.md 108.10.3.)
; -----------------------------------------------------------------------------
it_icpt_try:
    test dl, 1
    jz .vert
    ; --- the side is E or W: M = (pu, our d) ----------------------------------
    mov al, [IT_PLAYER + H_U]
    xor ah, ah
    mov cl, [bx + H_U]
    xor ch, ch
    sub ax, cx                      ; t = pu - u...
    cmp dl, HD_E
    je .th
    neg ax                          ; ...times -1 heading W
.th:
    mov si, ax
    mov al, [bx + H_D]
    xor ah, ah
    mov cl, [IT_PLAYER + H_D]
    sub ax, cx                      ; k = d - pd...
    cmp byte [IT_PLAYER + H_HD], HD_N
    je .have
    neg ax                          ; ...times -1 for a player heading S
    jmp short .have
.vert:
    ; --- the side is N or S: M = (our u, pd) ----------------------------------
    mov al, [IT_PLAYER + H_D]
    xor ah, ah
    mov cl, [bx + H_D]
    xor ch, ch
    sub ax, cx                      ; t = pd - d...
    cmp dl, HD_N
    je .tv
    neg ax                          ; ...times -1 heading S
.tv:
    mov si, ax
    mov al, [bx + H_U]
    xor ah, ah
    mov cl, [IT_PLAYER + H_U]
    sub ax, cx                      ; k = u - pu...
    cmp byte [IT_PLAYER + H_HD], HD_E
    je .have
    neg ax                          ; ...times -1 for a player heading W
.have:
    ; SI = the left side's t, AX = k
    or ax, ax
    jle .no                         ; k <= 0: neither side
    or si, si
    jg .side                        ; t > 0: the left side
    jz .no                          ; t = 0: neither
    neg si                          ; t < 0: the RIGHT side's t is -t > 0
    xor dl, 2                       ; (h + 1 = left + 2)
.side:
    ; The model's test is t + 2 < pi <= 120 with pi = ceil(6k / p), the
    ; player's iterations to M. Neither half needs the quotient: for an
    ; integer n, ceil(x) > n exactly when x > n, and ceil(x) <= 120 exactly
    ; when x <= 120 - so it is 6k > p(t + 2) and 6k <= 120p, i.e. k <= 20p.
    ; No word MUL and no DIV (~270 cycles a try saved): shifts and one byte
    ; MUL. k <= 216 and t + 2 <= 218, so p(t + 2) <= 1,962 fits the word.
    push dx                         ; DL = the side (DX goes to the sums)
    mov cl, [it_pspeed]             ; CL = p, 1..9
    xor ch, ch
    mov di, cx
    shl di, 1
    shl di, 1                       ; 4p
    mov dx, di
    shl dx, 1
    shl dx, 1                       ; 16p
    add dx, di                      ; 20p
    cmp ax, dx
    ja .nop                         ; k > 20p: pi > 120
    shl ax, 1                       ; 2k
    mov dx, ax
    shl ax, 1                       ; 4k
    add dx, ax                      ; DX = 6k
    mov ax, si
    add al, 2                       ; t + 2 (t <= 216: no carry)
    mul cl                          ; AX = p(t + 2)
    cmp ax, dx
    jae .nop                        ; t + 2 < pi needs 6k > p(t + 2)
    pop dx                          ; the side again
    mov cx, si                      ; the t cells to M are free: run(side, t)
    test dl, 1                      ; >= t is run(side, unbounded) >= t, so a
    jnz .cap                        ; N/S side scans to the ring (it_run's
    mov cx, RUN_CAP                 ; unrolled .nsf, ~25 a cell) and an E/W
.cap:                               ; one stops at t (repe scasb)
    call it_run_hd
    cmp ax, si
    jb .no
    stc
    ret
.nop:
    pop dx
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; it_bot - the harness's player (it_tflags BOT, SPEC.md 108.7.2): one F10 at
; iteration 0, and while the FIFO is empty a turn toward the longer side when
; the way ahead is 3 or less (left on a tie). Writes it_inboost / it_inf9 and
; pushes into the FIFO itself - with BOT set the keyboard's producer is off.
; -----------------------------------------------------------------------------
it_bot:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [it_inf9], 0
    mov byte [it_inboost], 0
    cmp word [it_iter], 0
    jne .nb
    mov byte [it_inboost], 1
.nb:
    mov bx, IT_PLAYER
    cmp byte [bx + H_ALIVE], 0
    je .out
    mov al, [it_fhead]
    cmp al, [it_ftail]
    jne .out                        ; a turn is already waiting
    mov dl, [bx + H_HD]
    mov cx, BOT_LOOK + 1
    call it_run_hd
    cmp ax, BOT_LOOK
    ja .out
    mov cx, RUN_CAP
    mov dl, [bx + H_HD]
    add dl, 3
    and dl, 3
    call it_run_hd
    mov si, ax                      ; left
    mov cx, RUN_CAP                 ; (it_run leaves CX spent)
    mov dl, [bx + H_HD]
    inc dl
    and dl, 3
    call it_run_hd                  ; right
    cmp si, ax                      ; (compared BEFORE AL is loaded: the
    mov al, K_LEFT                  ; right run is in AX)
    jae .push
    mov al, K_RIGHT
.push:
    call it_fifo_push
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
