; =============================================================================
; os8088 - apps/weave/wwork.inc          THE CANVAS FRAME LOOP (WEAVE-SPEC 6.10)
;
; %included by apps/weave/wcanvas.asm, which is WEAVE.WSM (WEAVE-SPEC 1.2.2).
; DS = CS = the module; ES is the canvas claim inside a body and KERNEL_SEG
; where a window record is read.
;
; -----------------------------------------------------------------------------
; THE WORKER'S OBLIGATIONS (SPEC.md 20.6), and every one of them is here
; -----------------------------------------------------------------------------
;  2. It NEVER returns and never exits on its own: OSAPI_TASK_ALIVE once per
;     outer iteration is how the task dies properly when the close box is
;     clicked, and it never comes back from that call.
;  3. It does not hold the gfx lock across a long computation: the move, the
;     collision pass and the key poll are LOCK-FREE, and the lock is taken for
;     the compose-and-blit burst alone.
;  4. It never calls TASK_ALIVE under the lock - that deadlocks the machine
;     and nothing ever draws again.
;  5. Under the lock it re-checks visibility (W_FLAGS bit 1) and arms
;     OSAPI_WM_CLIP_SET; CF=1 means not one pixel shows and the frame draws
;     nothing.
;
; And rule 7, which shapes the whole file: a worker may not touch a file, may
; not claim, and may not take a lock. So it may not write the VM's event ring
; either - `wvm_enq` runs with DS on the VM claim and its head and count are
; two unlocked words. The worker therefore STAGES into the ring below and
; posts OSAPI_WM_WAKE, and the UI task drains it and commits through wvm_enq,
; where the whole of 4.9's policy already lives. apps/ftpd (SPEC.md 77) is the
; precedent, said about frames instead of about bytes.
;
; -----------------------------------------------------------------------------
; THE ACK BYTE, AND THE RACE IT CLOSES
; -----------------------------------------------------------------------------
; The UI task frees the canvas claim on a reload, and a frame half-composed
; into freed memory does not fault on this machine - it writes over whatever
; the heap has handed to somebody else. So WSS_ACK means exactly "I am not
; touching the claim right now", and it is raised at three points: at the top
; of the loop, BEFORE OSAPI_GFX_LOCK, and after the flush.
;
; Raising it before the LOCK is the half that matters and it is not obvious.
; The UI task's unbind spins on ACK, and it may be inside a callback HOLDING
; the gfx lock - in which case a worker that had entered GFX_LOCK with ACK low
; would sit there until the UI gave up, because the UI is the one holding it.
; With ACK raised across the block, a worker waiting for the lock is correctly
; reported as not touching the claim; the claim is freed and WSS_CVSEG zeroed;
; and when the worker finally gets the lock it re-reads WSS_CVSEG, finds 0 and
; unlocks without composing anything. The mutual exclusion the lock already
; provides is what makes the other half safe: if the worker HOLDS the lock,
; the UI task is not inside a callback that does.
; =============================================================================

; -----------------------------------------------------------------------------
; wsm_stage - append one event record to the staging ring (WEAVE-SPEC 6.10.6).
;
; in:  AL = comp_id, AH = event atom, CX = data1, DX = data2
; Clobbers AX BX SI.
;
; The UI task owns HEAD and this owns TAIL, and neither writes the other's
; byte: that is the whole of the handshake, and a COUNT would have two writers
; and need the lock rule 3 forbids.
; -----------------------------------------------------------------------------
wsm_stage:
    cmp ah, 57                      ; ontick COLLAPSES to one (4.9 rule 3), by
    jne .notick                     ; a flag rather than by a scan - there is
    cmp byte [wsm_tickp], 0         ; never more than one to find
    jne .drop
    mov byte [wsm_tickp], 1
