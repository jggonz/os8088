; =============================================================================
; os8088 - apps/arcade/arcqbs.inc - Q*BERT'S SOUND BOARD, LIVE (ARCADE-SPEC 13)
; Included by arcade.asm, after arc6502.inc.
;
; Gottlieb's rev. 1 sound board as MAME's gottlieb_a.cpp has it, and as
; tools/qbsnd.py has it in Python: an M6503 at 894,886 Hz (arc6502.inc), a
; 6532 RIOT (its timer, its two ports, the PA7 edge detector whose interrupt
; is the 6503's IRQ) and an MC1408 DAC written at 1000h. The Votrax at 2000h
; and its clock at 3000h are not sounded.
;
; It runs LIVE, into the Sound Blaster stream (ARCADE-SPEC 8): each sample
; the stream asks for is the DAC's value after the 6503 has run that
; sample's share of its clock. MEASURED first (13.1): the board's 28 sounding
; codes are 35.8 s of sound in all, too much to render once and keep beside
; a .ROM on a 360KB disk; and the core's ESTIMATED cost, ~82 286-cycles a
; 6502 instruction, is a 29 MHz 286's worth for the board alone - so it runs
; on a 386 or better, and a 286 or an 8088 plays Q*bert silent, as before.
;
; The game's command: Q*bert writes FFh then the command to 5802h, which its
; patch moves to 580Ah (ARCADE-SPEC 10.2), never two commands in a frame
; (tests/qbnative.py --sndlog). Once a frame (qbs_tick) a byte there that
; is not FFh is taken, FFh put back, and the RIOT's port A strobed with the
; code the 6503 reads: the byte's low six bits inverted, PA7 up then down.
; =============================================================================

QBS_HZ      equ 10989               ; the stream's rate: the DSP's time
                                    ;   constant 165, as Pac-Man's (PM_SBHZ)
QBS_CPS     equ 81                  ; 6503 cycles a sample: 894,886 / 10,989
QBS_FRAC    equ 28490               ;   = 81.4347, its fraction in 65536ths
QBS_KB      equ 32                  ; its memory: 0000-7FFFh (A15 not decoded)
QBS_BOOT    equ 178977              ; its power-on run: 0.2 s, as qbsnd.py's

; -----------------------------------------------------------------------------
; qbs_prep - BD_SBPREP, on the desktop: the board can be heard here - a 386
; or better, and a QBERT.ROM with the sound chips - and is ready: its memory
; claimed, its ROM in, powered on once (kept across entries). CF=1 not here
; -----------------------------------------------------------------------------
qbs_prep:
    call OSAPI_CPU_INFO             ; AL = the tier: a 386 at least (13.1:
    cmp al, CPU_386                 ;   ESTIMATED ~82 286-cycles a 6502
                                    ;   instruction, a 29 MHz 286's worth)
    jb .no
    cmp word [qbs_on], 1            ; (powered on already: as it was left)
    je .yes
    mov si, [ar_rimgc]              ; the .ROM's header: its length, 68KB
    or si, si
    jz .no
    push es
    mov es, si
    cmp word [es:18], 1             ; (the image past 64KB: the sound chips
    jb .nochips                     ;   at its 10000h)
    pop es
    cmp word [qbs_seg], 0
    jne .have
    mov ax, QBS_KB
    call OSAPI_MEM_CLAIM
    jc .no
    mov [qbs_seg], dx
.have:
    mov ax, [qbs_seg]               ; the memory: zero, then the two chips
    mov [m6_seg], ax                ;   at 7000h
    push ds
    push es
    mov es, ax
    xor di, di
    mov cx, QBS_KB * 512
    xor ax, ax
    cld
    rep stosw
    mov ax, [ar_rimg]
    add ax, 0x1000
    mov ds, ax
    xor si, si
    mov di, 0x7000
    mov cx, 0x800
    rep movsw
    pop es
    pop ds
    call qr_reset
    call m6_reset
    mov word [qbs_left], QBS_BOOT & 0xFFFF   ; its own power-on, quietly
    mov word [qbs_left+2], QBS_BOOT >> 16
.boot:
    call qr_irq
    mov [m6_irq], al
    mov ax, 2000
    call m6_run
    sub word [qbs_left], 2000
    sbb word [qbs_left+2], 0
    jns .boot
    mov word [qbs_on], 1
    mov byte [qbs_dac], 0x80
.yes:
    clc
    ret
.nochips:
    pop es
.no:
    stc
    ret

; qbs_off - the board's memory given back (qb_off's)
qbs_off:
    xor dx, dx
    xchg dx, [qbs_seg]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    mov word [qbs_on], 0
    ret

; -----------------------------------------------------------------------------
; qbs_tick - BD_SBTICK, once a frame: the game's command, if it wrote one,
; strobed into the RIOT
; -----------------------------------------------------------------------------
qbs_tick:
    push es
    mov es, [qb_gseg]
    mov al, 0xFF
    xchg al, [es:0x580A]
    pop es
    cmp al, 0xFF
    je .r
    not al                          ; the code: the low six bits inverted,
    and al, 0x3F                    ;   PA7 the strobe (MAME's
    or al, 0xC0                     ;   gottlieb_sound_r1::write)
    call qr_porta
    and al, 0x7F
    call qr_porta
    call qr_irq
    mov [m6_irq], al
.r:
    ret

; -----------------------------------------------------------------------------
; qbs_mix - BD_MIX: CX samples at ES:DI, each the DAC after a sample's share
; of the 6503's clock (with a fraction carried, so the pitch is exact)
; -----------------------------------------------------------------------------
qbs_mix:
    jcxz .r
.s:
    push cx
    call qr_irq
    mov [m6_irq], al
    mov ax, QBS_CPS
    add word [qbs_frac], QBS_FRAC
    adc ax, 0
    call m6_run
    mov al, [qbs_dac]
    stosb
    pop cx
    loop .s
.r:
    ret

; =============================================================================
; THE RIOT (6532): ports, timer, edge detector - tools/qbsnd.py's Riot
; =============================================================================

; qr_reset - at the board's power-on
qr_reset:
    push ds
    pop es
    mov di, qr_state
    mov cx, QR_SIZE
    xor al, al
    cld
    rep stosb
    mov byte [qr_pa], 0x40          ; (idle: no command, PA7 low)
    mov byte [qr_pb], 0xFF
    mov byte [qr_tsh], 10
    ret

; qr_porta - AL = the main board's port A: an edge on PA7 the detector may
; take. Kept: all
qr_porta:
    push ax
    mov ah, [qr_pa]
    mov [qr_pa], al
    xor ah, al                      ; PA7 moved?
    test ah, 0x80
    jz .r
    and al, 0x80                    ; to the edge the detector watches
    cmp byte [qr_pa7pos], 0
    je .neg
    or al, al
    jz .r
    jmp short .take
.neg:
    or al, al
    jnz .r
.take:
    mov byte [qr_pa7f], 1
.r:
    pop ax
    ret

; qr_timer - AL = the timer's count now, CF=1 it has passed zero (and the
; flag is set). Kept: all but AX and the flags
qr_timer:
    push bx
    push cx
    push dx
    mov ax, [m6_cyc]                ; DX:AX = the cycles since the write
    mov dx, [m6_cyc+2]
    sub ax, [qr_tat]
    sbb dx, [qr_tat+2]
    mov bx, ax                      ; (kept, for past zero)
    push dx
    mov cl, [qr_tsh]                ; n = elapsed >> the divider's shift
    xor ch, ch
    jcxz .n
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
.n:
    or dx, dx
    jnz .past
    mov cl, [qr_tval]
    xor ch, ch
    cmp ax, cx
    ja .past
    sub cl, al                      ; count = tval - n
    mov al, cl
    pop dx
    clc
    jmp short .r
