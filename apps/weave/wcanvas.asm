; =============================================================================
; os8088 - apps/weave/wcanvas.asm            WEAVE.WSM (WEAVE-SPEC 1.2.2)
;
; The canvas core: WEAVE-SPEC 6.10's frame loop, mask composition, dirty-band
; emit, AABB collision, the key poll and the staging ring. A SECOND, RESIDENT
; SEGMENT beside WEAVE.O88 - not an overlay, and the difference is the whole
; design:
;
;   an overlay is on-demand and REFUSABLE, and SPEC.md 73.14's cc_ovneed
;   refuses a WORKER at its first instruction (it claims memory and reads a
;   floppy, both forbidden by SPEC.md 20.6 rule 7, and its return-stash LIFO
;   is correct for one task only). Every byte in this file runs on the worker,
;   eighteen times a second. So the overlay tenant list is not spent here, it
;   is INAPPLICABLE - and the answer is CLAUDE.md's own hard rule read through
;   SPEC.md 68.10's WORD.OVL and C64.ROM's lifecycle: a module beside the
;   package, read ONCE at open and only when the bundle declares a <canvas>,
;   resident until the instance closes.
;
; WEAVE-PLAN 2.9 prices the four alternatives this beat, and the first
; paragraph of the pull request names it as the decision the owner may
; reverse.
;
; -----------------------------------------------------------------------------
; IT IS A SEPARATE ASSEMBLY, AND THAT IS WHAT MAKES THE ABI A CONTRACT
; -----------------------------------------------------------------------------
; `nasm -f bin` has no notion of an external symbol, so this file and
; apps/weave/weave.asm share exactly one thing: apps/weave/wsmabi.inc. Nothing
; here may name a package label and nothing there may name one of ours. The
; three-word stamp at +0 (magic, ABI, size) is what a load checks before it
; believes a byte of this file (WEAVE-SPEC 1.2.2).
;
; -----------------------------------------------------------------------------
; THE SEGMENT RULES, WHICH ARE THE WHOLE TAX ON LIVING OUT HERE
; -----------------------------------------------------------------------------
;  * DS is the CALLER's on entry and on return. Every body banks it, sets
;    DS = CS - so the state block and the scratch below are plain DS-relative
;    labels rather than a file of `cs:` overrides - and puts it back.
;  * ES is preserved across every verb. Inside a body ES is the canvas claim
;    (the composition's destination) or KERNEL_SEG (the window record).
;  * SS is the task's, never ours: [bp+n] addresses the STACK and is right for
;    a scratch frame and wrong for anything of ours (CLAUDE.md's hard rule).
;  * The worker's stack is 384 bytes (SPEC.md 20.6 rule 6) and the tick and
;    mouse ISRs land on it while we run, so the deepest frame here is
;    wsm_draw's 16 bytes and there is no recursion anywhere.
;  * cpu 8086: no pusha, no push imm, no `shl reg, imm` above 1, no 32-bit
;    register. `shr ax, cl` IS an 8086 instruction and is what the composer's
;    barrel shift is built on.
; =============================================================================

cpu 8086
bits 16
org 0

%include "os88api.inc"              ; OSAPI_* are KERNEL_SEG:offset far
                                    ; immediates, so they work from any
                                    ; segment - which is why the module needs
                                    ; no call-back vector into the package
%include "weave/wsmabi.inc"

; =============================================================================
; THE HEADER (WEAVE-SPEC 1.2.2) - eight bytes, then the entry
; =============================================================================
wsm_hdr:
    dw WSM_MAGIC                    ; +0  so a truncated or unrelated file is
                                    ;     refused rather than entered
    dw WSM_ABI                      ; +2  the contract number, from the ONE
                                    ;     file both assemblies include
    dw wsm_end                      ; +4  our own size: the package compares
                                    ;     it with WSM_SIZE, injected by the
                                    ;     Makefile after this file is built
                                    ;     and before the package is
    dw wsm_state                    ; +6  where the state block starts, so the
                                    ;     package can read and write it with
                                    ;     wblob.inc's accessors and needs no
                                    ;     verb for a counter

; -----------------------------------------------------------------------------
; wsm_entry - the far-called dispatcher, at WSM_ENTRY = 8
;
; in:  AL = the verb (WSMV_*), AH = a verb flag, BX/CX/DX = its arguments
; out: AX = the answer. BP, DS, ES, SI, DI, SS:SP and DF as they arrived.
; -----------------------------------------------------------------------------
wsm_entry:
    push bp
    push si
    push di
    push es
    push ds
    mov [cs:wsm_cds], ds            ; the caller's DS, for the two verbs that
                                    ; read or write a block in it
    push cs
    pop ds                          ; ...and now every label below is DS
    cld
    mov [wsm_ab], bx
    mov [wsm_ac], cx
    mov [wsm_ad], dx
    mov [wsm_af], ah
    xor ah, ah
    cmp al, WSMV_PLACE
    ja .bad
    mov bx, ax
    shl bx, 1
    jmp [wsm_tab + bx]
.bad:
    xor ax, ax
wsm_out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

wsm_tab:
    dw wsm_v_bind                   ; 0
    dw wsm_v_sprite                 ; 1
    dw wsm_v_start                  ; 2
    dw wsm_v_stop                   ; 3
    dw wsm_v_paint                  ; 4
    dw wsm_v_drain                  ; 5
    dw wsm_v_unbind                 ; 6
    dw wsm_v_worker                 ; 7
    dw wsm_v_place                  ; 8

; =============================================================================
; THE VERBS
; =============================================================================

; -----------------------------------------------------------------------------
; WSMV_BIND - take the canvas claim and lay WEAVE-SPEC 6.10.4 out in it.
;
; The claim arrives zeroed by its caller, so every sprite record starts at
; 0,0 with no velocity, not shown and no image; the load path then writes each
; one with WSMF_DESC and the birth properties. Nothing here reads the bundle:
; the caller has already validated it (10.4) and the descriptor walk is
; WSMF_DESC's, one sprite at a time, so a bundle with sixteen sprites costs
; sixteen small calls rather than one body that has to re-validate.
; -----------------------------------------------------------------------------
wsm_v_bind:
    mov ax, [wsm_ab]                ; the canvas claim
    mov [wsm_cvseg], ax
    mov es, ax
    mov ax, [wsm_ac]                ; the bundle claim
    mov [wsm_bseg], ax
    mov si, [wsm_ad]                ; the parameter block, in the caller's DS
    push ds
    mov ds, [cs:wsm_cds]
    mov ax, [si + WSMP_W]
    mov bx, [si + WSMP_H]
    mov cx, [si + WSMP_WALLS]
    mov dx, [si + WSMP_TICK]
    mov di, [si + WSMP_NSPR]
    mov bp, [si + WSMP_SPOFF]
    mov si, [si + WSMP_CID]
    pop ds
    mov [es:WSC_W], ax
    mov [es:WSC_H], bx
    mov [es:WSC_WALLS], cx
    mov [es:WSC_TICK], dx
    mov [es:WSC_NSPR], di
    mov [es:WSC_CID], si
    mov [wsm_cid], si
    mov [wsm_nspr], di
    mov [wsm_spoff], bp
    mov cl, 3
    shr ax, cl                      ; stride = W / 8
    mov [es:WSC_STRIDE], ax
    ; the buffer follows the header and the sprite records
    mov ax, di
    mov cl, WSR_SIZE
    mul cl                          ; AX = 24 x nspr (di <= 16, so no overflow)
    add ax, WSC_SPR
    mov [es:WSC_BUF], ax
    mov word [wsm_run], 0           ; RUN and ACK together: the loop is parked
    mov word [wsm_phase], 0
    mov word [wsm_frame], 0
    mov word [wsm_blits], 0
    mov word [wsm_frames], 0
    mov word [wsm_ovf], 0
    mov byte [wsm_tickp], 0
    mov byte [wsm_rhead], 0
    mov byte [wsm_rtail], 0
    mov byte [wsm_posted], 0
    ; 6.10.7's pen, read last because every register above is spoken for. The
    ; low byte is the canvas's PAPER colour and the high one is `colored` -
    ; raised here for a paper that is not white, and by a WSMF_COLOR write for
    ; an ink that is not black. Clear, and wsm_flush is the file wave 5
    ; shipped, instruction for instruction.
    mov si, [wsm_ad]
    push ds
    mov ds, [cs:wsm_cds]
    mov ax, [si + WSMP_COLOR]
    pop ds
    and ax, 0x000F                  ; a hostile bundle cannot reach the kernel
    mov [wsm_pen], al               ; with a colour outside 0..15
    mov byte [wsm_pen + 1], 0
    cmp al, CWHITE
    je .plain
    mov byte [wsm_pen + 1], 1
.plain:
    ; the dirty bands, the contact bits and last frame's key states all belong
    ; to the bundle that is going away, and a stale bit in any of them is an
    ; event fired against a component the new bundle does not have
    mov di, wsm_kstate              ; one contiguous run: the key states, the
    mov cx, 5 + WSM_MAXBAND + 15    ; dirty bands and the contact bits
    xor al, al
    push es
    push cs
    pop es
    rep stosb
    pop es
    mov ax, 1
    jmp wsm_out

; -----------------------------------------------------------------------------
; WSMV_PLACE - where the canvas sits in the content grid, in CELLS.
;
; The UI task writes it at every layout. The worker adds it to what
; OSAPI_WM_CONTENT answers, so a window the user has DRAGGED is right on the
; next frame rather than on the next repaint - which matters because a frame
; is 55 ms and a repaint may never come.
; -----------------------------------------------------------------------------
wsm_v_place:
    mov ax, [wsm_ab]
    mov [wsm_cx], ax
    mov ax, [wsm_ac]
    mov [wsm_cy], ax
    mov ax, 1
    jmp wsm_out

; -----------------------------------------------------------------------------
; WSMV_SPRITE - one field of one sprite record. AH = 0 read, 1 write.
; -----------------------------------------------------------------------------
wsm_v_sprite:
    xor ax, ax
    cmp word [wsm_cvseg], 0
    je .out
    mov bx, [wsm_ab]
    cmp bx, [wsm_nspr]
    jae .out
    mov es, [wsm_cvseg]
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov si, ax                      ; ES:SI = the record
    mov bx, [wsm_ac]                ; the field id, in BL
    and bx, 0x00FF
    cmp bl, WSMF_COLOR
    ja .zero
    shl bx, 1
    cmp byte [wsm_af], 0
    jne .write
    jmp [wsm_get + bx]
.write:
    jmp [wsm_set + bx]
.zero:
    xor ax, ax
.out:
    jmp wsm_out

wsm_get:
    dw .x, .y, .vx, .vy, .frame, .shown, .nfr, .zero, .color
.x:     mov ax, [es:si + WSR_X]
        jmp wsm_out
.y:     mov ax, [es:si + WSR_Y]
        jmp wsm_out
.vx:    mov ax, [es:si + WSR_VX]
        jmp wsm_out
.vy:    mov ax, [es:si + WSR_VY]
        jmp wsm_out
.frame: mov al, [es:si + WSR_FRAME]
        and ax, 0x000F
        jmp wsm_out
.shown: mov al, [es:si + WSR_FLAGS]
        and ax, WSRF_SHOWN
        jmp wsm_out
.nfr:   mov al, [es:si + WSR_NFR]
        xor ah, ah
        jmp wsm_out
.color: mov al, [es:si + WSR_FLAGS]
        mov cl, WSRF_COLSH
        shr al, cl
        and ax, 0x000F
        jmp wsm_out
.zero:  xor ax, ax
        jmp wsm_out

wsm_set:
    dw .x, .y, .vx, .vy, .frame, .shown, .nofr, .desc, .color
    ; A write to x or y RE-SEEDS the 1/16-px accumulator and therefore
    ; DISCARDS the sub-pixel remainder (WEAVE-SPEC 6.10.1). The model does the
    ; same and this is the sentence that says so on both sides.
.x:     mov ax, [wsm_ad]
        mov [es:si + WSR_X], ax
        mov cl, 4
        shl ax, cl
        mov [es:si + WSR_PX16], ax
        jmp .ok
.y:     mov ax, [wsm_ad]
        mov [es:si + WSR_Y], ax
        mov cl, 4
        shl ax, cl
        mov [es:si + WSR_PY16], ax
        jmp .ok
.vx:    mov ax, [wsm_ad]
        mov [es:si + WSR_VX], ax
        jmp .ok2
.vy:    mov ax, [wsm_ad]
        mov [es:si + WSR_VY], ax
        jmp .ok2
.frame: mov ax, [wsm_ad]
        cmp al, [es:si + WSR_NFR]
        jae .no                     ; 10.6.1's `frame %d of %d.` - the CALLER
        cmp ah, 0                   ; spells it, from WSMF_NFRAME
        jne .no
        mov ah, [es:si + WSR_FRAME]
        and ah, 0xF0
        or al, ah
        mov [es:si + WSR_FRAME], al
        jmp .ok
.shown: mov al, [es:si + WSR_FLAGS]
        and al, ~WSRF_SHOWN & 0xFF
        cmp word [wsm_ad], 0
        je .sh0
        or al, WSRF_SHOWN
.sh0:   mov [es:si + WSR_FLAGS], al
        jmp .ok
.nofr:  jmp .no
.desc:  call wsm_desc
        jmp .ok2
        ; 6.10.7. The colour is the flags byte's TOP nibble, so this is a
        ; masked read-modify-write and every other writer of that byte is one
        ; too. `colored` is raised here and never lowered - by a NON-ZERO
        ; colour only, so the load path can write this field for every sprite
        ; (it does) and a canvas whose sprites are all black on white paper
        ; still ends the load with the palette off, which is the wave-5
        ; composer exactly.
.color: mov al, [wsm_ad]
        and al, 0x0F                  ; ...which is also the `colored` test
        jz .colst
        mov byte [wsm_pen + 1], 1
.colst: mov cl, WSRF_COLSH
        shl al, cl
        mov ah, [es:si + WSR_FLAGS]
        and ah, WSRF_STMASK
        or al, ah
        mov [es:si + WSR_FLAGS], al
        jmp .ok2
.ok:    or byte [es:si + WSR_FLAGS], WSRF_DIRTY
.ok2:   mov ax, 1
        jmp wsm_out
.no:    xor ax, ax
        jmp wsm_out

; -----------------------------------------------------------------------------
; wsm_desc - stamp one SPRITES descriptor (2.11) into a sprite record.
; in: ES:SI = the record, [wsm_ad] = the descriptor index. Clobbers AX BX CX DX.
;
; The descriptors are 8 bytes each at SPRITES+2, and the data offset they
; carry is relative to the SECTION, so it is turned into a CLAIM offset here
; and never again. Two <sprite> components may name ONE descriptor (PONG's two
; paddles do), so nothing about the descriptor may be consumed.
; -----------------------------------------------------------------------------
wsm_desc:
    push es
    push si
    push ds
    mov bx, [wsm_ad]
    mov al, 8
    mul bl
    add ax, 2
    add ax, [wsm_spoff]
    mov di, ax                      ; the descriptor, in the bundle claim
    mov ds, [cs:wsm_bseg]
    mov al, [di]                    ; w_bytes
    mov ah, [di + 1]                ; h_px
    mov bl, [di + 2]                ; frames
    mov dx, [di + 4]                ; the data offset, section-relative
    pop ds
    pop si
    pop es
    add dx, [wsm_spoff]
    mov [es:si + WSR_WB], al
    mov [es:si + WSR_PH], ah
    mov [es:si + WSR_NFR], bl
    mov [es:si + WSR_DOFF], dx
    mov cl, 3
    shl al, cl                      ; pw = 8 x w_bytes; wb <= 8, so it fits
    mov [es:si + WSR_PW], al
    ret

; -----------------------------------------------------------------------------
; WSMV_START / WSMV_STOP
;
; sleep = max(1, round(18.2 / fps)) - WEAVE-SPEC 6.10.1's table, taken from a
; pinned lookup rather than computed. The model spells it `int(18.2/fps+0.5)`
; in floating point, and this machine has no float at all; eighteen bytes is
; cheaper than any fixed-point reproduction of a constant that will never
; change, and it cannot round differently.
; -----------------------------------------------------------------------------
wsm_v_start:
    xor ax, ax
    cmp word [wsm_cvseg], 0
    je .out
    mov ax, [wsm_ac]                ; the WINDOW, taken here and not only from
    mov [wsm_win], ax               ; the worker's own entry. The worker needs
                                    ; it for TASK_ALIVE (SPEC.md 20.6 rule 2)
                                    ; and for WM_WAKE, and the UI task is the
                                    ; half that certainly has it: start() is
                                    ; called from a handler, which is a
                                    ; callback, which was given the window
    mov bx, [wsm_ab]
    cmp bx, 1
    jb .out
    cmp bx, 18
    ja .out
    mov al, [wsm_fps + bx - 1]
    xor ah, ah
    mov [wsm_sleep], ax
    mov word [wsm_phase], 0
    mov byte [wsm_run], 1
    mov ax, 1
.out:
    jmp wsm_out

wsm_fps:
    db 18, 9, 6, 5, 4, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1

; STOP does not return until the worker has ACKNOWLEDGED, and that is not
; politeness: the UI task frees the canvas claim on a reload, and a frame
; half-composed into freed memory does not fault on this machine - it writes
; over whatever the heap has handed to somebody else. The worker clears ACK at
; the top of every frame and sets it when it has nothing in hand, so spinning
; on it here costs at most one frame.
wsm_v_stop:
    mov byte [wsm_run], 0
    mov byte [wsm_tickp], 0
    mov al, [wsm_rtail]
    mov [wsm_rhead], al             ; and the ring is emptied, so a record
    mov ax, 1                       ; staged against THIS bundle's components
    jmp wsm_out                     ; cannot arrive against the next one's

; -----------------------------------------------------------------------------
; WSMV_UNBIND - the claim is about to be freed.
; -----------------------------------------------------------------------------
wsm_v_unbind:
    mov byte [wsm_run], 0
    mov cx, 40
.spin:
    cmp byte [wsm_ack], 0
    jne .done
    push cx
    mov ax, 1
    call OSAPI_TASK_SLEEP
    pop cx
    loop .spin
.done:
    mov word [wsm_cvseg], 0
    mov word [wsm_nspr], 0
    mov byte [wsm_tickp], 0
    mov byte [wsm_rhead], 0
    mov byte [wsm_rtail], 0
    mov ax, 1
    jmp wsm_out

; -----------------------------------------------------------------------------
; WSMV_DRAIN - one staged record into a 4-word block in the caller's DS.
;
; The UI task owns HEAD and the worker owns TAIL, and neither writes the
; other's byte. That is the whole of the handshake (WEAVE-SPEC 6.10.6): a
; COUNT would have two writers and need a lock, which SPEC.md 20.6 rule 3
; forbids a worker to take. apps/ftpd is the precedent, said about frames
; instead of about bytes.
; -----------------------------------------------------------------------------
wsm_v_drain:
    xor ax, ax
    mov bl, [wsm_rhead]
    cmp bl, [wsm_rtail]
    je .out                         ; head == tail is empty
    mov al, WSM_REC
    mul bl
    add ax, wsm_ring
    mov si, ax
    mov al, [si]                    ; comp
    xor ah, ah
    mov di, ax
    mov al, [si + 1]                ; atom
    xor ah, ah
    mov bp, ax
    cmp al, 57                      ; the ontick leaving the ring RE-ARMS the
    jne .notick                     ; pending flag wsm_stage collapses on
    mov byte [wsm_tickp], 0         ; (6.10.6). Until this line existed the
.notick:                            ; flag was cleared only by bind, stop and
                                    ; unbind, so a running canvas fired ONE
                                    ; ontick per start() and dropped every
                                    ; other - PONG's computer paddle was the
                                    ; first handler whose effect was looked
                                    ; at on the machine. The worker may set
                                    ; the flag again between our read and this
                                    ; clear; that drops one ontick, which is
                                    ; the collapse the rule allows, and needs
                                    ; no lock
    mov cx, [si + 2]                ; data1
    mov dx, [si + 4]                ; data2
    mov si, [wsm_ad]
    mov es, [wsm_cds]
    mov [es:si], di
    mov [es:si + 2], bp
    mov [es:si + 4], cx
    mov [es:si + 6], dx
    mov bl, [wsm_rhead]
    inc bl
    and bl, WSM_RING - 1
    mov [wsm_rhead], bl             ; ...written LAST, which is what frees the
    mov ax, 1                       ; slot only once it has been read
.out:
    jmp wsm_out

; -----------------------------------------------------------------------------
; WSMV_PAINT - the UI task's full repaint of the canvas.
;
; in: [wsm_ab] = screen x, [wsm_ac] = screen y. The gfx lock is HELD by the
;     caller and its clip is already armed (wpaint.c), so this draws and does
;     not lock, which is the one difference from the worker's own emit.
; -----------------------------------------------------------------------------
wsm_v_paint:
    xor ax, ax
    cmp word [wsm_cvseg], 0
    je .out
    call wsm_markall
    mov ax, [wsm_ab]
    mov bx, [wsm_ac]
    call wsm_flush
.out:
    jmp wsm_out

%include "weave/wspr.inc"           ; the composer and the band emit
%include "weave/wwork.inc"          ; the frame loop and the worker

%include "weave/wsmdata.inc"
