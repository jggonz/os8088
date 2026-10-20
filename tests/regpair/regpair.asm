; =============================================================================
; os8088 - tests/regpair/regpair.asm
;
; THE CANONICAL PAIR, as a fixture rather than as a game. tests/regapp.py
; checks that a package's heap region really is relocatable - MC_RLOC set by
; OS88_REGION_MOVABLE, and inst_restart set by OS88_WORKER_RESTARTABLE - and
; SPEC.md 66.6.1.1 is why both halves need a case:
;
;   rloc and NO worker      Calc. Movable on I_TASK == 0xFF alone.
;   rloc and a worker and a restart point    <- THIS, and it is the pair.
;
; The five application rows in that table (Word, Tank, ftpd, Browser, Audio)
; look like they cover the second line and do not: ftpd hires only when the
; card is up and Audio only when playback starts, so on regapp's machine -
; MartyPC, which has no NIC - neither ever hires at all. PACMAN.O88 was the
; case that reliably did, and it is RETIRED (SPEC.md 89.12, apps/RETIRED.txt),
; so the shape needs a fixture that is not a shipping program.
;
; WHAT MAKES IT THE PAIR IS *WHERE* THE WORKER IS HIRED. It is spawned from the
; PAINT and not from the entry proc, which is the harder order and the one that
; found the defect: the kernel writes our segment into a worker's frame before
; its first instruction (SPEC.md 66.6.2), so mem_frameless pins a region with
; an undeclared worker HOWEVER that region is declared - and a package that
; declares movable at the entry and hires later was exactly the run that used
; to declare nothing. Hiring from the paint also means regapp's ten-second wait
; is doing real work here: it is what gives this package a frame to hire in.
;
; IT ASSERTS NOTHING, which is tests/filler/filler.asm's rule for the same
; reason - an instrument that can fail its own assertions cannot be used to
; set up somebody else's. It opens a window, hires one worker, and the row
; driving it reads MC_RLOC and inst_restart out of the kernel's own tables.
;
; NOT SHIPPED and nothing in `all` builds it: tests/regpair is built by
; $(BUILD)/regapp360.img alone, which is regapp's own disk.
;
; Prefix rp_.
; =============================================================================

%include "os88api.inc"

    ; FLAGS 0 AND NOT 1: bit 0 means "embedded icon", and os88pkg then
    ; requires the entry at +0x60 or past it, which is where an OS88_ICON16
    ; block would have put it. A fixture wants no icon - it is opened by name
    ; from a script and never looked for on a desktop - so the flags byte says
    ; so and the entry sits at +0x20. Claiming the bit without the block is
    ; refused at pack time, which is how this was caught.
    OS88_HEADER 'REGPAIR', rp_entry, 0, OS88_STACK_256

RP_BSS equ 6

; -----------------------------------------------------------------------------
; rp_entry - just the window. THE WORKER IS NOT HIRED HERE, deliberately.
; -----------------------------------------------------------------------------
rp_entry:
    push si
    mov si, rp_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [rp_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1) - here, where the window exists,
    ; and NOT beside the spawn. That placement is the whole point of this
    ; fixture: the declaration and the hire are deliberately at different
    ; moments, so a kernel that only honours them together fails this row.
    OS88_REGION_MOVABLE
    clc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; rp_paint - W_PAINT, and where the worker is hired
; in:  SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
rp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [rp_hired], 0
    jne .draw
    mov ax, rp_worker
    mov bx, [rp_win]
    call OSAPI_TASK_SPAWN
    jc .draw                        ; no slot: the next paint retries, which is
    mov byte [rp_hired], 1          ; pacman's own behaviour here
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2). What a
    ; restart costs is one pass of the loop below: the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else, which is the TOP of that loop, and
    ; this package keeps nothing across a pass.
    OS88_WORKER_RESTARTABLE rp_worker
.draw:
    mov bx, si
    call OSAPI_WM_CONTENT
    mov cx, ax                      ; OSAPI_WM_CONTENT answers AX = left,
    add cx, 8                       ; DX = top, so DX is already where we want
    add dx, 8                       ; to measure down from
    mov si, rp_s_line
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN             ; font_run and not a fill-then-letter pair
    pop si                          ; (SPEC.md 6.1)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_worker - one pass a tick, and the park is at the TOP
; -----------------------------------------------------------------------------
rp_worker:
.loop:
    mov bx, [rp_win]
    call OSAPI_TASK_ALIVE           ; may terminate; never under the lock, and
                                    ; this is the one place a restart lands
    mov ax, 1
    call OSAPI_TASK_SLEEP
    inc word [rp_passes]            ; the only state, and it does not outlive
    jmp .loop                       ; a pass in any way that matters

rp_tpl:
    dw 120, 40, 200, 60
    dw rp_title, rp_paint, 0, 0

rp_title:   db 'Reg Pair', 0
rp_s_line:  db 'region + worker', 0

    OS88_BSS RP_BSS
    OS88_IMAGE_END

rp_win      equ os88_image_end + 0      ; word
rp_passes   equ os88_image_end + 2      ; word: worker passes, reported by none
rp_hired    equ os88_image_end + 4      ; byte: the worker exists
