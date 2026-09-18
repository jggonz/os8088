; =============================================================================
; os8088 - tests/dossnd/snd.asm
;
; The wave-3 sound gate's DOS program (SPEC.md 96.17). It prints the whole
; environment out of its own PSP:002C, which is the only place BLASTER= can be
; seen from - and BLASTER= is the whole visible half of "the drivers got out
; of the way". The invisible half is asserted by the HARNESS, which reads the
; sound row's DRVR_SEG straight out of the guest.
;
; It waits for a key, so the harness has a window in which the driver is
; unloaded and the program is still running.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    mov ah, 0x09
    mov dx, msg_env
    int 0x21
    mov ax, [0x2C]                  ; the environment segment, out of the PSP
    or ax, ax
    jz .noenv
    mov es, ax
    xor di, di
.var:
    cmp byte [es:di], 0             ; a NUL where a name should start ends the
    je .envdone                     ; whole set
.ch:
    mov al, [es:di]
    inc di
    or al, al
    jz .eol
    mov dl, al
    mov ah, 0x02
    int 0x21
    jmp short .ch
.eol:
    call put_crlf
    jmp short .var
.noenv:
    mov ah, 0x09
    mov dx, msg_noenv
    int 0x21
.envdone:
    mov ah, 0x09
    mov dx, msg_end
    int 0x21

    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C21
    int 0x21

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

msg_hi:    db 13,10,'os8088 DOS sound gate - DOSSND.COM',13,10,13,10,'$'
msg_env:   db 'ENV{',13,10,'$'
msg_noenv: db '(no environment segment)',13,10,'$'
msg_end:   db '}ENV',13,10,'$'
msg_key:   db 13,10,'READY - press a key to exit with code 33',13,10,'$'