.past:                              ; past zero: one a cycle from FFh,
    pop dx                          ;   past = elapsed - (tval + 1) << shift
    mov al, [qr_tval]
    xor ah, ah
    inc ax
    mov cl, [qr_tsh]
    shl ax, cl                      ; (1024 x 256 fits no word: the low
    sub bx, ax                      ;   byte is all the count needs)
    mov al, 0xFF
    sub al, bl
    mov byte [qr_tflag], 1
    stc
.r:
    pop dx
    pop cx
    pop bx
    ret

; qr_irq - AL = the RIOT's IRQ line: 1 asserted. Kept: all but AX
qr_irq:
    call qr_timer                   ; (a timer past zero sets its flag)
    xor al, al
    cmp byte [qr_tflag], 0
    je .pa7
    cmp byte [qr_tie], 0
    je .pa7
    mov al, 1
.pa7:
    cmp byte [qr_pa7f], 0
    je .r
    cmp byte [qr_pa7ie], 0
    je .r
    mov al, 1
.r:
    ret

; m6_ior - the 6503 reads I/O: BX = the address (A15 masked off), out AL,
; everything else kept. The RIOT where (BX & 7200h) = 0200h; nothing else
; reads (FFh). [m6_cyc] is the cycle the instruction began on (arc6502.inc)
m6_ior:
    push bx
    mov [qr_sah], ah                ; (everything but AL kept, AH too)
    mov ax, bx
    and ax, 0x7200
    cmp ax, 0x0200
    jne .ff
    and bx, 0x1F
    test bl, 0x04
    jnz .tim
    and bl, 3                       ; ports: A, its DDR, B, its DDR
    jz .pa
    cmp bl, 2
    je .pb
    mov al, [qr_ddra]
    cmp bl, 1
    je .r
    mov al, [qr_ddrb]
    jmp short .r