.notick:
    mov bl, [wsm_rtail]
    mov bh, bl
    inc bh
    and bh, WSM_RING - 1
    cmp bh, [wsm_rhead]
    jne .put                        ; there is room
    ; --- the ring is FULL (4.9 rules 4 and 5) -------------------------------
    inc word [wsm_ovf]              ; counted, always: INPUT OVERRUN is one of
                                    ; the three defects no emulator shows
                                    ; (CLAUDE.md), so it is instrumented
                                    ; rather than hoped about
    cmp ah, 50                      ; a key?
    jne .drop
    ; Rule 4's shape, adapted to a single-writer ring: the NEWEST queued
    ; record is overwritten by the key when it is not itself a key. The rule
    ; as 4.9 writes it scans further back and SLIDES, which would move slots
    ; the UI task is reading and needs the lock a worker may not take;
    ; overwriting the slot THIS task wrote last does not. If the newest is a
    ; key, or is the one the UI is about to read, the answer is rule 5's BEL.
    mov bh, bl
    dec bh
    and bh, WSM_RING - 1
    cmp bh, [wsm_rhead]
    je .bel
    push ax
    mov al, WSM_REC
    mul bh
    mov si, ax
    pop ax
    add si, wsm_ring
    cmp byte [si + 1], 50
    je .bel
    jmp short .write
.bel:
    push ax
    push cx
    push dx
    mov ax, 880
    mov cx, 2
    mov dl, 0x40
%ifndef WSM_HOSTTEST
    call OSAPI_SND_TONE             ; 4.9 rule 5's BEL, and it can only be
%endif                              ; emitted by the task that SEES the
    inc word [wsm_bels]             ; overflow. SND_TONE is the one
    pop dx                          ; worker-legal voice (8.4, 9.11) - and the
    pop cx                          ; boot-sector differential has no kernel
    pop ax                          ; to call, so it counts them instead
.drop:
    ret
.put:
    push ax
    mov al, WSM_REC
    mul bl
    mov si, ax
    pop ax
    add si, wsm_ring
    mov bh, bl
    inc bh
    and bh, WSM_RING - 1
.write:
    mov [si], al
    mov [si + 1], ah
    mov [si + 2], cx
    mov [si + 4], dx
    cmp bh, bl
    je .done                        ; an overwrite in place: TAIL does not move
    mov [wsm_rtail], bh             ; ...written LAST, so a record is visible
.done:                              ; to the drain only once it is whole
    mov byte [wsm_posted], 1
    ret

; -----------------------------------------------------------------------------
; wsm_sar4 - AX = AX >> 4, ARITHMETIC (WEAVE-SPEC 6.10.1).
;
; Not an IDIV, and the difference is a pixel on every negative coordinate:
; -17 >> 4 is -2 and -17 / 16 truncating toward zero is -1. The model floors
; and so does this.
; -----------------------------------------------------------------------------
wsm_sar4:
    push cx
    mov cl, 4
    sar ax, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; wsm_wall - stage one onwall. in: DX = the edge code (0 T, 1 B, 2 L, 3 R).
; Preserves SI; clobbers AX BX CX.
; -----------------------------------------------------------------------------
wsm_wall:
    push si
    mov ax, [wsm_cid]
    mov ah, 55                      ; onwall
    mov cx, [wsm_cid]
    add cx, [wsm_spri]
    inc cx                          ; the sprite's own comp_id: a sprite
                                    ; record follows its canvas directly and
                                    ; ids are 1..n in UISTREAM order (2.5)
    call wsm_stage
    pop si
    ret

; -----------------------------------------------------------------------------
; wsm_move - one sprite's move, its four edges and its score latch.
;
; in:  ES = the canvas claim, BX = the sprite index
; Clobbers AX BX CX DX SI.
; -----------------------------------------------------------------------------
wsm_move:
    mov [wsm_spri], bx
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov si, ax
    mov [wsm_srec], si
    test byte [es:si + WSR_FLAGS], WSRF_SHOWN
    jz .out                         ; a hidden sprite does not move, does not
                                    ; bounce, does not score and is not a
                                    ; collision candidate (6.10.1)
    mov ax, [es:si + WSR_PX16]
    add ax, [es:si + WSR_VX]
    mov [es:si + WSR_PX16], ax
    call wsm_sar4
    mov [es:si + WSR_X], ax
    mov ax, [es:si + WSR_PY16]
    add ax, [es:si + WSR_VY]
    mov [es:si + WSR_PY16], ax
    call wsm_sar4
    mov [es:si + WSR_Y], ax

    ; --- ALL FOUR hit tests, taken HERE, before any bounce (6.10.1) ---------
    xor dl, dl
    mov ax, [es:si + WSR_Y]
    or ax, ax
    jns .nt
    or dl, 1
.nt:
    mov bx, ax
    mov cl, [es:si + WSR_PH]
    xor ch, ch
    add bx, cx
    cmp bx, [es:WSC_H]
    jle .nb
    or dl, 2
.nb:
    mov ax, [es:si + WSR_X]
    or ax, ax
    jns .nl
    or dl, 4
.nl:
    mov bx, ax
    mov cl, [es:si + WSR_PW]
    xor ch, ch
    add bx, cx
    cmp bx, [es:WSC_W]
    jle .nr
    or dl, 8
.nr:
    mov [wsm_hits], dl
    mov word [wsm_oute], -1

    ; --- T ------------------------------------------------------------------
    test byte [wsm_hits], 1
    jz .edgeb
    test byte [es:WSC_WALLS], 1
    jz .topen
    mov ax, [es:si + WSR_PY16]
    neg ax
    mov [es:si + WSR_PY16], ax
    call wsm_sar4
    mov [es:si + WSR_Y], ax
    mov ax, [es:si + WSR_VY]
    neg ax
    mov [es:si + WSR_VY], ax
    xor dx, dx
    call wsm_wall
    jmp short .edgeb
.topen:
    mov ax, [es:si + WSR_Y]
    mov cl, [es:si + WSR_PH]
    xor ch, ch
    add ax, cx
    or ax, ax
    jns .edgeb
    mov word [wsm_oute], 0

    ; --- B ------------------------------------------------------------------
.edgeb:
    test byte [wsm_hits], 2
    jz .edgel
    test byte [es:WSC_WALLS], 2
    jz .bopen
    mov ax, [es:WSC_H]
    mov cl, [es:si + WSR_PH]
    xor ch, ch
    sub ax, cx
    mov cl, 4
    shl ax, cl
    shl ax, 1                       ; 2 x (H - ph) x 16: a true mirror, so the
    sub ax, [es:si + WSR_PY16]      ; sprite ends the same distance inside the
    mov [es:si + WSR_PY16], ax      ; wall as it overshot, remainder and all
    call wsm_sar4
    mov [es:si + WSR_Y], ax
    mov ax, [es:si + WSR_VY]
    neg ax
    mov [es:si + WSR_VY], ax
    mov dx, 1
    call wsm_wall
    jmp short .edgel
.bopen:
    cmp word [wsm_oute], 0
    jge .edgel
    mov ax, [es:si + WSR_Y]
    cmp ax, [es:WSC_H]
    jle .edgel
    mov word [wsm_oute], 1

    ; --- L ------------------------------------------------------------------
.edgel:
    test byte [wsm_hits], 4
    jz .edger
    test byte [es:WSC_WALLS], 4
    jz .lopen
    mov ax, [es:si + WSR_PX16]
    neg ax
    mov [es:si + WSR_PX16], ax
    call wsm_sar4
    mov [es:si + WSR_X], ax
    mov ax, [es:si + WSR_VX]
    neg ax
    mov [es:si + WSR_VX], ax
    mov dx, 2
    call wsm_wall
    jmp short .edger
.lopen:
    cmp word [wsm_oute], 0
    jge .edger
    mov ax, [es:si + WSR_X]
    mov cl, [es:si + WSR_PW]
    xor ch, ch
    add ax, cx
    or ax, ax
    jns .edger
    mov word [wsm_oute], 2

    ; --- R ------------------------------------------------------------------
.edger:
    test byte [wsm_hits], 8
    jz .score
    test byte [es:WSC_WALLS], 8
    jz .ropen
    mov ax, [es:WSC_W]
    mov cl, [es:si + WSR_PW]
    xor ch, ch
    sub ax, cx
    mov cl, 4
    shl ax, cl
    shl ax, 1
    sub ax, [es:si + WSR_PX16]
    mov [es:si + WSR_PX16], ax
    call wsm_sar4
    mov [es:si + WSR_X], ax
    mov ax, [es:si + WSR_VX]
    neg ax
    mov [es:si + WSR_VX], ax
    mov dx, 3
    call wsm_wall
    jmp short .score
.ropen:
    cmp word [wsm_oute], 0
    jge .score
    mov ax, [es:si + WSR_X]
    cmp ax, [es:WSC_W]
    jle .score
    mov word [wsm_oute], 3

    ; --- the score latch, decided AFTER the whole edge loop (6.10.1) --------
