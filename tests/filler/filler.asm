; =============================================================================
; os8088 - tests/filler/filler.asm
;
; THE INSTRUMENT WITH NO OPINIONS: a package whose only job is to take the heap
; down to a few tens of KB and then, on a keypress, ask for one KB more than
; the largest free run. 'S' asks WITHOUT filling first, for a row that has
; built the arena itself and wants it left exactly as it is (fl_onkey).
;
; It exists because tests/heapfrag cannot be that instrument. heapfrag's comb
; is sized L/8 from the largest run IT sees and its twelve assertions are about
; the arena it expects to own - so with another package's claims interleaved,
; its own checks 8 and 11 fail and a run in which the forcing claim was refused
; looks exactly like a run in which it was granted and did nothing. An
; instrument that can fail its own assertions cannot be used to set up somebody
; else's (docs/plans/HEAP-UNPIN-PLAN.md 10.9).
;
; So this one asserts NOTHING. It reports four numbers - free KB, blocks held,
; asks made, asks granted - and the row driving it reads whatever it is
; actually about out of the kernel.
;
; WHY THE FILL IS ONE PINNED CLAIM. Undeclared is the default (SPEC.md 66.2),
; and that is what is wanted here: a filler the ascending pass could pack would
; hand it exactly the room the test is trying to deny it.
;
; WHY THE ASK IS A LOOP. `avail + 1` is more than any single run, so a grant
; means a compaction happened. The first ask is usually granted by the
; ASCENDING pass, which is not the pass under test; each grant is given
; straight back and the next ask is made against a more packed arena, so the
; loop walks the ascending pass to exhaustion and the last ask is the one that
; reaches whatever comes after it.
;
; Prefix fl_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FILLER', fl_entry

FL_LEAVE  equ 8                 ; KB to leave in the largest run after a fill
FL_HOLD   equ 6                 ; ...and how many fill claims may be held
                                ; (MEM_OWNER_MAX is 8 and the region is one)
FL_STEP   equ 4                 ; ...and the ladder the forcing ask walks down
FL_FLOOR  equ 8                 ; KB, stopping here
FL_BSS    equ 40

; -----------------------------------------------------------------------------
; fl_entry - just the window; the fill runs on the first paint
; -----------------------------------------------------------------------------
fl_entry:
    push si
    mov si, fl_tpl
    call OSAPI_WM_CREATE
    mov [fl_win], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; fl_fill - take the largest run down to FL_LEAVE, and HOLD it
;
; ONE claim per call, and the caller calls it again. That is not laziness: the
; ASCENDING pass a forcing ask triggers packs the other claims on the machine
; down and leaves a fresh hole where they were, bigger than anything a ceiling
; merge could produce - so an arena filled once is not filled after the first
; ask. Alternating a fill with an ask converges: each fill mops up what the
; last compaction freed, until the only room left is the ceiling's.
; -----------------------------------------------------------------------------
fl_fill:
    push ax
    push bx
    push dx
    mov bx, [fl_nheld]
    cmp bx, FL_HOLD
    jae .out
    call OSAPI_MEM_AVAIL        ; AX = largest run KB
    cmp ax, FL_LEAVE + 8
    jbe .out                    ; nothing worth taking
    sub ax, FL_LEAVE
    call OSAPI_MEM_CLAIM
    jc .out
    shl bx, 1
    mov [bx+fl_seg], dx
    inc word [fl_nheld]
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fl_ask - A DESCENDING LADDER of asks, taking the first that is granted
;
; NOT `avail + 1`, and the difference is the whole instrument. `avail + 1` is
; more than any single run, so it forces A compaction - but the ASCENDING pass
; usually funds it, and mem_cp_worth then never asks the descending one
; (SPEC.md 66.4.1). Nor can the ask simply be "everything free": mem_cp_worth
; refuses to run a pass that will not reach the number, so an unreachable ask
; moves nothing at all.
;
; So the ladder starts just under the total and steps down FL_STEP KB at a
; time, and the FIRST GRANT is the largest claim this machine can fund - which
; is reached by whichever passes it took, descending included. The block is
; freed immediately: the point is the compaction, not the memory.
;
; [fl_nask], [fl_nok] and [fl_got] are what the window shows and what a row
; reads; this package asserts nothing itself.
; -----------------------------------------------------------------------------
fl_ask:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_MEM_AVAIL        ; AX = the largest run, BX = TOTAL free
    mov cx, bx
    sub cx, 2                   ; start just under everything there is
