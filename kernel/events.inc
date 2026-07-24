; =============================================================================
; jop - events.inc
;
; The system event queue (SPEC.md 10): a single ring of 16 fixed 8-byte
; records in .bss. Producers may be ISRs (the mouse ISR pushes button
; events with IF=0), consumers are tasks (the UI task pops), so both push
; and pop guard their whole read-modify-write with pushf/cli ... popf -
; the caller's interrupt flag is preserved and never a bare sti.
; =============================================================================

; --- event record layout (8 bytes) -------------------------------------------
EV_TYPE     equ 0               ; word: EVT_*
EV_A        equ 2               ; word: payload a (mouse events: x)
EV_B        equ 4               ; word: payload b (mouse events: y)
EV_C        equ 6               ; word: payload c (unused so far)

EVT_NONE    equ 0
EVT_MDOWN   equ 1               ; a=x, b=y
EVT_MUP     equ 2               ; a=x, b=y

EVQ_RECSIZE equ 8               ; bytes per record
EVQ_CAP     equ 16              ; records in the ring
EVQ_MASK    equ (EVQ_RECSIZE * EVQ_CAP) - 1     ; byte-offset wrap mask (0x7F)

; -----------------------------------------------------------------------------
; evq_init - reset the queue to empty
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
evq_init:
    mov word [evq_head], 0
    mov word [evq_tail], 0
    mov word [evq_count], 0
    ret

; -----------------------------------------------------------------------------
; evq_push - append one event record to the queue
; in:  SI -> 8-byte record (in DS)
; out: nothing; drops the event silently when the queue is full
; clobbers: nothing (flags only)
; ISR-safe: the copy + index update sit inside pushf/cli ... popf, so a
; caller running with IF=0 (the mouse ISR) stays that way throughout.
; -----------------------------------------------------------------------------
evq_push:
    push ax
    push cx
    push si
    push di
    push es
    pushf
    cli

    cmp word [evq_count], EVQ_CAP
    jae .full                   ; full: drop silently

    mov ax, ds                  ; copy runs DS:SI -> ES:DI within the kernel
    mov es, ax
    mov di, [evq_tail]
    add di, evq_buf
    cld
    mov cx, EVQ_RECSIZE / 2
    rep movsw

    mov ax, [evq_tail]
    add ax, EVQ_RECSIZE
    and ax, EVQ_MASK
    mov [evq_tail], ax
    inc word [evq_count]

.full:
    popf
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; evq_pop - remove the oldest event record from the queue
; in:  DI -> 8-byte destination buffer (in DS)
; out: CF=0 and the record copied to [DI]; CF=1 if the queue was empty
;      (destination untouched)
; clobbers: nothing but CF
; The whole read-modify (copy + index update) sits inside pushf/cli ... popf
; so a concurrent ISR push can never interleave with it.
; -----------------------------------------------------------------------------
evq_pop:
    push ax
    push cx
    push si
    push di
    push es
    pushf
    cli

    cmp word [evq_count], 0
    je .empty

    mov ax, ds                  ; copy runs DS:SI -> ES:DI within the kernel
    mov es, ax
    mov si, [evq_head]
    add si, evq_buf
    cld
    mov cx, EVQ_RECSIZE / 2
    rep movsw

    mov ax, [evq_head]
    add ax, EVQ_RECSIZE
    and ax, EVQ_MASK
    mov [evq_head], ax
    dec word [evq_count]

    popf                        ; restore caller's IF first, then report
    clc
    jmp .done

.empty:
    popf
    stc

.done:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

evq_buf:    resb EVQ_RECSIZE * EVQ_CAP  ; the ring itself (128 bytes)
evq_head:   resw 1                      ; byte offset of the oldest record
evq_tail:   resw 1                      ; byte offset of the next free slot
evq_count:  resw 1                      ; records currently queued (0..EVQ_CAP)

section .text
