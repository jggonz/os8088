; =============================================================================
; os8088 - tests/dosirq/irq.asm
;
; CAN A DOS PROGRAM DRIVE THE HARDWARE IN THE BOX? (SPEC.md 96.18)
;
; Every other DOS gate here asks a question about INT 21h, which is OUR code
; answering. This one asks about the machine: a program inside the fsx bracket
; hooks a real IRQ vector, unmasks a real line at a real 8259, and asks a real
; card to pull it. If the count comes back zero the DOS box can host only
; programs that poll, which rules out most of what a Sound Blaster is for.
;
; THE SOUND BLASTER'S DSP COMMAND 0F2h IS THE CHEAPEST HARDWARE INTERRUPT ON
; THE MACHINE TO ASK FOR: it raises the card's IRQ with no DMA, no buffer and
; no sound - one write, and either the line fires or it does not. Every other
; way of getting an interrupt out of this card needs a DMA transfer set up
; first, which is three more things that can be wrong.
;
; It reports FOUR numbers rather than a verdict, because they fail separately:
;
;   DSP     - the version the card answered, which says the reset handshake
;             worked and we are talking to a card at all. Without this line a
;             zero IRQ count means nothing, since a card that never answered
;             was never asked.
;   MASK    - the 8259's IMR as the BRACKET handed it over. It is expected to
;             have IRQ7 masked: os8088 has no use for the line once SOUND.DRV
;             is out of the way, so the program has to unmask it itself - and
;             a bracket that handed over a mask a program cannot change would
;             fail here rather than at the count.
;   IRQ     - how many times the handler ran. This is the assertion.
;   TICKS   - how many BIOS ticks the wait actually spent, which separates
;             "the line never fired" from "the machine never ran".
;
; PHASE 2 IS DMA, and it is the half that could not be inferred from phase 1.
; os8088's floppy owns channel 2 of the same 8237 and takes it inside
; `dsk_xfer`, so "the program can write DMA registers" and "a transfer the
; program set up actually completes" are different questions - and every
; interesting thing a DOS program does with a sound card is on the second one.
; It plays 256 bytes of 80h - DC centre, so silence - through the DSP's
; single-cycle 8-bit output at ~11 kHz, which is 23ms of transfer, and counts
; the completion interrupt. Two more numbers:
;
;   PHYS    - the 20-bit physical address the buffer landed at, which is the
;             one piece of arithmetic a program does differently inside our
;             heap to on a bare machine: its segment is wherever the arena
;             put it rather than a low one, so a page register computed by
;             habit rather than from the segment would be wrong HERE and
;             right everywhere the program was tested.
;   DMA     - the completion interrupts. One transfer, so one.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

SB_BASE  equ 0x220
SB_RESET equ SB_BASE + 0x6
SB_RDAT  equ SB_BASE + 0xA
SB_WR    equ SB_BASE + 0xC          ; write command/data; bit 7 set = busy
SB_RSTAT equ SB_BASE + 0xE          ; bit 7 set = data available

IRQ_LINE equ 7                      ; ...the card's, and the machine config's
IRQ_VEC  equ 0x0F                   ; IRQ7 is INT 0Fh on the master 8259
IRQ_BIT  equ 0x80                   ; ...bit 7 of the IMR at port 21h

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; ---- reset the DSP ------------------------------------------------------
    ; Write 1 to the reset port, hold it for at least 3us, write 0, then the
    ; card answers 0AAh in the read buffer. The hold is spelled as a read of
    ; an unused port because that is the period way to spend a microsecond and
    ; needs no clock: an 8-bit ISA read is ~1us on any machine this runs on.
    mov dx, SB_RESET
    mov al, 1
    out dx, al
    mov cx, 8
.hold:
    in al, 0x80
    loop .hold
    xor al, al
    out dx, al

    mov cx, 0                       ; 65,536 polls is far longer than the
    mov dx, SB_RSTAT                ; ~100us the card needs, and terminates
.rwait:
    in al, dx
    test al, 0x80
    jnz .rready
    loop .rwait
    mov dx, msg_nodsp
    mov ah, 0x09
    int 0x21
    jmp short .nodsp
.rready:
    mov dx, SB_RDAT
    in al, dx
    cmp al, 0xAA
    je .dspok
    mov dx, msg_nodsp
    mov ah, 0x09
    int 0x21
    jmp short .nodsp
.dspok:

    ; ---- ask its version, which proves the command path both ways -----------
    mov al, 0xE1
    call dsp_write
    call dsp_read
    mov [ver_hi], al
    call dsp_read
    mov [ver_lo], al

    mov ah, 0x09
    mov dx, msg_dsp
    int 0x21
    mov al, [ver_hi]
    call put_dec
    mov dl, '.'
    mov ah, 0x02
    int 0x21
    mov al, [ver_lo]
    call put_dec
    call put_crlf