.pa:
    mov al, [qr_ora]                ; (an output bit reads what was written)
    and al, [qr_ddra]
    mov ah, [qr_ddra]
    not ah
    and ah, [qr_pa]
    or al, ah
    jmp short .r
.pb:
    mov al, [qr_orb]
    and al, [qr_ddrb]
    mov ah, [qr_ddrb]
    not ah
    and ah, [qr_pb]
    or al, ah
    jmp short .r
.tim:
    test bl, 1
    jnz .flags
    mov al, bl                      ; the timer: A3 its interrupt's enable,
    and al, 8                       ;   and a read past zero clears its flag
    mov [qr_tie], al
    call qr_timer
    jnc .r
    mov byte [qr_tflag], 0
    jmp short .r
.flags:
    call qr_timer                   ; the flags: timer bit 7, PA7 bit 6 -
    xor al, al                      ;   and the PA7 flag cleared by the read
    cmp byte [qr_tflag], 0
    je .f2
    mov al, 0x80
.f2:
    cmp byte [qr_pa7f], 0
    je .f3
    or al, 0x40
.f3:
    mov byte [qr_pa7f], 0
    jmp short .r
.ff:
    mov al, 0xFF
    jmp short .r2
.r:
    push ax                         ; a RIOT read may have dropped its line
    call qr_irq                     ;   (the flags read clears PA7's)
    mov [m6_irq], al
    pop ax
.r2:
    mov ah, [qr_sah]
    pop bx
    ret

; m6_iow - the 6503 writes I/O: BX = the address, AL = the byte. The RIOT,
; the DAC (1000h, mirrored to 1FFFh); the Votrax and its clock are not
; sounded. Kept: all
m6_iow:
    push ax
    push bx
    push cx
    push dx
    mov dx, bx
    and dx, 0x7000
    cmp dx, 0x1000
    jne .notdac
    mov [qbs_dac], al
    jmp short .r
.notdac:
    mov dx, bx
    and dx, 0x7200
    cmp dx, 0x0200
    jne .r
    and bx, 0x1F
    test bl, 0x04
    jnz .tim
    and bl, 3
    jnz .w1
    mov [qr_ora], al
    jmp short .riot
.w1:
    cmp bl, 1
    jne .w2
    mov [qr_ddra], al
    jmp short .riot
.w2:
    cmp bl, 2
    jne .w3
    mov [qr_orb], al
    jmp short .riot
.w3:
    mov [qr_ddrb], al
    jmp short .riot
.tim:
    test bl, 0x10
    jz .edge
    mov [qr_tval], al               ; the timer: its count, its divider (1,
    mov al, bl                      ;   8, 64, 1024 - shifts 0, 3, 6, 10),
    and al, 3                       ;   A3 its interrupt's enable
    push bx
    mov bl, al
    xor bh, bh
    mov al, [qr_shifts+bx]
    pop bx
    mov [qr_tsh], al
    mov al, bl
    and al, 8
    mov [qr_tie], al
    mov byte [qr_tflag], 0
    mov ax, [m6_cyc]
    mov [qr_tat], ax
    mov ax, [m6_cyc+2]
    mov [qr_tat+2], ax
    jmp short .riot
.edge:
    mov al, bl                      ; the edge detector: A0 positive, A1 its
    and al, 1                       ;   interrupt's enable
    mov [qr_pa7pos], al
    mov al, bl
    and al, 2
    mov [qr_pa7ie], al
.riot:
    call qr_irq                     ; the line as the RIOT now drives it
    mov [m6_irq], al
.r:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

qr_shifts:  db 0, 3, 6, 10
