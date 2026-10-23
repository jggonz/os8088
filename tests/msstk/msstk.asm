; =============================================================================
; os8088 - tests/msstk/msstk.asm
;
; MSSTK, a TEST instrument (nothing under tests/ ships): the mouse ISR taken
; on a stack that is NOT LOW_SEG (SPEC.md 9.10.1, docs/plans/KERN-MEGA-PLAN.md
; 14.39). SeaBIOS runs int 13h and int 16h on a stack in a segment of its own,
; and the kernel calls both with IF = 1, so the mouse ISR can arrive with SS
; anywhere - but only by a race no emulator here wins on demand. This makes
; the case on purpose: SS:SP moved into an 8KB claim filled with A5h, IRQ0
; masked so the scheduler cannot switch away while SS is ours (a task record
; keeps SP and not SS), IF = 1, and a spin for the harness's mouse packets.
; The ISR's BODY reaches .lowbss through SS - gfx_rowbase's row table,
; evq_push's ring - so a body left on this stack WRITES THE CLAIM at those
; offsets, far below the few bytes the CPU's own frame takes at its top.
;
; The handshake is the BIOS's intra-application area at 0040:00F0, which
; nothing else on this machine uses:
;
;   F0  'W' the window is open (SS is the claim's)   'D' done   'E' refused
;   F2  word: bytes of the claim below its top 256 that are not A5h
;   F4  word: the lowest offset that changed, FFFFh = none
;
; The window is a bounded spin, about eight seconds on a 4.77 MHz 8088, and
; the harness sends its packets inside it; the verdict is written after. Runs on any kernel that hooks the serial
; mouse; kern_mega's identity grid holds 0040h, LOW0 aside, so it is not for
; that kernel.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MSSTK', ms_entry

MS_KB       equ 8
MS_TOP      equ MS_KB * 1024
MS_FRAME    equ 256                 ; the top: the CPU's frames, and the
                                    ; keyboard ISR's, which runs in place

ms_entry:
    push si
    push di
    push bp
    push es
    mov ax, 0x40
    mov es, ax
    mov byte [es:0xF0], 'E'
    mov ax, MS_KB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [ms_seg], dx
    push es
    mov es, dx                      ; the claim, A5h throughout
    xor di, di
    mov cx, MS_TOP / 2
    mov ax, 0xA5A5
    cld
    rep stosw
    pop es
    cli
    in al, 0x21
    mov [ms_imr], al
    or al, 1                        ; IRQ0 off: no task switch with SS ours
    out 0x21, al
    mov [ms_ss], ss
    mov [ms_sp], sp
    mov ss, dx                      ; SS:SP = the claim's top
    mov sp, MS_TOP
    mov byte [es:0xF0], 'W'
    sti
    mov bx, 32                      ; ~7 s at 4.77 MHz
.spin:
    xor cx, cx
.in:
    loop .in
    dec bx
    jnz .spin
    cli
    mov ss, [ms_ss]
    mov sp, [ms_sp]
    mov al, [ms_imr]
    out 0x21, al
    sti
    push ds                         ; --- what the window left in the claim
    mov ds, [ms_seg]
    xor si, si
    xor dx, dx                      ; DX = bytes changed
    mov bx, 0xFFFF                  ; BX = the lowest that did
.scan:
    cmp byte [si], 0xA5
    je .same
    inc dx
    cmp bx, 0xFFFF
    jne .same
    mov bx, si
.same:
    inc si
    cmp si, MS_TOP - MS_FRAME
    jb .scan
    pop ds
    mov [es:0xF2], dx
    mov [es:0xF4], bx
    mov dx, [ms_seg]
    call OSAPI_MEM_FREE
    mov byte [es:0xF0], 'D'
.out:
    pop es
    pop bp
    pop di
    pop si
    stc                             ; no window: the instance ends here
    ret

ms_seg      dw 0
ms_ss       dw 0
ms_sp       dw 0
ms_imr      db 0

    OS88_BSS 0
    OS88_IMAGE_END