.nodsp:

    ; ---- what the bracket handed us -----------------------------------------
    in al, 0x21
    mov [imr0], al
    mov ah, 0x09
    mov dx, msg_mask
    int 0x21
    mov al, [imr0]
    call put_hex2
    call put_crlf

    ; ---- hook IRQ7 ----------------------------------------------------------
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [IRQ_VEC*4]             ; bank the old vector
    mov bx, [IRQ_VEC*4+2]
    pop ds
    mov [old_off], ax
    mov [old_seg], bx

    cli
    push ds
    xor ax, ax
    mov ds, ax
    mov word [IRQ_VEC*4], irq_handler
    mov [IRQ_VEC*4+2], cs
    pop ds

    in al, 0x21                     ; ...and unmask the line
    and al, ~IRQ_BIT & 0xFF
    out 0x21, al
    sti

    ; ---- pull the line ------------------------------------------------------
    mov al, 0xF2                    ; "raise your IRQ now" - no DMA, no sound
    call dsp_write

    ; ---- wait one second of BIOS ticks --------------------------------------
    ; The tick count is read out of the BDA rather than counted in a loop, so
    ; the wait is a real second on any machine and the number is reportable:
    ; a zero IRQ count beside a zero tick count is a machine that never ran,
    ; which is a different failure to a line that never fired.
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x46C]
    pop ds
    mov [tick0], ax
.wait:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x46C]
    pop ds
    mov bx, [tick0]
    sub ax, bx
    mov [ticks], ax
    cmp ax, 18                      ; ...one second at 18.2Hz
    jb .wait

    ; ---- put it all back ----------------------------------------------------
    cli
    mov al, [imr0]
    out 0x21, al
    push ds
    xor ax, ax
    mov ds, ax
    mov bx, [cs:old_off]
    mov [IRQ_VEC*4], bx
    mov bx, [cs:old_seg]
    mov [IRQ_VEC*4+2], bx
    pop ds
    sti

    ; ---- report -------------------------------------------------------------
    mov ah, 0x09
    mov dx, msg_irq
    int 0x21
    mov ax, [hits]
    call put_dec16
    call put_crlf

    mov ah, 0x09
    mov dx, msg_ticks
    int 0x21
    mov ax, [ticks]
    call put_dec16
    call put_crlf

    ; ---- PHASE 2: a real DMA transfer ---------------------------------------
    ; Only worth attempting if the card answered at all; a DMA set up against
    ; a DSP that is not there would report a zero meaning nothing.
    cmp byte [ver_hi], 0
    je .nodma

    ; Where did the buffer land? Physical = DS:0 * 16 + offset, and the 8237
    ; wants it as a 16-bit address plus a 4-bit page. A transfer may not cross
    ; a 64KB physical boundary - the page register does not carry - so this
    ; reports the address rather than assuming it, and refuses rather than
    ; programming a transfer that would wrap to the start of its own page.
    mov ax, ds
    mov dx, ax
    mov cl, 12
    shr dx, cl                      ; DX = the segment's top 4 bits -> the page
    mov cl, 4
    shl ax, cl                      ; AX = (segment << 4), low 16 bits
    add ax, dma_buf                 ; ...and the carry off THAT is the one the
    adc dx, 0                       ; page register has to see
    mov [phys_lo], ax
    mov [phys_pg], dl

    mov ah, 0x09
    push dx
    mov dx, msg_phys
    int 0x21
    pop dx
    mov al, [phys_pg]
    call put_hex2
    mov al, byte [phys_lo+1]
    call put_hex2
    mov al, byte [phys_lo]
    call put_hex2
    call put_crlf

    mov ax, [phys_lo]
    add ax, DMA_LEN
    jc .wrap                        ; ...the 16-bit address carried: the
                                    ; transfer would wrap inside its own page
    ; fill the buffer with DC centre, which is silence at any volume
    mov cx, DMA_LEN
    mov di, dma_buf
    mov al, 0x80
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es

    mov word [hits], 0              ; phase 2 counts on its own
    mov word [ticks], 0

    cli                             ; hook and unmask again for the transfer
    push ds
    xor ax, ax
    mov ds, ax
    mov word [IRQ_VEC*4], irq_handler
    mov [IRQ_VEC*4+2], cs
    pop ds
    in al, 0x21
    and al, ~IRQ_BIT & 0xFF
    out 0x21, al
    sti

    ; ---- programme the 8237, channel 1 --------------------------------------
    mov al, 0x05                    ; mask channel 1 while it is set up
    out 0x0A, al
    xor al, al
    out 0x0C, al                    ; clear the byte-pointer flip-flop
    mov al, 0x49                    ; single cycle, read from memory, channel 1
    out 0x0B, al
    mov ax, [phys_lo]
    out 0x02, al                    ; address low, then high - the flip-flop
    mov al, ah                      ; is what sequences these two writes
    out 0x02, al
    mov al, [phys_pg]
    out 0x83, al                    ; channel 1's page register
    mov ax, DMA_LEN - 1             ; the 8237 counts one MORE than it is told
    out 0x03, al
    mov al, ah
    out 0x03, al
    mov al, 0x01                    ; ...and let it go
    out 0x0A, al

    ; ---- ...and the DSP -----------------------------------------------------
    mov al, 0xD1                    ; speaker on: the transfer is silence, and
    call dsp_write                  ; a card that never leaves standby is a
                                    ; different thing being measured
    mov al, 0x40                    ; time constant = 256 - 1000000/rate
    call dsp_write
    mov al, 256 - 90                ; ...which is ~11 kHz
    call dsp_write
    mov al, 0x14                    ; 8-bit single-cycle DMA output
    call dsp_write
    mov ax, DMA_LEN - 1
    call dsp_write_al
    mov al, ah
    call dsp_write

    ; 256 bytes at 11 kHz is 23ms, so one tick is already generous; wait the
    ; same second phase 1 waited, for the same reason - a zero beside a zero
    ; tick count is a machine that never ran.
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x46C]
    pop ds
    mov [tick0], ax