.score:
    mov si, [wsm_srec]
    cmp word [wsm_oute], 0
    jge .isout
    and byte [es:si + WSR_FLAGS], ~WSRF_SCORED & 0xFF
    jmp short .out                  ; ...and THAT is the re-arm. An event that
                                    ; fires once per exit needs a definition
                                    ; of coming back; the model latched it for
                                    ; the life of the app instead, which made
                                    ; PONG a one-goal game
.isout:
    test byte [es:si + WSR_FLAGS], WSRF_SCORED
    jnz .out
    or byte [es:si + WSR_FLAGS], WSRF_SCORED
    mov word [es:si + WSR_VX], 0
    mov word [es:si + WSR_VY], 0
    mov ax, [wsm_cid]
    mov ah, 56                      ; onscore
    mov cx, [wsm_cid]
    add cx, [wsm_spri]
    inc cx
    mov dx, [wsm_oute]
    call wsm_stage
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_cbit - the contact bit for pair [wsm_pair].
; out: BX -> its byte in DS, AL = its mask. Clobbers CX.
; -----------------------------------------------------------------------------
wsm_cbit:
    mov bx, [wsm_pair]
    mov cx, bx
    and cl, 7
    mov al, 1
    shl al, cl
    mov cl, 3
    shr bx, cl
    add bx, wsm_cont
    ret

; -----------------------------------------------------------------------------
; wsm_pairtest - one sprite pair's AABB. Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
wsm_pairtest:
    mov al, WSR_SIZE
    mul byte [wsm_ci]
    add ax, WSC_SPR
    mov si, ax
    mov al, WSR_SIZE
    mul byte [wsm_cj]
    add ax, WSC_SPR
    mov di, ax
    mov al, [es:si + WSR_FLAGS]
    and al, [es:di + WSR_FLAGS]
    test al, WSRF_SHOWN
    jz .apart                       ; either one hidden: SEPARATED, by
                                    ; definition (6.10.1). Walking the shown
                                    ; list alone could never reach this arm,
                                    ; which is what left a hidden sprite's
                                    ; contact latched for the rest of the run
    ; ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2, all STRICT: touching
    ; edges do not collide
    mov ax, [es:di + WSR_X]
    mov cl, [es:di + WSR_PW]
    xor ch, ch
    add ax, cx
    cmp [es:si + WSR_X], ax
    jge .apart
    mov ax, [es:si + WSR_X]
    mov cl, [es:si + WSR_PW]
    xor ch, ch
    add ax, cx
    cmp [es:di + WSR_X], ax
    jge .apart
    mov ax, [es:di + WSR_Y]
    mov cl, [es:di + WSR_PH]
    xor ch, ch
    add ax, cx
    cmp [es:si + WSR_Y], ax
    jge .apart
    mov ax, [es:si + WSR_Y]
    mov cl, [es:si + WSR_PH]
    xor ch, ch
    add ax, cx
    cmp [es:di + WSR_Y], ax
    jge .apart
    call wsm_cbit
    test [bx], al
    jnz .done                       ; already in contact: once per contact
    or [bx], al
    mov ax, [wsm_cid]
    mov ah, 54                      ; oncollide
    mov cx, [wsm_cid]
    add cx, [wsm_ci]
    inc cx
    mov dx, [wsm_cid]
    add dx, [wsm_cj]
    inc dx
    call wsm_stage
.done:
    ret
.apart:
    call wsm_cbit
    not al
    and [bx], al
    ret

; -----------------------------------------------------------------------------
; wsm_collide - AABB over every sprite PAIR (WEAVE-SPEC 6.10.1).
; in: ES = the canvas claim. Clobbers everything but BP, DS, ES, SS:SP.
; -----------------------------------------------------------------------------
wsm_collide:
    mov word [wsm_pair], 0
    mov word [wsm_ci], 0
.i:
    mov ax, [wsm_ci]
    inc ax
    cmp ax, [wsm_nspr]
    jae .out
    mov [wsm_cj], ax
.j:
    mov ax, [wsm_cj]
    cmp ax, [wsm_nspr]
    jae .nexti
    call wsm_pairtest
    inc word [wsm_pair]
    inc word [wsm_cj]
    jmp short .j
