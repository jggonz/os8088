; =============================================================================
; os8088 - tests/pinme/pinme.asm
;
; THE SUBJECT OF THE NEGATIVE ARM, and it does nothing else.
;
; docs/plans/HEAP-UNPIN-PLAN.md 10.1 asks for a row that opens a package owning
; a worker, runs the hard pass, and asserts its region did NOT move - because a
; pass that moved it would not fault, it would run the wrong memory. That needs
; a package which is BOTH declared movable and worker-owning, and no shipped
; one is: SHEET declares and hires nobody, and every package that hires a
; worker declares nothing.
;
; IT CANNOT BE tests/filler EITHER, and the reason is the finding behind piece
; C (docs/plans/HEAP-UNPIN-PLAN.md 10.9). A package reaches `mem_claim` only
; from inside its own callback, so when the filler's forcing ask triggers a
; compaction, `[wm_pkgd]` is 1 and `wm_pkgs[0]` is the FILLER'S OWN SEGMENT -
; `mem_frameless` then refuses its region because a frame is inside it, which
; is correct and is not the test. Measured: with the worker pin removed from
; the kernel the filler's region still stood still, and the row stayed green.
;
; So the asker and the subject have to be two packages. The filler asks; this
; one is asked about, and holds still for a different reason.
;
; It declares its region movable at ENTRY and hires the worker at its first
; PAINT - the spawn wants the gfx lock held, which an entry proc does not hold
; and a paint callback does.
;
; Prefix pm_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PINME', pm_entry

PM_BSS    equ 24

; -----------------------------------------------------------------------------
; pm_entry - the window, then the declaration
; -----------------------------------------------------------------------------
pm_entry:
    push si
    mov si, pm_tpl
    call OSAPI_WM_CREATE
    mov [pm_win], bx
    pop si
    mov [pm_self], cs           ; our own segment, cached for pm_reloc alone
    push ax
    push dx
    mov dx, cs                  ; OUR REGION is the claim (SPEC.md 66.6.1);
    mov ax, pm_reloc            ; DX is the instance index on entry, so it is
    call OSAPI_MEM_MOVABLE      ; banked rather than spent
    jc .out
    mov byte [pm_mov], 1        ; a refused declaration is silent from in here
.out:                           ; (SPEC.md 66.5.6.2), so record that it took
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_reloc - our region moved. BX = the base it WAS at, DX = where it is now.
;
; It has nothing it must fix, so it fixes something it does not need to: a
; cached copy of our own segment, and a counter. That is the point - a proc
; with nothing to do cannot be seen to have RUN, and "the region moved and the
; proc was never called" has to be tellable from "the region did not move".
; -----------------------------------------------------------------------------
pm_reloc:
    mov [pm_self], dx
    inc word [pm_nreloc]
    ret

; -----------------------------------------------------------------------------
; pm_worker - hired at the first paint, and it does NOTHING.
;
; Its whole purpose is to exist: `mem_frameless` refuses to move a region whose
; instance owns a worker, because `task_spawn` wrote this segment into the
; worker's frame before it ran an instruction and the stack carries it at a
; depth nothing can find (docs/plans/HEAP-UNPIN-PLAN.md 4.6) - unless the
; package declares a restart point (SPEC.md 66.6.2), which 'R' does.
;
; **IT IS ALSO THE RESTART POINT**, so `[pm_nstart]` counts entries: 1 from the
; spawn, 2 after the kernel has thrown its stack away and re-entered it. And
; `[pm_ntick]` keeps rising afterwards, which is the assertion that matters -
; a restart that produced a task which never runs again would leave the count
; right and the machine one worker short.
;
; Both counters are BSS, so they live in the region and the move carries them.
; -----------------------------------------------------------------------------
pm_worker:
    inc word [pm_nstart]
.loop:
    inc word [pm_ntick]
    mov bx, [pm_win]
    call OSAPI_TASK_ALIVE       ; never returns once the close box is clicked;
                                ; ALSO the park point (SPEC.md 66.5.4), which
                                ; is what makes a restart legal at all
    mov ax, 4
    call OSAPI_TASK_SLEEP
    jmp .loop

; -----------------------------------------------------------------------------
; pm_onkey - 'R' declares the restart point, 'P' withdraws it
; in:  AL = ASCII, AH = scan, SI = window; the gfx lock is HELD
; -----------------------------------------------------------------------------
pm_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or al, 0x20                 ; case-blind
    cmp al, 'r'
    jne .draw
    mov ax, pm_worker           ; the top of the loop, which is the one point
    call OSAPI_TASK_RESTARTABLE ; where this worker holds nothing at all
    jc .draw                    ; refused: [pm_rst] stays 0 and the row sees it
    mov byte [pm_rst], 1
.draw:
    call pm_paint               ; the gfx lock is HELD in a key callback
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_paint - W_PAINT, the gfx lock HELD. The hire happens here, once: the entry
; proc does not hold the lock and OSAPI_TASK_SPAWN requires it.
; -----------------------------------------------------------------------------
pm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [pm_wk], 0
    jne .draw
    mov ax, pm_worker
    mov bx, [pm_win]
    call OSAPI_TASK_SPAWN       ; refused is a normal outcome (the task table
    jc .draw                    ; is full): [pm_wk] stays 0 and the row sees it
    mov byte [pm_wk], 1
.draw:
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, 6
    mov cx, ax
    add dx, 6
    mov si, pm_s_mov
    cmp byte [pm_mov], 0
    jne .say
    mov si, pm_s_no
.say:
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pm_tpl:
    dw 300, 26, 128, 40
    dw pm_ttl, pm_paint, pm_onkey, 0

pm_ttl:    db 'PinMe', 0
pm_s_mov:  db 'movable', 0
pm_s_no:   db 'REFUSED', 0

    OS88_BSS PM_BSS
    OS88_IMAGE_END

pm_win     equ os88_image_end + 0    ; word: our window
pm_self    equ os88_image_end + 2    ; word: our own segment, for pm_reloc
pm_nreloc  equ os88_image_end + 4    ; word: times pm_reloc was called
pm_wk      equ os88_image_end + 6    ; byte: the worker has been hired
pm_mov     equ os88_image_end + 7    ; byte: the declaration took
pm_nstart  equ os88_image_end + 8    ; word: times pm_worker has been ENTERED
pm_ntick   equ os88_image_end + 10   ; word: ...and times round its loop
pm_rst     equ os88_image_end + 12   ; byte: the restart point is declared