.lp:
    cmp cx, FL_FLOOR
    jbe .out                    ; nothing left worth asking for
    mov ax, cx
    inc word [fl_nask]
    call OSAPI_MEM_CLAIM
    jnc .got
    sub cx, FL_STEP
    jmp short .lp
.got:
    inc word [fl_nok]
    mov [fl_got], cx            ; the largest claim this machine could fund
    call OSAPI_MEM_FREE         ; DX is still the base
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fl_onkey - W_ONKEY: any key makes one round of asks, and 'S' makes HALF a one
; in:  AL = ASCII, AH = scan, SI = window; the gfx lock is HELD
;
; 'S' ASKS WITHOUT FILLING, and the difference matters to any row that builds
; its own arena. The fill is first fit ASCENDING, so it lands in the lowest run
; big enough - which for a row that has deliberately opened a hole ABOVE its
; subject is that very hole, and the claim is undeclared and therefore PINNED
; against the block the ask is about to need moved. `tests/sndmove.py` opens
; that hole by mounting a driver over the sound driver and dropping it again;
; one round of fill-then-ask sealed it every time, and the ask that followed
; was refused for want of the room the fill had just taken.
;
; So a row that arrives at a fresh arena presses any key (fill, then ask), and
; a row that has already built the arena it wants presses 'S'.
; -----------------------------------------------------------------------------
fl_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or al, 0x20                 ; either case
    cmp al, 's'
    je .ask                     ; ...ask only: the arena is the row's
    call fl_fill                ; mop up whatever the LAST round's compaction
.ask:
    call fl_ask                 ; freed, and only then ask again
    call fl_paint               ; THE GFX LOCK IS HELD in a key callback
                                ; (SPEC.md 13), so the window is redrawn here
                                ; rather than through a repaint request - and
                                ; fl_paint's own fill has already run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fl_num - AX as decimal at (CX, BP), heapfrag's own OSAPI_FONT_RUN idiom:
; CX = x, DX = y, SI = the string, AL = ink and AH = the ground (SPEC.md 6.1).
; -----------------------------------------------------------------------------
fl_num:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, fl_buf + 5
    mov byte [si], 0
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .d
    mov dx, bp
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fl_paint - W_PAINT. The fill happens here, once, for heapfrag's reason: the
; entry proc runs before the window is shown and a claim there is a claim the
; loader is still standing in.
; -----------------------------------------------------------------------------
fl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [fl_done], 0
    jne .draw
    mov byte [fl_done], 1
    push si
    call fl_fill
    pop si
.draw:
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, 6
    mov di, ax
    add dx, 6
    mov bp, dx

    mov cx, di
    mov dx, bp
    mov si, fl_s_free
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov cx, di
    add cx, 48
    call OSAPI_MEM_AVAIL
    call fl_num

    add bp, 12
    mov cx, di
    mov dx, bp
    mov si, fl_s_ask
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov cx, di
    add cx, 48
    mov ax, [fl_nask]
    call fl_num
    mov cx, di
    add cx, 88
    mov ax, [fl_got]
    call fl_num

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fl_tpl:
    dw 300, 26, 176, 60
    dw fl_ttl, fl_paint, fl_onkey, 0

fl_ttl:    db 'Filler', 0
fl_s_free: db 'free', 0
fl_s_ask:  db 'asks', 0

    OS88_BSS FL_BSS
    OS88_IMAGE_END

fl_win     equ os88_image_end + 0    ; word: our window
fl_seg     equ os88_image_end + 2    ; FL_HOLD words: the fill claims
fl_nheld   equ os88_image_end + 14   ; word: how many of them are taken
fl_nask    equ os88_image_end + 16   ; word: asks made
fl_nok     equ os88_image_end + 18   ; word: ...and granted
fl_done    equ os88_image_end + 20   ; byte: the first fill has run
fl_got     equ os88_image_end + 22   ; word: KB of the largest grant
fl_buf     equ os88_image_end + 24   ; 6 bytes: the number formatter
