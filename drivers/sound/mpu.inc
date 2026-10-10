; =============================================================================
; os8088 - drivers/sound/mpu.inc
;
; THE MPU-401, in UART mode (SPEC.md 34.13). Prefix mpu_. Included by
; sound.asm, which is why it carries no state of its own outside this file's
; few bytes and no service-table cell: everything a package asks of it goes
; through DSV_PKGCALL (sndpkg.inc's SNDV_MID* verbs).
;
; WHAT IT IS FOR is the daughterboard's road: a Roland SC-55, an MT-32, a
; Waveblaster or a Dreamblaster on the MIDI connector, or an emulator's
; synthesiser behind its MPU (86Box's FluidSynth). The samples live on the
; card, and the machine sends three bytes a note - which an 8088 at 4.77 MHz
; does with nothing to spare and nothing to mix.
;
; UART MODE ONLY. The intelligent mode's sequencer is the MPU's own business
; and nothing here needs it; the clones that matter (the SB16's, the
; daughterboard headers, every emulator's) do UART and many do nothing else.
;
; THE PROBE IS CHEAP WHERE THERE IS NO CARD, which is nearly every machine
; this driver loads on. An empty port reads FFh - DSR and DRR both "not
; ready" - so the very first status read refuses, with no write and no
; timeout spent. Only a status byte that is NOT FFh earns the reset.
; =============================================================================

MPU_TRIES   equ 4096                ; status reads before a wait gives up
MPU_ACK     equ 0xFE
MPU_DRR     equ 0x40                ; status bit 6: 1 = cannot take a byte
MPU_DSR     equ 0x80                ; status bit 7: 1 = nothing to read

mpu_bases:  dw 0x330, 0x300

; -----------------------------------------------------------------------------
; mpu_probe - is an MPU-401 at 330h or 300h? If so it is put in UART mode and
; [mpu_base] says where. out: CF=0 found; CF=1 none, and no port was written
; at a base whose status read FFh. Every register but the flags preserved
; -----------------------------------------------------------------------------
mpu_probe:
    push ax
    push cx
    push dx
    push si
    xor ax, ax                      ; (a reload asks again from nothing)
    mov [mpu_base], ax
    mov [mpu_own], ax
    mov si, mpu_bases
    mov cx, 2
.base:
    mov dx, [si]
    inc dx                          ; DX = the status / command port
    in al, dx
    cmp al, 0xFF
    je .next                        ; an empty port: nothing written
    call mpu_waitw
    jc .next
    mov al, 0xFF                    ; RESET...
    out dx, al
    call mpu_ack                    ; ...answered by an ACK, or it is not one
    jc .next
    call mpu_waitw
    jc .next
    mov al, 0x3F                    ; UART mode. Some clones ACK it and some
    out dx, al                      ; do not, so the ACK is drained if it
    call mpu_ack                    ; comes and not required
    dec dx
    mov [mpu_base], dx
    clc
    jmp short .out
.next:
    add si, 2
    loop .base
    stc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mpu_waitw - DX = status: wait for DRR clear. CF=1 timed out. AL clobbered
mpu_waitw:
    push cx
    mov cx, MPU_TRIES
.w:
    in al, dx
    test al, MPU_DRR
    jz .ok
    loop .w
    pop cx
    stc
    ret
.ok:
    pop cx
    clc
    ret

; mpu_ack - DX = status: wait for a byte, CF=0 when it is the ACK. AL
; clobbered
mpu_ack:
    push cx
    mov cx, MPU_TRIES
.w:
    in al, dx
    test al, MPU_DSR
    jz .rd
    loop .w
    pop cx
    stc
    ret
.rd:
    dec dx
    in al, dx
    inc dx
    pop cx
    cmp al, MPU_ACK
    je .ok
    stc
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; mpu_put - AL to the MIDI out. CF=1 the interface never became ready (and
; the byte is lost). Every register preserved but the flags.
;
; ANYTHING ARRIVING IS READ AND DROPPED on the way: a keyboard on MIDI IN, or
; a module's active sensing, fills the interface's input, and some clones stop
; taking output while their input is full. Nothing here wants MIDI IN
; -----------------------------------------------------------------------------
mpu_put:
    push ax
    push cx
    push dx
    mov ah, al
    mov dx, [mpu_base]
    inc dx
    mov cx, MPU_TRIES
.w:
    in al, dx
    test al, MPU_DSR
    jnz .ndr
    dec dx                          ; something came in: take it and drop it
    in al, dx
    inc dx
    jmp short .nx
.ndr:
    test al, MPU_DRR
    jz .go
.nx:
    loop .w
    stc
    jmp short .out
.go:
    dec dx
    mov al, ah
    out dx, al
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpu_hush - every channel's notes off and its controllers reset (CC 123 and
; CC 121, sixteen channels): what a release and a detach owe a module that was
; playing. Registers preserved
; -----------------------------------------------------------------------------
mpu_hush:
    push ax
    push cx
    xor cl, cl
.c:
    mov al, 0xB0
    or al, cl
    call mpu_put
    mov al, 123
    call mpu_put
    xor al, al
    call mpu_put
    mov al, 121                     ; (running status: the B0+n again)
    call mpu_put
    xor al, al
    call mpu_put
    inc cl
    cmp cl, 16
    jb .c
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the package verbs (sndpkg.inc), reached from snd_pkg with BL = the verb,
; ES = the caller's segment. out: CF=0 / CF=1 with AX = SNDE_*
; -----------------------------------------------------------------------------
mpu_v_info:                         ; SNDV_MIDINFO: AX = the base
    mov ax, [mpu_base]
    or ax, ax
    jz mpu_nomidi
    clc
    ret
mpu_nomidi:
    mov ax, SNDE_NOMIDI
    stc
    ret

mpu_v_open:                         ; SNDV_MIDOPEN: the interface is ES's
    cmp word [mpu_base], 0
    je mpu_nomidi
    push bx
    mov bx, es
    pushf
    cli
    mov ax, [mpu_own]
    or ax, ax
    jz .take
    cmp ax, bx
    je .take
    popf
    pop bx
    mov ax, SNDE_BUSY
    stc
    ret
.take:
    mov [mpu_own], bx
    popf
    pop bx
    xor ax, ax
    clc
    ret

mpu_v_write:                        ; SNDV_MIDW: ES:SI, CX bytes
    cmp word [mpu_base], 0
    je mpu_nomidi
    mov ax, es                      ; (AX is the answer either way)
    cmp ax, [mpu_own]
    je .ok
    mov ax, SNDE_NOCLAIM
    stc
    ret
.ok:
    push cx
    push si
    push di
    xor di, di                      ; DI = bytes sent
    jcxz .done
.b:
    mov al, [es:si]
    call mpu_put
    jc .time
    inc si
    inc di
    loop .b
.done:
    mov ax, di
    pop di
    pop si
    pop cx
    clc
    ret
.time:
    mov ax, SNDE_MIDTIME
    pop di
    pop si
    pop cx
    stc
    ret

mpu_v_close:                        ; SNDV_MIDCLOSE: silenced and given back
    cmp word [mpu_base], 0
    je mpu_nomidi
    mov ax, es                      ; (AX is the answer either way)
    cmp ax, [mpu_own]
    jne .not
    call mpu_hush
    mov word [mpu_own], 0
.not:
    xor ax, ax
    clc
    ret

; mpu_detach - the driver going: a module that was playing is silenced
mpu_detach:
    cmp word [mpu_own], 0
    je .r
    call mpu_hush
    mov word [mpu_own], 0
.r:
    ret

; state: expanded by sound.asm at the end of the image, with the rest of the
; driver's zeroed cells (they ship as bss there, not as file bytes)
%macro MPU_STATE 0
mpu_base:   dw 0                    ; the interface's data port, 0 = none
mpu_own:    dw 0                    ; the SEGMENT of the package holding it
%endmacro