.nexti:
    inc word [wsm_ci]
    jmp short .i
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_pollkeys - WEAVE-SPEC 6.10.5's pinned 37-key poll.
;
; OSAPI_KEY_DOWN is the held-key API (SPEC.md 9.7) and is worker-legal by its
; own contract: it takes no lock, touches no port and reads no VRAM. int 16h
; cannot answer this question at all - a typematic repeat is byte-identical to
; a fresh press - which is why a game loop needs it and why the set is polled
; rather than inferred.
;
; The set is PINNED and is letters, digits and space, because 3.4 gives a
; canvas's onkey one ASCII word and one up/down word and NO scan code: a key
; with no ASCII meaning would arrive as onkey(0, 1) and no handler could tell
; which one it was. Arrows are therefore deliberately absent (6.10.5).
;
; 37 far calls at 46.7 us is 1.7 ms of a 55 ms frame - 3%, against ~756 us for
; a single drawing primitive.
;
; Clobbers everything but BP, DS, ES, SS:SP.
; -----------------------------------------------------------------------------
wsm_pollkeys:
    mov word [wsm_ki], 0
.k:
    cmp word [wsm_ki], WSM_NKEY
    jae .out
    mov si, [wsm_ki]
    shl si, 1
    mov al, [wsm_ktab + si]         ; the make scancode
    call OSAPI_KEY_DOWN             ; CF = 1 down
    mov byte [wsm_kdn], 0
    jnc .have
    mov byte [wsm_kdn], 1
.have:
    mov bx, [wsm_ki]
    mov cx, bx
    and cl, 7
    mov al, 1
    shl al, cl                      ; AL = this key's bit
    mov cl, 3
    shr bx, cl
    add bx, wsm_kstate
    mov ah, [bx]
    mov ch, ah
    and ch, al                      ; CH != 0: it WAS down
    cmp byte [wsm_kdn], 0
    je .up
    or ah, al
    jmp short .st
.up:
    mov cl, al
    not cl
    and ah, cl
.st:
    mov [bx], ah
    ; a transition?
    or ch, ch
    jz .wasup
    cmp byte [wsm_kdn], 0
    jne .next                       ; down, and was: no event
    jmp short .fire
.wasup:
    cmp byte [wsm_kdn], 0
    je .next                        ; up, and was: no event
.fire:
    mov si, [wsm_ki]
    shl si, 1
    mov cl, [wsm_ktab + si + 1]     ; the ASCII this key means
    xor ch, ch
    xor dh, dh
    mov dl, [wsm_kdn]
    mov ax, [wsm_cid]
    mov ah, 50                      ; onkey: data1 = ASCII, data2 = 1 down
    call wsm_stage
.next:
    inc word [wsm_ki]
    jmp .k
.out:
    ret

; The pinned set (6.10.5): a make scancode, then the lower-case ASCII it means.
wsm_ktab:
    db 0x02,'1', 0x03,'2', 0x04,'3', 0x05,'4', 0x06,'5'
    db 0x07,'6', 0x08,'7', 0x09,'8', 0x0A,'9', 0x0B,'0'
    db 0x10,'q', 0x11,'w', 0x12,'e', 0x13,'r', 0x14,'t'
    db 0x15,'y', 0x16,'u', 0x17,'i', 0x18,'o', 0x19,'p'
    db 0x1E,'a', 0x1F,'s', 0x20,'d', 0x21,'f', 0x22,'g'
    db 0x23,'h', 0x24,'j', 0x25,'k', 0x26,'l'
    db 0x2C,'z', 0x2D,'x', 0x2E,'c', 0x2F,'v', 0x30,'b'
    db 0x31,'n', 0x32,'m'
    db KSC_SPACE,' '

