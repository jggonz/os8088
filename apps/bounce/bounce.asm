; =============================================================================
; os8088 - apps/bounce/bounce.asm
;
; BOUNCE (SPEC.md 112): an 8x8 black square bouncing off the walls of its own
; 150x130 window, three pixels across and two down per step, one step every
; two ticks. It was the kernel's Bounce KIND (SPEC.md 14) until SPEC.md 14.8,
; and SPEC.md 20.6 pointed every package author at its task as THE REFERENCE
; IMPLEMENTATION of a worker. It is one now in fact rather than by analogy:
; this file is that loop written through the SDK, and it is still the
; smallest complete worker in the tree - open, then sleep / alive-check /
; lock / visible? / clip / draw / unlock.
;
; THE STEP IS UNCONDITIONAL and only the drawing is not (SPEC.md 11.3, 14):
; under the lock the worker re-checks that the window is visible and asks
; OSAPI_WM_CLIP_SET for the region nothing covers. CF = 0: the two fills are
; cut to it, so a half-covered Bounce shows the square crossing the half you
; can see. CF = 1, or hidden: it steps and draws nothing, which is what makes
; a covered or minimized Bounce come back where it NOW is rather than where it
; was buried - and that is safe precisely because the paint proc draws from
; state.
;
; THE NEW SQUARE FIRST, THEN ONLY THE PART OF THE OLD ONE IT DOES NOT COVER
; (SPEC.md 14.5). Erase-then-draw wrote every pixel the two squares share
; white and then black, once a frame, and the glass caught it as flicker: for
; ~0.5 ms of every step there was no square on screen at all. bn_wipe is the
; subtraction - an L of two strips - and it is short because the step is
; smaller than the square, which an assembly-time %error holds.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Bounce', bn_entry, 1, OS88_STACK_128
                                    ; flags bit 0: the icon below. The worker
                                    ; sleeps and draws two fills: the kind's
                                    ; task measured +10..24 over the floor on
                                    ; a 128-byte slice, six at once (SPEC.md
                                    ; 8.7), and that is the class it asks for
    OS88_ICON16                     ; the kind's icon (SPEC.md 29.3): a ball,
    ; --- mask ---                  ; its trajectory and the floor it will
    dw 0x7E00, 0xFF00, 0xFF00, 0xFF00   ; come off
    dw 0xFF80, 0x7FC0, 0x03E0, 0x01F0
    dw 0x00F8, 0x007C, 0x003E, 0x001F
    dw 0x000F, 0xFFFF, 0xFFFF, 0xFFFF
    ; --- data ---
    dw 0x0000, 0x3C00, 0x7E00, 0x7E00
    dw 0x3C00, 0x0100, 0x0080, 0x0040
    dw 0x0020, 0x0010, 0x0008, 0x0004
    dw 0x0002, 0x0000, 0x7FFE, 0x0000
    OS88_ICON16_END

; --- geometry (SPEC.md 112.1) ------------------------------------------------
BN_W        equ 150                 ; frame
BN_H        equ 130
BN_CW       equ BN_W - 2            ; content: 148 x 111
BN_CH       equ BN_H - TITLE_H - 1
BN_SQ       equ 8                   ; the square
BN_XMAX     equ BN_CW - BN_SQ       ; the square's far positions, content-relative
BN_YMAX     equ BN_CH - BN_SQ
BN_VX0      equ 3                   ; the opening velocity...
BN_VY0      equ 2
BN_X0       equ 4                   ; ...and position
BN_PERIOD   equ 2                   ; ticks between steps
%if BN_VX0 >= BN_SQ || BN_VY0 >= BN_SQ
%error "bn_wipe: a Bounce step must stay under the square's width"
%endif

; -----------------------------------------------------------------------------
; bn_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the file we came out of,
;      IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; NOTHING IS SPAWNED HERE: OSAPI_TASK_SPAWN refuses before the loader has
; published the instance, so the worker starts at the first paint (bn_spawn_ck).
; -----------------------------------------------------------------------------
bn_entry:
    push si
    mov si, bn_tpl                  ; the kind's window, where the kind put it
                                    ; (a second Bounce opens on the first, as a
                                    ; second Note Pad does; the kind cascaded by
                                    ; pool slot and a package has no pool)
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov [bn_win], bx
    OS88_REGION_MOVABLE             ; SPEC.md 66.6.1: the worker declares
                                    ; itself restartable at its spawn
    mov si, bn_about                ; the standard About (SPEC.md 12.2, 20.5.1)
    call OSAPI_ABOUT_SET
    mov ax, BN_X0                   ; fresh Bounce: the square at (4,4) going
    mov [bn_pos], ax                ; (3,2) - the kind's seed, SPEC.md 14
    mov [bn_pos+2], ax
    mov word [bn_vel], BN_VX0
    mov word [bn_vel+2], BN_VY0
    clc                             ; the CF the loader is owed
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; bn_paint - W_PAINT: the square where it now is, the About card last
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
bn_paint:
    push bx
    push si
    mov bx, si
    call bn_spawn_ck                ; the worker starts here, not at the entry
    call bn_fill                    ; black: the fill sets it
    cmp byte [bn_abon], 0
    je .out
    mov bx, si
    mov si, bn_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; bn_spawn_ck - start the worker, once, from a window callback with the lock