.dwait:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x46C]
    pop ds
    mov bx, [tick0]
    sub ax, bx
    mov [ticks], ax
    cmp ax, 18
    jb .dwait

    mov al, 0xD3                    ; speaker off
    call dsp_write

    cli                             ; ...and put it all back, the 8237 too
    mov al, 0x05
    out 0x0A, al
    mov al, [imr0]
    out 0x21, al
    push ds
    xor ax, ax
    mov ds, ax
    mov bx, [cs:old_off]
    mov [IRQ_VEC*4], bx
    mov bx, [cs:old_seg]
    mov [IRQ_VEC*4+2], bx
    pop ds
    sti

    mov ah, 0x09
    mov dx, msg_dma
    int 0x21
    mov ax, [hits]
    call put_dec16
    call put_crlf
    jmp short .done2

.wrap:
    mov ah, 0x09
    mov dx, msg_wrap
    int 0x21
    jmp short .done2
.nodma:
    mov ah, 0x09
    mov dx, msg_nodma
    int 0x21
.done2:

    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C22
    int 0x21

; -----------------------------------------------------------------------------
; The handler. It counts, acknowledges the CARD (reading the status port is
; what clears an 8-bit DSP interrupt) and then the PIC, and returns - it does
; nothing that could itself fail, because the number it produces is the whole
; assertion and a handler that faulted would be indistinguishable from a line
; that never fired.
; -----------------------------------------------------------------------------
irq_handler:
    push ax
    push dx
    push ds
    mov ax, cs
    mov ds, ax
    inc word [hits]
    mov dx, SB_RSTAT
    in al, dx                       ; ack the card
    mov al, 0x20
    out 0x20, al                    ; ...then the PIC
    pop ds
    pop dx
    pop ax
    iret

; -----------------------------------------------------------------------------
dsp_write_al:                       ; ...the same thing, named at the one call
dsp_write:                          ; site where the value is in AL already
                                    ; AL = the byte
    push ax
    push cx
    push dx
    mov ah, al
    mov dx, SB_WR
    mov cx, 0
.busy:
    in al, dx
    test al, 0x80
    jz .go
    loop .busy
.go:
    mov al, ah
    out dx, al
    pop dx
    pop cx
    pop ax
    ret

dsp_read:                           ; -> AL
    push cx
    push dx
    mov dx, SB_RSTAT
    mov cx, 0
.poll:
    in al, dx
    test al, 0x80
    jnz .got
    loop .poll
    xor al, al
    pop dx
    pop cx
    ret
.got:
    mov dx, SB_RDAT
    in al, dx
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
put_dec:                            ; AL, 0-255
    push ax
    xor ah, ah
    call put_dec16
    pop ax
    ret

put_dec16:                          ; AX
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

put_hex2:                           ; AL
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .nib
    mov al, ah
    and al, 0x0F
    call .nib
    pop cx
    pop ax
    ret
.nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 7
.put:
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    ret

put_crlf:
    push ax
    push dx
    mov ah, 0x02
    mov dl, 13
    int 0x21
    mov dl, 10
    int 0x21
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
hits    dw 0
ticks   dw 0
tick0   dw 0
old_off dw 0
old_seg dw 0
imr0    db 0
ver_hi  db 0
ver_lo  db 0
phys_lo dw 0
phys_pg db 0

DMA_LEN equ 256

msg_hi:    db 13,10,'os8088 DOS hardware-IRQ gate - DOSIRQ.COM',13,10,13,10,'$'
msg_dsp:   db 'DSP ','$'
msg_nodsp: db 'DSP none - the card did not answer its reset',13,10,'$'
msg_mask:  db 'MASK ','$'
msg_irq:   db 'IRQ ','$'
msg_ticks: db 'TICKS ','$'
msg_phys:  db 'PHYS ','$'
msg_dma:   db 'DMA ','$'
msg_wrap:  db 'DMA skipped - the buffer straddles a 64KB page',13,10,'$'
msg_nodma: db 'DMA skipped - no card answered phase 1',13,10,'$'
msg_key:   db 13,10,'READY - press a key to exit with code 34',13,10,'$'

; The transfer's buffer, last so the image on disk does not carry 256 bytes
; that are overwritten before they are read.
dma_buf:
