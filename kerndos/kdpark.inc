; =============================================================================
; kerndos/kdpark.inc - THE WAY BACK TO A PARKED kern_mega (-DKD_MEGA only;
; docs/plans/KERN-MEGA-DOS-PLAN.md 2.3 step 8, kernel/megados.inc)
;
; kern_mega copied all of conventional memory into a block above 1MB before it
; handed the machine over, and the launch block's tail says where
; (KDL_PBASE). So the way home is not HIBERNAT.IMG and needs no disk: stage
; what hbm_wake reads (the clock, the tick, the exit code, the arena) where it
; reads it, open A20, and enter protected mode on the GDT the kernel left in
; the park's header, straight into the PARK STUB where it lies above 1MB
; (kernel/megapark.inc). The stub and its GDT are the PARKED KERNEL's own
; build, so a kern_dos and a kernel from different commits cannot disagree
; about their layout - this side knows two numbers, KDP_GDT and the selector.
;
; in:  from kd_leave, after the mouse, the keyboard guard and the int 15h
;      filter are unhooked; SI = the launch block, which has a park
; out: never - the stub restores the session, or says why it cannot and
;      restarts the machine
; =============================================================================
KDP_GDT     equ 0x380               ; MPK_GDT: the stub's GDT in the park's header
                                    ; (kernel/megados.inc; one build, one number)

kd_unpark:
    mov ax, [si+KDL_STAGE]          ; where the kernel will look (96.49.6)
    or ax, ax
    jnz .stg
    call kd_stageseg
    mov ax, es
.stg:
    mov es, ax
    mov [kdp_stage], ax
    ; --- a TEXT page, so the stub has somewhere to say a refusal ------------
    mov ah, 0x0F
    int 0x10                        ; AL = the mode the program left
    cmp al, 2
    je .text
    cmp al, 3
    je .text
    cmp al, 7
    je .text
    mov ax, 0x0003
    cmp word [kdp_stage], 0xB000
    jne .mode
    mov al, 0x07
.mode:
    int 0x10
.text:
    mov es, [kdp_stage]
%ifdef DOSR_MARK
    mov byte [es:HM_ROW + 40 * 2], 'U'  ; DOSRMARK=1: kern_dos reached the way
    mov byte [es:HM_ROW + 40 * 2 + 1], 0x07 ; back; the stub marks 41-44
%endif
    mov si, [kd_lbp]
    ; --- what hbm_wake reads off the stage (kd_resume's own four) ------------
    push si
    add si, KDL_CLK
    mov di, HS_CLK
    mov cx, 10
    cld
    rep movsb
    pop si
    mov ax, [si+KDL_TICK]
    mov [es:HS_TICK0], ax
    mov ax, [si+KDL_TICK + 2]
    mov [es:HS_TICK0 + 2], ax
    mov ax, [kd_code]
    mov [es:HS_DOSCODE], ax
    mov ax, [dos_akb]
    mov [es:HS_DOSAKB], ax
    ; --- A20, opened and PROVED (kernel/mega.inc's mega_a20 sequence) -------
    ; The stub runs above 1MB, so the line has to be open before the jump; a
    ; DOS program may have shut it and a BIOS's AH=87h may shut it after.
    call kdp_a20t
    jnc .a20ok
    in al, 0x92
    test al, 2
    jnz .kbc
    or al, 2
    and al, 0xFE
    out 0x92, al
    call kdp_a20w
    jnc .a20ok
.kbc:
    cli
    call kdp_kbw
    mov al, 0xD1
    out 0x64, al
    call kdp_kbw
    mov al, 0xDF
    out 0x60, al
    call kdp_kbw
    sti
    call kdp_a20w
    jnc .a20ok
    mov si, kdp_s_a20
    jmp short .fail
.a20ok:
    ; --- INTO THE STUB, WHERE IT IS: protected mode on the park's own GDT ----
    mov si, [kd_lbp]
    mov ax, [si+KDL_PBASE]          ; the GDT is at the park + KDP_GDT
    mov dx, [si+KDL_PBASE + 2]
    add ax, KDP_GDT
    adc dx, 0
    mov [kdp_gdtr + 2], ax
    mov [kdp_gdtr + 4], dl
    mov bx, [si+KDL_PSUM]           ; the stub's check: what the kernel said
    mov dx, [si+KDL_PSUM + 2]       ; the park is, from below 1MB, against
    mov cx, [si+KDL_PKB]            ; the header's own cells (megapark.inc)
    mov di, [si+KDL_PBASE]
    mov si, [si+KDL_PBASE + 2]
    cli
    mov al, 0xFF                    ; every line masked: from here the IVT is
    out 0x21, al                    ; nobody's
    out 0xA1, al
    mov al, 0x8D                    ; NMI off
    out 0x70, al
cpu 286
    lgdt [kdp_gdtr]
    smsw ax
    or al, 1
    lmsw ax
cpu 8086
    db 0xEA                         ; jmp far 08h:0 - the stub, above 1MB
    dw 0, 0x08
.fail:
    call kd_puts
    xor ah, ah
    int 0x16
    mov si, [kd_lbp]
    mov dl, [si+KDL_UNIT]
    int 0x19
.h:
    hlt
    jmp .h

kdp_a20w:                           ; up to 512 probes for the gate to take
    mov cx, 512
.p:
    call kdp_a20t
    jnc .r
    loop .p
    stc
.r:
    ret
kdp_a20t:                           ; CF = 1 the gate is SHUT
    push ds
    push es
    pushf
    cli
    xor ax, ax
    mov es, ax
    dec ax
    mov ds, ax
    mov bx, [es:0xFFE0]             ; 0000:FFE0 and FFFF:FFF0 - not 0500/0510,
    not word [0xFFF0]               ; which on the HMA arm is kern_dos's own
    mov ax, [es:0xFFE0]             ; code; FFFF:FFF0 is past any image
    not word [0xFFF0]
    popf
    pop es
    pop ds
    cmp ax, bx
    je .o
    stc
    ret
.o:
    clc
    ret
%ifndef KD_HMA
; -----------------------------------------------------------------------------
; kd_a20shut - A20 SHUT for the program: the LOW kern_dos's machine (SPEC.md
; 96.40.9.2). kern_mega leaves the gate open across the drop, and a program
; that wants the 1MB wrap - EXEPACK's unpacker, CP/M's CALL 5 through a low
; PSP - wants it shut, which is how kern_big's kern_dos finds it on an AT
; whose BIOS left it so. Port 92h's gate first (only where the port reads as
; one: 0FFh is no port at all), then the 8042's output port with A20 low and
; the reset line high; then up to 512 probes for the gate to follow. A gate
; that will not shut is left open: the program runs as it would have in the
; HMA arm's arena, which is the one it was refused. Every register kept
; -----------------------------------------------------------------------------
kd_a20shut:
    push ax
    push bx
    push cx
    call kdp_a20t
    jc .out                         ; shut already
    in al, 0x92
    cmp al, 0xFF
    je .kbc                         ; no fast gate on this machine
    test al, 2
    jz .kbc
    and al, 0xFC                    ; A20 off, and never the reset bit
    out 0x92, al
    call .wait
    jc .out
.kbc:
    pushf
    cli
    call kdp_kbw
    mov al, 0xD1
    out 0x64, al
    call kdp_kbw
    mov al, 0xDD
    out 0x60, al
    call kdp_kbw
    popf
    call .wait
.out:
    pop cx
    pop bx
    pop ax
    ret
.wait:                              ; CF = 1 once the gate reads SHUT
    mov cx, 512
.p:
    push cx
    call kdp_a20t
    pop cx
    jc .r
    loop .p
    clc
.r:
    ret
%endif

kdp_kbw:
    xor cx, cx
.w:
    in al, 0x64
    test al, 2
    loopnz .w
    ret

kdp_s_a20:    db 'A20 will not open, so the parked session cannot come back. '
              db 'Press a key to restart.', 13, 10, 0
kdp_gdtr:     dw 6 * 8 - 1, 0, 0    ; the park's GDT, as lgdt wants it

section .bss
kdp_stage:  resw 1
section .text
