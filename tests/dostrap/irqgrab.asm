; =============================================================================
; os8088 - tests/dostrap/irqgrab.asm
;
; **A DOS PROGRAM TAKING IRQ4, WHICH IT IS ENTITLED TO DO** (SPEC.md 96.45.4).
;
; Battle Chess does exactly this and does it unconditionally: its serial link
; writes `int 0Ch`'s vector DIRECTLY and its handler chains to nobody - LSR,
; data register, keep the byte, EOI, iret. On `kern_dos` that stopped
; `kd_mou_isr` for the whole session until `kd_mou_rearm`; in the WINDOWED box
; the displaced ISR is the KERNEL's `mou_isr`, and this probe is how that half
; is measured rather than reasoned about.
;
; It does the smallest honest version of what the game does - take the vector,
; point it at a handler that swallows the byte and EOIs - and then BLOCKS on
; `INT 21h AH=08h`, which is the window the harness needs: it moves the
; pointer and reads the kernel's own `mouse_x` while the vector is stolen.
;
; It puts the vector back before it exits, so a machine that runs this is not
; left broken by it - the box restores the whole IVT at the bracket's end
; anyway (`dos_restore_machine`), but a probe that relies on that is a probe
; that cannot be run twice in one session.
;
; NOTHING HERE IS THIRD-PARTY: ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

VEC     equ 0x0C * 4                ; IRQ4's vector, which is where a serial
                                    ; mouse on COM1 lives

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- take it, the way the game does: straight into the IVT --------------
    xor ax, ax
    mov es, ax
    cli
    mov ax, [es:VEC]                ; bank what was there so this is reversible
    mov [old_off], ax
    mov ax, [es:VEC+2]
    mov [old_seg], ax
    mov word [es:VEC], grab_isr
    mov [es:VEC+2], cs
    sti

    mov ah, 0x09
    mov dx, msg_took
    int 0x21

    ; --- ...and BLOCK, which is the harness's window ------------------------
    ; AH=08h goes through the box's own key poll, which samples the mouse -
    ; so a re-arm that lives on that path gets its chance here.
    mov ah, 0x08
    int 0x21

    ; --- give it back -------------------------------------------------------
    xor ax, ax
    mov es, ax
    cli
    mov ax, [old_off]
    mov [es:VEC], ax
    mov ax, [old_seg]
    mov [es:VEC+2], ax
    sti

    mov ah, 0x09
    mov dx, msg_gave
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; -----------------------------------------------------------------------------
; grab_isr - the game's shape: read the byte, keep nobody informed, EOI
; -----------------------------------------------------------------------------
grab_isr:
    push ax
    push dx
    mov dx, 0x03FD                  ; LSR, as the game reads it
    in al, dx
    mov dx, 0x03F8                  ; ...and the byte, which is what makes this
    in al, dx                       ; a THEFT rather than a chain
    inc word [cs:count]
    mov al, 0x20
    out 0x20, al
    pop dx
    pop ax
    iret

count:   dw 0
old_off: dw 0
old_seg: dw 0

msg_hi:   db 13,10,'IRQ4 grab probe - IRQGRAB.COM',13,10,'$'
msg_took: db 'TOOK int 0Ch - press a key to give it back',13,10,'$'
msg_gave: db 'GAVE it back - press a key to exit',13,10,'$'