;               held - which is what OSAPI_TASK_SPAWN requires
; in:  the gfx lock is held. Preserves every register.
; A refusal is TRANSIENT - the task table is full - so the flag is set only on
; success and the next paint tries again.
; -----------------------------------------------------------------------------
bn_spawn_ck:
    cmp byte [bn_spawned], 0
    jne .out
    push ax
    push bx
    mov ax, bn_worker
    mov bx, [bn_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [bn_spawned], 1
    OS88_WORKER_RESTARTABLE bn_worker ; SPEC.md 66.6.2: the kernel wrote our
                                    ; segment into this worker's frame; the
                                    ; park is inside OSAPI_TASK_ALIVE at the top
                                    ; of the loop, and every byte that outlives
                                    ; a pass is a static that moves with us
.no:
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; bn_worker - the per-instance task: step every BN_PERIOD ticks, draw where
;             the window can be seen (SPEC.md 20.6 - this IS the reference)
; in:  DX = our instance index; never returns
; -----------------------------------------------------------------------------
bn_worker:
.loop:
    mov bx, [bn_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here; a clicked
                                    ; close box never returns (20.6 rule 2)
    mov ax, BN_PERIOD
    call OSAPI_TASK_SLEEP
    call OSAPI_GFX_LOCK             ; 20.6 rule 5's sequence, in order: lock,
    mov bx, [bn_win]                ; re-check visibility UNDER the lock, arm
    mov ax, KERNEL_SEG              ; the clip
    mov es, ax
    test word [es:bx+W_FLAGS], 2
    jz .blind
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one visible pixel (SPEC.md
    jc .blind                       ; 11.3)
    mov cx, [bn_pos]                ; bank where the square is ON THE GLASS
    mov dx, [bn_pos+2]
    call bn_step
    call bn_fill                    ; the NEW square FIRST, black...
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call bn_wipe                    ; ...then the part of the old one it does
    jmp short .unlock               ; not cover (SPEC.md 14.5)
.blind:
    call bn_step                    ; nothing on screen to keep in step with
.unlock:
    call OSAPI_GFX_UNLOCK           ; ...which also disarms the clip
    jmp .loop

; -----------------------------------------------------------------------------
; bn_step - advance the square, bouncing off the content edges
; out: nothing; preserves all registers
; The two axes are the same eleven instructions with a different offset and a
; different wall; a bounce only ever SHORTENS a step (the clamp), which is
; what lets bn_wipe read the real delta from the positions.
; -----------------------------------------------------------------------------
bn_step:
    push ax
    push bx
    push dx
    xor bx, bx                      ; x: offset 0
    mov dx, BN_XMAX
    call .axis
    mov bx, 2                       ; y: offset 2
    mov dx, BN_YMAX
    call .axis
    pop dx
    pop bx
    pop ax
    ret
.axis:                              ; in BX = the axis's offset, DX = its wall
    mov ax, [bn_pos+bx]
    add ax, [bn_vel+bx]
    jns .high
    xor ax, ax                      ; hit the low wall
    jmp short .wall
.high:
    cmp ax, dx
    jle .done
    mov ax, dx                      ; hit the high wall
.wall:
    neg word [bn_vel+bx]            ; ...and either wall turns it round
.done:
    mov [bn_pos+bx], ax
    ret

; -----------------------------------------------------------------------------
; bn_fill - the 8x8 square at the current position, black
; in:  BX = window ptr; gfx lock held
; out: nothing; preserves all registers. SETS the colour to CBLACK.
; -----------------------------------------------------------------------------
bn_fill:
    push ax
    push bx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add ax, [bn_pos]                ; x1
    add dx, [bn_pos+2]
    mov bx, dx                      ; y1
    mov cx, ax
    add cx, BN_SQ - 1               ; x2
    add dx, BN_SQ - 1               ; y2
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bn_wipe - white out the part of the OLD square the new one does not cover
;           (SPEC.md 14.5)
; in:  BX = window ptr; CX = the OLD content x, DX = the OLD content y; bn_pos
;      already stepped (the NEW position); the colour set to the background
; out: nothing; preserves all registers
;
; Two equal squares offset by (ddx, ddy) leave an L: a COLUMN strip |ddx| wide
; over the old square's full height on the trailing side, plus a ROW strip
; |ddy| tall over the columns the two share. Either can be empty and both are
; skipped then; nothing is written twice, and nothing is ever absent, because
; the caller has already drawn the new square. With |ddx| and |ddy| under 8
; the strips need no clamping and the no-overlap case cannot arise.
;
; THE SIGN IS TESTED BEFORE THE ARITHMETIC THAT WOULD OVERWRITE THE FLAGS:
; the kind's first version tested the flags of the `dec` that had just
; computed the strip's edge, and every step that went left erased an inverted
; rect - a solid diagonal trail, which assembled and looked deliberate.
; -----------------------------------------------------------------------------
bn_wipe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, [bn_pos]
    sub si, cx                      ; SI = ddx, new - old
    mov bp, [bn_pos+2]
    sub bp, dx                      ; BP = ddy
    mov ax, si
    or ax, bp
    jz .out                         ; a step the clamps ate entirely
    push cx
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add cx, dx
    mov [bn_oldy], cx               ; the old square, ABSOLUTE: its y...
    pop cx
    add cx, ax
    mov di, cx                      ; DI = ...and its x, for the whole routine
    ; --- the column strip, full height ---------------------------------------
    or si, si
    jz .rows
    mov ax, di
    mov cx, ax
    add cx, si
    dec cx                          ; moved right: from the old left edge
    or si, si                       ; ...and the TEST is of ddx, not of the
    jg .colgo                       ; edge just computed
    add ax, BN_SQ
    add ax, si                      ; moved left: from the far side back
    mov cx, di
    add cx, BN_SQ - 1
.colgo:
    mov bx, [bn_oldy]
    mov dx, bx
    add dx, BN_SQ - 1
    call OSAPI_GFX_FILL
    ; --- the row strip, over the SHARED columns ------------------------------
.rows:
    or bp, bp
    jz .out
    mov ax, di
    mov cx, ax
    add cx, BN_SQ - 1
    or si, si
    jz .ry
    jg .rright
    add cx, si                      ; moved left: they end earlier
    jmp short .ry
.rright:
    add ax, si                      ; moved right: they start later
.ry:
    mov bx, [bn_oldy]               ; the old y, read ONCE: test ddy's sign
    mov dx, bx                      ; before the arithmetic
    or bp, bp
    jg .rdown
    add bx, BN_SQ
    add bx, bp                      ; moved up: from the bottom back
    add dx, BN_SQ - 1
    jmp short .rowgo
.rdown:
    add dx, bp                      ; moved down: from the old top edge
    dec dx
.rowgo:
    call OSAPI_GFX_FILL
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; 'About Bounce' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; bn_about - the OSAPI_ABOUT_SET handler
; in:  SI = our window ptr; the UI task, gfx lock HELD
bn_about:
    mov byte [bn_abon], 1
    mov bx, si
    mov si, bn_ablines
    jmp os88ui_about                ; arms the clip itself; a tail call

; bn_abdismiss - take the card down if it is up. ALSO the window's W_ONCLICK,
;                named in bn_tpl: this program has no other use for a click.
; in:  SI = our window ptr; gfx lock held (CX/DX = the click, unused)
; out: CF = 1 the click was spent on the card; clobbers AX and BX, which a
;      W_ONCLICK may
bn_abdismiss:
    mov bx, bn_abon
    mov ax, bn_repaint
    jmp os88ui_abdismiss            ; cmp/clear/clip/repaint/answer: the
                                    ; library's, and CF is its

; bn_repaint - erase the content and draw it again
; in:  SI = BX = window ptr; gfx lock held, a clip region armed
; out: nothing; preserves all registers
bn_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx                      ; y1 = top
    mov cx, ax
    add cx, BN_CW - 1               ; x2
    add dx, BN_CH - 1               ; y2
    call OSAPI_GFX_FILL             ; AX = x1 already
    call bn_paint                   ; SI = window ptr still
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- data ----------------------------------------------------------------------
; window template (SPEC.md 11: 16 bytes, 8 words), the kind's: 300,150 keeps
; the whole cap-10 cascade on a VGA screen, and wm_fit clamps a shorter one
bn_tpl:     dw 300, 150, BN_W, BN_H
            dw bn_ttl, bn_paint, 0, bn_abdismiss

bn_ttl:     db 'Bounce', 0

; the standard About card's lines (SPEC.md 20.5.1): 2*7 + lines*12 tall in a
; 111px content, so up to four
bn_ablines:
    dw bn_ab1, bn_ab2, 0
bn_ab1:     db 'Bounce for os8088', 0
bn_ab2:     db 'a square, bouncing', 0

; --- the shared controls (SPEC.md 20.5.1) ---------------------------------------
%define OS88UI_ABOUT            ; the standard About card, and NOTHING else -
%define OS88UI_NOBTN            ; this package draws no button at all
%define OS88UI_ABDISMISS        ; ...and takes the card down with os88ui_abdismiss
%include "os88ui.inc"

    OS88_BSS 14
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) --------------------------------------
bn_win      equ os88_image_end + 0  ; word: our window
bn_pos      equ os88_image_end + 2  ; 2 words: the square, content-relative x, y
bn_vel      equ os88_image_end + 6  ; 2 words: its velocity
bn_oldy     equ os88_image_end + 10 ; word: bn_wipe's old square's absolute y
bn_spawned  equ os88_image_end + 12 ; byte: the worker is running
bn_abon     equ os88_image_end + 13 ; byte: the About card is up
%if bn_vel - bn_pos != 4
%error "bn_step reads each axis's velocity at bn_vel - bn_pos from its position"
%endif
