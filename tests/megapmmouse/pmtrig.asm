; =============================================================================
; os8088 - tests/megapmmouse/pmtrig.asm
;
; A TEST instrument (nothing under tests/ ships): PMTRIG, the "user moving the
; PicoMEM's USB mouse" for tests/megapmmouse.py (KERN-MEGA-PLAN 14.117). Run
; by MEGARUN off C:, it
;   1. with -DPMT_UP: ticks the pointer driver in as the Control Panel does
;      (OSAPI_DRV_USE, DRVC_POINT) - a PROTECTED-MODE attach of USBMOUSE.DRV;
;      without it the driver must already be up (SYSTEM.CFG, a boot attach in
;      real mode before the switch)
;   2. puts PMT_N reports of (+PMT_DX, +PMT_DY), no button, one at a time in
;      0040:00F0 and raises the card's line, IRQ 7, as a software interrupt -
;      vector 27h in protected mode. Its IDT gate is kern_mega's reflection of
;      an option ROM's IRQ, so the "card" (pmrom.asm) runs in REAL mode and
;      does `int 33h AX=0060h`, exactly the road a real report takes
;   3. says where the pointer was and is, and how many reports the ROM took
;
; E9:  PMT: up CF AAAA          (with PMT_UP)
;      PMT: at XXXX YYYY        before
;      PMT: now XXXX YYYY took NN
; and then refuses to start, so it leaves nothing behind.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PMTRIG', pt_entry

PMT_N   equ 4
PMT_DX  equ 20                  ; halved by the driver: +10, +5 a report
PMT_DY  equ 10

pt_entry:
    push si
    push di
%ifdef PMT_UP
    mov ax, (DRVU_UP << 8) | DRVC_POINT
    call OSAPI_DRV_USE          ; (said AFTER: the load writes E9 lines of
    push ax                     ; its own, which would split ours)
    mov al, '0'
    adc al, 0
    mov si, pt_s_up
    call pt_say
    out 0xE9, al
    mov al, ' '
    out 0xE9, al
    pop ax
    call pt_hex4
    call pt_nl
%endif
    mov si, pt_s_at
    call pt_say
    call pt_pos
    call pt_nl

    mov dx, 0x0040              ; the BIOS's inter-application area: a window
    mov cx, 0x0100              ; over it under protected mode, the segment
    xor ax, ax                  ; itself on a real-mode kernel
    mov bl, SEG_WIN
    call OSAPI_SEG
    jc .out
    mov es, dx
    mov byte [es:0xF3], 0
    call OSAPI_CPU_INFO
    mov bl, ah
    mov cx, PMT_N
.rep:
    mov byte [es:0xF0], 0
    mov byte [es:0xF1], PMT_DX
    mov byte [es:0xF2], PMT_DY
    test bl, CPU_F_PM
    jz .rm
    int 0x27                    ; IRQ 7 at MEGA_PIC_BASE: the reflection
    jmp short .nx
.rm:
    int 0x0F                    ; ...a real-mode kernel's IRQ 7
.nx:
    loop .rep

    mov si, pt_s_now
    call pt_say
    call pt_pos
    mov si, pt_s_took
    call pt_say0
    mov al, [es:0xF3]
    call pt_hex2
    call pt_nl
    push ds
    pop es
.out:
    pop di
    pop si
    stc                         ; nothing to keep running
    ret

pt_pos:                         ; "XXXX YYYY", the pointer
    push cx
    push dx
    call OSAPI_MOUSE
    mov ax, cx
    call pt_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, dx
    call pt_hex4
    pop dx
    pop cx
    ret

pt_say:                         ; "PMT: " then SI
    push si
    mov si, pt_s_pfx
    call pt_say0
    pop si
pt_say0:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
pt_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
pt_hex4:
    push ax
    mov al, ah
    call pt_hex2
    pop ax
pt_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

pt_s_pfx:   db 'PMT: ', 0
pt_s_up:    db 'up CF ', 0
pt_s_at:    db 'at ', 0
pt_s_now:   db 'now ', 0
pt_s_took:  db ' took ', 0

    OS88_IMAGE_END
