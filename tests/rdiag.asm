; =============================================================================
; rdiag - a KERNEL-SIZED payload that reports which sectors landed wrong
;         (SPEC.md 18.93.1)
;
; The canary asks one question at one offset and has now been wrong twice about
; where to ask it. This asks at EVERY offset: the payload is the same number of
; sectors as KERNEL.SYS, so boot.asm partitions it into exactly the same
; transfer runs, and every sector past the first carries its own file-sector
; index as its first word. Sector 0 - which is always loaded correctly, being
; ahead of any head boundary - then walks the rest and draws a map.
;
; A '.' is a sector that arrived; an 'X' is one that did not. The shape of the
; X's is the diagnosis:
;
;   X's in the TAIL of every run      the BIOS transferred short and answered
;                                     CF=0 for the whole request (SPEC.md 18.91)
;   every X holding index+1           the multi-track flip is off by a sector
;   X's holding the other head's      EOT was ignored (SPEC.md 18.92)
;
; The splash entry at 0x0008 is a bare retf ON PURPOSE: this wants the text
; screen the boot sector left, not a graphics mode it would have to undo.
; =============================================================================
cpu 8086
bits 16
org 0

KERNEL_SEG  equ 0x0060

    jmp verify                  ; 0x0000 - the boot sector's handoff target
    times 0x08 - ($ - $$) db 0
    retf                        ; 0x0008 - the splash tick, deliberately inert
    times 0x10 - ($ - $$) db 0

; -----------------------------------------------------------------------------
verify:
    mov ax, cs
    mov ds, ax
    cld
    mov si, msg_head
    call print

    mov si, 1                   ; file sector 0 is this code; start at 1
.next:
    mov ax, si
    mov cl, 5
    shl ax, cl                  ; 32 paragraphs a sector...
    add ax, KERNEL_SEG          ; ...from where the loader put us
    mov es, ax
    mov ax, [es:0]              ; every sector carries its own index
    cmp ax, si
    je .ok
    cmp word [firstbad], 0xFFFF
    jne .mark
    mov [firstbad], si          ; ...and the first liar is worth naming
    mov [firstval], ax
.mark:
    inc word [nbad]
    mov al, 'X'
    jmp short .put
.ok:
    mov al, '.'
.put:
    call putc
    inc si
    cmp si, SECS
    jb .next

    mov si, msg_bad
    call print
    mov ax, [nbad]
    call hexw
    mov si, msg_first
    call print
    mov ax, [firstbad]
    call hexw
    mov si, msg_held
    call print
    mov ax, [firstval]
    call hexw
.halt:
    cli
    hlt
    jmp short .halt

; -----------------------------------------------------------------------------
putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    pop bx
    pop ax
    ret

print:
    push ax
    push si
.n:
    lodsb
    test al, al
    jz .d
    call putc
    jmp short .n
.d:
    pop si
    pop ax
    ret

hexw:                           ; AX as four hex digits
    push ax
    push cx
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl
    push ax
    and al, 0x0F
    add al, 0x90
    daa
    adc al, 0x40
    daa
    call putc
    pop ax
    pop cx
    loop .d
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
msg_head    db 13, 10, 'rdiag: . = sector arrived, X = did not', 13, 10, 13, 10, 0
msg_bad     db 13, 10, 13, 10, 'bad=', 0
msg_first   db '  first=', 0
msg_held    db '  held=', 0
nbad        dw 0
firstbad    dw 0xFFFF
firstval    dw 0

    times 512 - ($ - $$) db 0   ; sector 0 exactly, and no more: everything
                                ; after this is pattern the tool appends