; -----------------------------------------------------------------------------
; wsm_place - the canvas's top-left in SCREEN pixels.
; out: AX = x, BX = y. Clobbers CX DX.
;
; Asked of the kernel every frame rather than banked at layout, so a window
; the user has DRAGGED is right on the next frame instead of on the next
; repaint - and a repaint may never come. The x rounds UP to a multiple of 8
; exactly as the flow walk does (7.1.2), because GFX_BLIT1's x must be one and
; because a canvas at a different origin from its own card would be a
; pixel-level disagreement with everything else the walk placed.
; -----------------------------------------------------------------------------
wsm_place:
    mov bx, [wsm_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add ax, 7
    and ax, ~7 & 0xFFFF
    mov bx, [wsm_cx]
    mov cl, 3
    shl bx, cl
    add ax, bx                      ; AX = the canvas's screen x
    mov bx, [wsm_cy]
    shl bx, cl
    add bx, dx                      ; BX = ...and its screen y
    ret

; -----------------------------------------------------------------------------
; wsm_doframe - one frame, in WEAVE-SPEC 6.10's own order.
; Clobbers everything but BP, DS, SS:SP and AX.
; -----------------------------------------------------------------------------
wsm_doframe:
    push ax
    mov es, [wsm_cvseg]
    mov ax, [es:WSC_CID]
    mov [wsm_cid], ax
    mov ax, [es:WSC_NSPR]
    mov [wsm_nspr], ax
    inc word [wsm_frame]
    inc word [wsm_frames]
    mov byte [wsm_ack], 0           ; phase 1: lock-free, and touching

    ; 1-2: move, bounce, score
    xor bx, bx
.mv:
    cmp bx, [wsm_nspr]
    jae .coll
    push bx
    call wsm_move
    pop bx
    inc bx
    jmp short .mv
.coll:
    ; 3: AABB
    call wsm_collide
    ; ...and the bands this frame changed
    mov es, [wsm_cvseg]
    xor bx, bx
.mk:
    cmp bx, [wsm_nspr]
    jae .draw
    push bx
    call wsm_markspr
    pop bx
    inc bx
    jmp short .mk

.draw:
    ; 4-5: compose and emit, under the lock and nowhere near a long
    ; computation (SPEC.md 20.6 rule 3)
    mov byte [wsm_ack], 1           ; raised BEFORE the lock - this file's
                                    ; header has the race it closes
    call OSAPI_GFX_LOCK
    cmp byte [wsm_run], 0           ; re-read UNDER the lock: the UI task may
    je .unlock                      ; have stopped us and freed the claim
    cmp word [wsm_cvseg], 0         ; while we waited here
    je .unlock
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [wsm_win]
    test word [es:bx + W_FLAGS], 2  ; still visible? (rule 5)
    jz .unlock
    call OSAPI_WM_CLIP_SET          ; BX = our window
    jc .unlock                      ; not one pixel shows: draw nothing
    mov byte [wsm_ack], 0           ; the pen is wsm_flush's, because both
                                    ; callers need it and a pen is valid for
                                    ; one lock hold only
    mov es, [wsm_cvseg]
    call wsm_place                  ; AX, BX = the canvas's screen origin
    call wsm_flush
    mov byte [wsm_ack], 1
.unlock:
    call OSAPI_GFX_UNLOCK

    ; 6: the key poll, lock-free
    call wsm_pollkeys

    ; 7: every `tick` frames, one ontick
    mov es, [wsm_cvseg]
    mov cx, [es:WSC_TICK]
    jcxz .out
    mov ax, [wsm_frame]
    xor dx, dx
    div cx
    or dx, dx
    jnz .out
    mov ax, [wsm_cid]
    mov ah, 57                      ; ontick, data1 = the frame counter's low
    mov cx, [wsm_frame]             ; word (3.4)
    xor dx, dx
    call wsm_stage
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; WSMV_WORKER - the frame loop. It DOES NOT RETURN (SPEC.md 20.6 rule 2).
; in: [wsm_ab] = our window pointer, banked by the dispatcher.
; -----------------------------------------------------------------------------
wsm_v_worker:
    mov ax, [wsm_ab]
    mov [wsm_win], ax
.loop:
    mov byte [wsm_ack], 1           ; "not touching the claim"
    cmp byte [wsm_run], 0
    je .idle
    cmp word [wsm_cvseg], 0
    je .idle
    mov ax, [wsm_phase]
    inc ax
    cmp ax, [wsm_sleep]
    jb .nostep
    xor ax, ax
    call wsm_doframe
.nostep:
    mov [wsm_phase], ax
    cmp byte [wsm_posted], 0
    je .nowake
    mov byte [wsm_posted], 0
    mov bx, [wsm_win]
    call OSAPI_WM_WAKE              ; ISR- and worker-safe, no lock needed -
                                    ; SPEC.md 20.6 rule 7 as this wave amends
                                    ; it, and the cell has always said so
.nowake:
    mov ax, 1
    jmp short .park
.idle:
    mov ax, 4                       ; parked: a quarter of the tick rate, so a
                                    ; stopped canvas costs ~4 task switches a
                                    ; second rather than 18
.park:
    call OSAPI_TASK_SLEEP
    mov bx, [wsm_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4),
    jmp .loop                       ; and this never returns once the close
                                    ; box has been clicked
