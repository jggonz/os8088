; =============================================================================
; os8088 - tests/vidbench/vidsnd.asm
;
; VIDSND: wave 0 (c) and (e) of docs/plans/VIDEO-PLAN.md.
;
;   python3 tests/vidsnd.py [--machine os8088_5150_herc_hdd_sb_gla]
;
; (c) ONE INTERRUPT PER VIDEO FRAME. XDC's clock is the Sound Blaster's
;     block interrupt: auto-init DMA over a double buffer, DSP block length
;     = one frame's audio, so the card interrupts once a frame. SOUND.DRV
;     today fixes the block at 2048 bytes (drivers/sound/sb.inc SBL_HALF),
;     so the plan adds a frame stream to it (VIDEO-PLAN 4.4); this row is
;     that stream built by hand, to show the card and the emulator deliver
;     it. Three shapes, each counted for 91 ticks (5 s):
;       PCM8   22,050 Hz, 735-byte block    BADAPPLE's 30 fps
;       PCM8    8,040 Hz, 134-byte block    BBBB's 60 fps
;       ADPCM4 22,050 Hz, 368-byte block    the same 30 fps at half the bytes,
;                                           DSP 7Dh (auto-init 4-bit ADPCM
;                                           with reference) - does the DSP,
;                                           and does MartyPC, do it at all?
;
; (e) THE CEILING. The card runs PCM8 22,050/735 and its interrupt handler
;     BURNS a set share of each frame with interrupts off - the player's
;     decode, stood in for by a counted loop - while the foreground reads
;     whole tracks off the fixed disk through the ROM's int 13h for 5 s. The
;     tracks read at 0, 25, 50 and 75% say how much frame time a disk of THIS
;     kind leaves the reader. MartyPC's is XT-IDE, CPU-copied; the owner's
;     ST11M is DMA, and there the answer is expected to be nearly flat.
;     A last row burns 50% with interrupts ON - the card acknowledged and
;     the EOI sent first, then `sti` - which is what the player's hook does
;     (SPEC.md 98.3): on a DMA controller its completion interrupt then
;     lands mid-burn instead of waiting for it, and the difference between
;     the two 50% rows is exactly that.
;
; It saves VIDSND.TXT, its report, beside itself when it finishes.
;
; (t) THE TONE, KEY T: the same card set-up, but the DMA buffer holds a
;     635 Hz sine (35 samples, 42 periods in the 1,470-byte ring) instead of
;     silence, so a row can be HEARD - and recorded, and counted on the host
;     (tools/vstone.py). Ten 5 s rows, each after 2 s of silence: the tone
;     alone; the ISR holding 16 ms with no disk; whole tracks read with the
;     ISR holding 0, 4, 8 and 16 ms; one sector a call holding 16 ms; one
;     sector a call with the card's line MASKED at the 8259 for each call,
;     holding 16 ms; and the last two again with IRQ0 - the timer, whose hook
;     is where Video Battle and Video Player decode - as the holder. It asks whether a PicoMEM 2's sound breaks when its
;     int 13h copy is held by an interrupt - the card mixes nothing while it
;     waits for the PC to copy a sector, and has 5 ms of output mixed ahead -
;     and whether masking the holder for each call is the cure. The report
;     is VSTONE.TXT (numbered, as the others).
;
; It SUSPENDS the drivers (OSAPI_DRV_SUSPEND, SPEC.md 51.11) and programs the
; DSP, DMA channel 1 and the PIC itself, as the fallback of VIDEO-PLAN 4.4
; would, and resumes them on every path out. The IRQ line is found here with
; DSP F2h (SPEC.md 96.17.1: the driver defers discovery to first use).
;
; ON A PicoMEM IT DOES WHAT SOUND.DRV DOES (SPEC.md 34.10), and with the same
; code: drivers/sound/picomem.inc is assembled here under PM_CORE. It used to
; end a 1.x run in STACK OVERFLOW, twice over (LOADALL-PLAN's PicoMEM round):
;   - F2h discovery hooked 7, 5, 3 and 2 in turn, and one of those is the
;     card's OWN line, whose vector is its BIOS's multiplexer (34.10.1). So the
;     card is switched off whatever the suspended driver left (pm_undo),
;     configured afresh (pm_init: its line learned and kept open, the SB put on
;     a line and a port the firmware accepts), the port found by a DSP reset
;     at pm_sbports, and [pm_irq] taken as the line - no discovery at all;
;   - the "50%, ints on" row `sti`s inside the ISR, and on a 1.x the SB's IRQ
;     is a SOFTWARE int raised from INSIDE the multiplexer, so the next block
;     re-entered the multiplexer under it, and that one again: a recursion one
;     frame deep per block. That row is for a DMA controller's completion
;     interrupt, which a PicoMEM has none of, so it is SKIPPED there (flags 64).
; Channel 1 is checked for RACING after every start (pm_runaway, 34.10.5) and
; kept masked if it does, and the card's DSP is switched off again before the
; drivers resume, so SOUND.DRV's attach finds it as a reset leaves it.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDSND', vs_entry

VS_TICKS    equ 91                  ; 5 seconds a row
VS_BUFKB    equ 4
VS_TRKKB    equ 16                  ; the int 13h target: a 26-sector track
; the burn: `loop $` is 17 cycles a turn; a 30.23 Hz frame (22,222 Hz / 735,
; the DSP's real rate for time constant 211) is 157,880 cycles
VS_B25      equ 157880 / 4 / 17
VS_B50      equ 157880 / 2 / 17
VS_B75      equ 157880 * 3 / 4 / 17
VS_TMS      equ 4773 / 17           ; (t): `loop` turns in a millisecond
VS_TGAP     equ 36                  ; (t): 2 s of silence before each row
VS_TLEN     equ 35                  ; (t): a period of the sine, in samples
VS_TPER     equ 42                  ; (t): periods in the 1,470-byte ring

vs_entry:
    push si
    call bl_blank
    mov si, vs_s_title
    call bl_sline
    mov si, vs_s_hint
    call bl_sline
    mov si, vs_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vs_win], bx
    clc
.out:
    pop si
    ret

vs_paint:
    call bl_paint
    ret

vs_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    cmp bl, 't'
    je .tone
    call bl_key
    jc .out
    mov si, [vs_win]                ; bl_paint's SI is the WINDOW, and a run
    call bl_paint                   ; spends SI: with it the window manager
    jmp short .out                  ; was asked about whatever SI held, and
.run:                               ; the report went down at (269, 5) - a
    mov byte [vs_tmode], 0          ; white block over the menu bar and the
    call vs_run                     ; title, at the end of every tone test
    mov si, [vs_win]                ; (the owner's 5150, 2026-11-08)
    call bl_paint
    jmp short .out
.tone:
    mov byte [vs_tmode], 1
    call vs_run
    mov byte [vs_tmode], 0
    mov si, [vs_win]
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vs_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call vs_run
    mov si, [vs_win]
    call bl_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the card
; =============================================================================

; vs_dspw - AL to the DSP (wait until it will take a byte). CF = timed out
vs_dspw:
    push cx
    push dx
    push ax
    mov dx, [vs_port]
    add dx, 0x0C
    mov cx, 0xFFFF
.w:
    in al, dx
    test al, 0x80
    jz .go
    loop .w
    pop ax
    pop dx
    pop cx
    stc
    ret
.go:
    pop ax
    out dx, al
    pop dx
    pop cx
    clc
    ret

; vs_reset - reset the DSP. CF = it did not answer AAh
vs_reset:
    push ax
    push cx
    push dx
    mov dx, [vs_port]
    add dx, 6
    mov al, 1
    out dx, al
    in al, dx                       ; a few microseconds
    in al, dx
    in al, dx
    in al, dx
    xor al, al
    out dx, al
    mov cx, 0xFFFF
.w:
    mov dx, [vs_port]
    add dx, 0x0E
    in al, dx
    test al, 0x80
    jz .n
    mov dx, [vs_port]
    add dx, 0x0A
    in al, dx
    cmp al, 0xAA
    je .ok
.n:
    loop .w
    stc
    jmp short .out
.ok:
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; the interrupt: count, burn [vs_burn] turns, acknowledge, EOI
vs_isr:
    push ax
    push cx
    push dx
    push ds
    push cs
    pop ds
    inc word [vs_irqs]
    cmp byte [vs_bsti], 0
    jne .on
    mov cx, [vs_burn]
    jcxz .nb
.b:
    loop .b
.nb:
    call vs_ack
    jmp short .out
.on:
    call vs_ack                     ; acknowledged and EOI'd FIRST, so the
    sti                             ; next interrupt of any line is free
    mov cx, [vs_burn]
    jcxz .nb2
.b2:
    loop .b2
.nb2:
    cli
.out:
    pop ds
    pop dx
    pop cx
    pop ax
    iret

; vs_ack - the DSP's 8-bit acknowledge, and the EOI. DS = ours
vs_ack:
    mov dx, [vs_port]
    add dx, 0x0E
    in al, dx
    mov al, 0x20
    out 0x20, al
    ret

; vs_hook - [vs_irq]'s vector to vs_isr, the old one kept; unmask it
vs_hook:
    push ax
    push bx
    push cx
    push es
    xor ax, ax
    mov es, ax
    mov bl, [vs_irq]
    xor bh, bh
    add bx, 8
    shl bx, 1
    shl bx, 1
    pushf
    cli
    mov ax, [es:bx]
    mov [vs_ovec], ax
    mov ax, [es:bx + 2]
    mov [vs_ovec + 2], ax
    mov word [es:bx], vs_isr
    mov [es:bx + 2], cs
    in al, 0x21
    mov [vs_omask], al
    mov ah, 1
    mov cl, [vs_irq]
    shl ah, cl
    not ah
    and al, ah
    out 0x21, al
    popf
    pop es
    pop cx
    pop bx
    pop ax
    ret

vs_unhook:
    push ax
    push bx
    push es
    xor ax, ax
    mov es, ax
    mov bl, [vs_irq]
    xor bh, bh
    add bx, 8
    shl bx, 1
    shl bx, 1
    pushf
    cli
    mov al, [vs_omask]
    out 0x21, al
    mov ax, [vs_ovec]
    mov [es:bx], ax
    mov ax, [vs_ovec + 2]
    mov [es:bx + 2], ax
    popf
    pop es
    pop bx
    pop ax
    ret

; vs_discover - which line does the card interrupt on? Try 7, 5, 3, 2 with
; DSP F2h. CF = none answered
vs_discover:
    push ax
    push si
    mov si, vs_cands
.try:
    mov al, [si]
    or al, al
    jz .none
    mov [vs_irq], al
    mov word [vs_irqs], 0
    mov word [vs_burn], 0
    call vs_hook
    mov al, 0xF2
    call vs_dspw
    mov cx, 0x4000
.w:
    cmp word [vs_irqs], 0
    jne .got
    loop .w
    call vs_unhook
    inc si
    jmp short .try
.got:
    call vs_unhook
    call vs_reset
    clc
    jmp short .out
.none:
    stc
.out:
    pop si
    pop ax
    ret

; vs_dma - DMA channel 1: auto-init, memory to card, over CX bytes at the
; buffer's base
vs_dma:
    push ax
    push bx
    push dx
    mov ax, [vs_buf]
    mov dx, ax
    mov bl, 4
.sh:                                ; the 20-bit address: seg << 4
    shl ax, 1
    dec bl
    jnz .sh
    mov bl, dh                      ; page = seg >> 12
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov dx, ax                      ; DX = the offset in the page
    pushf
    cli
    mov al, 5
    out 0x0A, al                    ; mask channel 1
    xor al, al
    out 0x0C, al                    ; the flip-flop
    mov al, 0x59                    ; single, increment, AUTO-INIT, read, ch 1
    out 0x0B, al
    mov al, dl
    out 0x02, al
    mov al, dh
    out 0x02, al
    mov al, bl
    out 0x83, al
    mov ax, cx
    dec ax
    out 0x03, al
    mov al, ah
    out 0x03, al
    mov al, 1
    or al, [sbl_drqmask]            ; 5: a channel that RACED stays masked
    out 0x0A, al                    ; unmask (SPEC.md 34.10.5)
    popf
    pop dx
    pop bx
    pop ax
    ret

; vs_play - AL = time constant, BX = block length, CX = DMA length,
; DL = the DSP start command (1Ch PCM8, 7Dh ADPCM4). CF = the DSP refused
vs_play:
    push ax
    push bx
    call vs_dma
    push ax
    mov al, 0xD1                    ; speaker on
    call vs_dspw
    mov al, 0x40
    call vs_dspw
    pop ax
    call vs_dspw                    ; the time constant
    mov al, 0x48
    call vs_dspw
    dec bx
    mov al, bl
    call vs_dspw
    mov al, bh
    call vs_dspw
    mov al, dl
    call vs_dspw
    pop bx
    pop ax
    ret

; vs_race - a card WE configured: is channel 1 racing now the DSP plays?
; pm_runaway masks it and sets [sbl_drqmask] if so (SPEC.md 34.10.5).
; It asks only while [pm_rchk] has ticks left, which sb.inc loads at every
; stream start; this bench asks ONCE a stream, so it loads one - without it
; the counter sits at 0 and the question is never asked at all
vs_race:
    pushf
    cli
    mov byte [pm_rchk], 1
    call pm_runaway
    popf
    ret

vs_stop:
    call vs_reset
    push ax
    mov al, 5
    out 0x0A, al                    ; mask channel 1
    pop ax
    ret

; vs_count - play, count interrupts for VS_TICKS, stop.
; in: as vs_play. out: [vs_irqs]
vs_count:
    push ax
    mov word [vs_irqs], 0
    call vs_hook
    call vs_play
    call vs_race
    call OSAPI_GET_TICKS
    mov [vs_t0], ax
.w:
    call OSAPI_GET_TICKS
    sub ax, [vs_t0]
    cmp ax, VS_TICKS
    jb .w
    call vs_stop
    call vs_unhook
    pop ax
    ret

; vs_ceil - PCM8 30 fps with [vs_burn] turns burnt per interrupt, and the
; foreground reading tracks for VS_TICKS. out: AX = tracks read
vs_ceil:
    push bx
    push cx
    push dx
    push es
    mov word [vs_irqs], 0
    mov word [vs_trk], 0
    mov byte [vs_head], 0
    mov byte [vs_cyl], 1
    call vs_hook
    mov al, 211
    mov bx, 735
    mov cx, 1470
    mov dl, 0x1C
    call vs_play
    call vs_race
    call OSAPI_GET_TICKS
    mov [vs_t0], ax
.rd:
    mov es, [vs_tbuf]
    xor bx, bx
    mov al, [vs_spt]
    mov ah, 2
    mov ch, [vs_cyl]
    mov cl, 1
    mov dh, [vs_head]
    mov dl, 0x80
    int 0x13
    jc .next
    inc word [vs_trk]
.next:
    mov al, [vs_head]
    inc al
    cmp al, [vs_heads]
    jb .h
    xor al, al
    inc byte [vs_cyl]
.h:
    mov [vs_head], al
    call OSAPI_GET_TICKS
    sub ax, [vs_t0]
    cmp ax, VS_TICKS
    jb .rd
    call vs_stop
    call vs_unhook
    mov ax, [vs_trk]
    pop es
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; (t) the tone rows
; =============================================================================

; vs_tones - the ten rows, each after VS_TGAP ticks of silence. Needs
; [vs_port], [vs_irq] and the tone in the buffer. AH=8 is asked here: a
; machine with no fixed disk plays the two rows that need none
vs_tones:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [vs_spt], 0
    mov ah, 8
    mov dl, 0x80
    push es
    int 0x13
    pop es
    jc .geo
    mov al, cl
    and al, 0x3F
    mov [vs_spt], al
    inc dh
    mov [vs_heads], dh
.geo:
    mov si, vs_s_thdr
    call bl_sline
    mov si, vs_trows
.row:
    lodsw                           ; the hold, in ms
    cmp ax, 0xFFFF
    je .done
    mov bx, ax
    lodsb                           ; the disk: 0 none, 1 tracks, 2 sectors,
    mov dl, al                      ; 3 sectors with the line masked
    lodsb                           ; the holder: 0 the card's ISR, 1 IRQ0
    mov [vs_thold], al
    lodsw                           ; the label
    push si
    mov si, ax
    or dl, dl
    jz .go
    cmp byte [vs_spt], 0            ; a disk row and no disk: say so
    jne .go
    mov di, vs_s_tnohd
    call bl_kvs
    pop si
    jmp short .row
.go:
    mov [vs_tdisk], dl              ; (before the mul takes DX)
    mov ax, VS_TMS                  ; ms -> `loop` turns
    mul bx
    mov word [vs_burn], 0
    mov word [vs_tburn], 0
    cmp byte [vs_thold], 0
    jne .t0
    mov [vs_burn], ax
    jmp short .bs
.t0:
    mov [vs_tburn], ax
.bs:
    call vs_tsil
    call vs_trow                    ; AX = IRQs, CX = reads
    mov dx, cx
    push ax
    mov ax, dx
    xor dx, dx
    mov cx, 6
    call bl_kv                      ; "label   <reads>"
    pop ax
    push si
    mov si, vs_s_tirq
    xor dx, dx
    call bl_kv                      ; "...IRQs   <n>" (150 is a whole 5 s)
    pop si
    pop si
    jmp short .row
.done:
    call vs_tsil
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vs_tsil - VS_TGAP ticks of silence, the card stopped. The rows are found
; in a recording by these gaps
vs_tsil:
    push ax
    push si
    mov si, [vs_win]                ; (bl_paint's SI: the window)
    call bl_paint                   ; the rows so far, inside the gap
    pop si
    call OSAPI_GET_TICKS
    mov [vs_t0], ax
.w:
    call OSAPI_GET_TICKS
    sub ax, [vs_t0]
    cmp ax, VS_TGAP
    jb .w
    pop ax
    ret

; vs_trow - the tone for VS_TICKS with [vs_burn] turns burnt per interrupt,
; the foreground reading as [vs_tdisk] says. out: AX = IRQs, CX = reads
vs_trow:
    push bx
    push dx
    push es
    mov word [vs_irqs], 0
    mov word [vs_trk], 0
    mov byte [vs_head], 0
    mov byte [vs_cyl], 1
    mov byte [vs_sec], 1
    call vs_hook
    cmp byte [vs_thold], 0
    je .nt0
    call vs_t0hook
.nt0:
    mov al, 211
    mov bx, 735
    mov cx, 1470
    mov dl, 0x1C
    call vs_play
    call vs_race
    call OSAPI_GET_TICKS
    mov [vs_t0], ax
.rd:
    mov al, [vs_tdisk]
    or al, al
    jz .tick
    mov es, [vs_tbuf]
    xor bx, bx
    mov ch, [vs_cyl]
    mov dh, [vs_head]
    mov dl, 0x80
    cmp al, 1
    jne .one
    mov al, [vs_spt]                ; 1: a whole track a call
    mov ah, 2
    mov cl, 1
    int 0x13
    jc .tick
    inc word [vs_trk]
    jmp short .nexth
.one:
    mov cl, [vs_sec]                ; 2, 3: one sector a call
    mov ax, 0x0201
    cmp byte [vs_tdisk], 3
    je .mask
    int 0x13
    jmp short .got
.mask:
    call vs_tmask                   ; the holder's line shut for the call:
    mov ax, 0x0201                  ; an `sti` inside the ROM cannot open it
    int 0x13
    call vs_tunmask
.got:
    jc .tick
    inc word [vs_trk]
    mov al, [vs_sec]
    inc al
    mov [vs_sec], al
    cmp al, [vs_spt]
    jbe .tick
    mov byte [vs_sec], 1
.nexth:
    mov al, [vs_head]
    inc al
    cmp al, [vs_heads]
    jb .h
    xor al, al
    inc byte [vs_cyl]
.h:
    mov [vs_head], al
.tick:
    call OSAPI_GET_TICKS
    sub ax, [vs_t0]
    cmp ax, VS_TICKS
    jb .rd
    call vs_stop
    call vs_unhook
    cmp byte [vs_thold], 0
    je .nt0u
    call vs_t0unhook
.nt0u:
    mov ax, [vs_irqs]
    mov cx, [vs_trk]
    pop es
    pop dx
    pop bx
    ret

; vs_tmask / vs_tunmask - [vs_irq]'s bit set / cleared in the 8259's mask.
; An interrupt raised while it is set waits in the IRR, as an os8088 read
; holding off IRQ0 would leave the tick waiting
vs_tmask:
    push ax
    push cx
    pushf
    cli
    call vs_tline
    mov ah, 1
    shl ah, cl
    in al, 0x21
    or al, ah
    out 0x21, al
    popf
    pop cx
    pop ax
    ret

vs_tunmask:
    push ax
    push cx
    pushf
    cli
    call vs_tline
    mov ah, 1
    shl ah, cl
    not ah
    in al, 0x21
    and al, ah
    out 0x21, al
    popf
    pop cx
    pop ax
    ret

; vs_tline - CL = the holder's line: the card's, or 0 for IRQ0
vs_tline:
    mov cl, [vs_irq]
    cmp byte [vs_thold], 0
    je .o
    xor cl, cl
.o:
    ret

; vs_t0isr - IRQ0 first through here: burn [vs_tburn] turns with interrupts
; off, as a decode in the timer's hook does, then the kernel's own handler
vs_t0isr:
    push cx
    mov cx, [cs:vs_tburn]
    jcxz .n
.b:
    loop .b
.n:
    pop cx
    jmp far [cs:vs_t0vec]

vs_t0hook:
    push ax
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [es:0x20]
    mov [vs_t0vec], ax
    mov ax, [es:0x22]
    mov [vs_t0vec + 2], ax
    mov word [es:0x20], vs_t0isr
    mov [es:0x22], cs
    popf
    pop es
    pop ax
    ret

vs_t0unhook:
    push ax
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [vs_t0vec]
    mov [es:0x20], ax
    mov ax, [vs_t0vec + 2]
    mov [es:0x22], ax
    popf
    pop es
    pop ax
    ret

; vs_put - BX = result word index: store AX there, and a report line
vs_put:
    push bx
    shl bx, 1
    mov [vs_res + bx], ax
    pop bx
    push cx
    push dx
    xor dx, dx
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    ret

; =============================================================================
vs_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [vs_done], 0
    mov word [vs_flags], 0
    mov word [bl_nrow], 0
    mov si, vs_s_title
    cmp byte [vs_tmode], 0
    je .ttl
    mov si, vs_s_ttone
.ttl:
    call bl_sline
    cmp word [vs_buf], 0
    jne .have
    mov ax, VS_BUFKB * 64
    mov cx, VS_BUFKB * 64
    mov bl, MEMP_CONV + MEMP_DMAPOOL
    call OSAPI_MEM_P
    jc .noclaim
    mov [vs_buf], dx
    mov ax, VS_TRKKB * 64                ; the tracks are DMA'd here too: a
    mov cx, VS_TRKKB * 64                ; claim across a 64 KB page read NOTHING
    mov bl, MEMP_CONV + MEMP_DMAPOOL
    call OSAPI_MEM_P            ; on the 286 (error 09h every call)
    jc .noclaim
    mov [vs_tbuf], dx
.have:
    ; silence in the buffer, so a speaker plays nothing loud
    push es
    mov es, [vs_buf]
    xor di, di
    mov cx, VS_BUFKB * 512
    mov ax, 0x8080
    cld
    rep stosw
    cmp byte [vs_tmode], 0          ; (t): the sine over the ring's 1,470
    je .sil                         ; bytes, 42 periods of 35
    xor di, di
    mov dx, VS_TPER
.per:
    mov si, vs_sine
    mov cx, VS_TLEN
    rep movsb
    dec dx
    jnz .per
.sil:
    pop es
    ; --- the drivers out of the way, and the card's port off their record
    push es
    push ds
    pop es
    mov di, vs_recs
    xor bl, bl
    mov al, 1
    call OSAPI_DRV_SUSPEND
    pop es
    jc .nosusp
    mov byte [vs_susp], 1
    mov si, vs_recs
.rec:
    jcxz .guess                     ; SOUND.DRV is not wanted by default
    cmp byte [si + DQ_CLASS], DRVC_SOUND    ; (SPEC.md 51.3), so there may
    je .card                        ; be no record at all: try 220h
    add si, DQ_SIZE
    dec cx
    jmp short .rec
.card:
    mov ax, [si + DQ_A]
    mov [vs_port], ax
    mov ax, [si + DQ_C]
    mov [vs_ver], ax
.guess:
    call vs_pmup                    ; a PicoMEM: configured here, line known
    cmp al, 1
    je .found
    cmp al, 2
    je .nodsp
    call vs_reset
    jc .nodsp
    call vs_discover
    jc .noirq
.found:
    mov si, vs_r_pm                 ; the card, its own line, and channel 1
    mov ax, [pm_base]
    mov bx, 1
    call vs_put
    mov si, vs_r_pmirq
    mov al, [pm_pmirq]
    xor ah, ah
    mov bx, 3
    call vs_put
    mov si, vs_r_port
    mov ax, [vs_port]
    mov bx, 11
    call vs_put
    mov si, vs_r_irq
    mov al, [vs_irq]
    xor ah, ah
    mov bx, 12
    call vs_put
    mov si, vs_r_ver
    mov ax, [vs_ver]
    mov bx, 13
    call vs_put
    cmp byte [vs_tmode], 0
    je .crow
    call vs_tones                   ; (t) instead of (c) and (e)
    jmp .resume
.crow:

    ; --- (c) one interrupt a frame ----------------------------------------
    mov si, vs_s_hdrc
    call bl_sline
    mov word [vs_burn], 0
    mov al, 211                     ; 1e6 / 45 = 22,222 Hz
    mov bx, 735
    mov cx, 1470
    mov dl, 0x1C
    call vs_count
    mov ax, [vs_irqs]
    mov si, vs_r_p22
    mov bx, 0
    call vs_put
    mov al, 132                     ; 1e6 / 124 = 8,065 Hz
    mov bx, 134
    mov cx, 268
    mov dl, 0x1C
    call vs_count
    mov ax, [vs_irqs]
    mov si, vs_r_p8
    mov bx, 2
    call vs_put
    mov al, 211
    mov bx, 368                     ; 735 samples of 4-bit ADPCM
    mov cx, 736
    mov dl, 0x7D
    call vs_count
    mov ax, [vs_irqs]
    mov si, vs_r_ad
    mov bx, 4
    call vs_put

    ; --- (e) the ceiling, if there is a fixed disk ----------------------------
    mov ah, 8
    mov dl, 0x80
    push es
    int 0x13
    pop es
    jc .nohdd
    mov al, cl
    and al, 0x3F
    mov [vs_spt], al
    inc dh
    mov [vs_heads], dh
    mov si, vs_s_hdre
    call bl_sline
    mov word [vs_burn], 0
    call vs_ceil
    mov si, vs_r_c0
    mov bx, 6
    call vs_put
    mov word [vs_burn], VS_B25
    call vs_ceil
    mov si, vs_r_c25
    mov bx, 7
    call vs_put
    mov word [vs_burn], VS_B50
    call vs_ceil
    mov si, vs_r_c50
    mov bx, 8
    call vs_put
    mov word [vs_burn], VS_B75
    call vs_ceil
    mov si, vs_r_c75
    mov bx, 9
    call vs_put
    cmp byte [pm_up], 0             ; a PicoMEM's SB IRQ is a software int
    je .ion                         ; from INSIDE its multiplexer: an ISR
    or word [vs_flags], 64          ; that `sti`s re-enters it (header)
    mov si, vs_s_noion
    call bl_sline
    jmp short .ioff
.ion:
    mov word [vs_burn], VS_B50
    mov byte [vs_bsti], 1
    call vs_ceil
    mov byte [vs_bsti], 0
    mov si, vs_r_c50i
    mov bx, 15
    call vs_put
.ioff:
    mov al, [vs_spt]
    xor ah, ah
    mov si, vs_r_spt
    mov bx, 10
    call vs_put
    jmp short .resume
.nohdd:
    or word [vs_flags], 4
    jmp short .resume
.nocard:
    or word [vs_flags], 1
    jmp short .resume
.nodsp:
    or word [vs_flags], 2
    jmp short .resume
.noirq:
    or word [vs_flags], 8
.resume:
    cmp byte [pm_up], 0
    je .rs
    mov si, vs_r_drq
    mov al, [sbl_drqmask]
    xor ah, ah
    mov bx, 5
    call vs_put
    call pm_undo                    ; the DSP off again: SOUND.DRV's attach
    mov byte [pm_up], 0             ; expects the card as a reset leaves it
.rs:
    xor al, al
    push es
    push ds
    pop es
    xor di, di
    call OSAPI_DRV_SUSPEND          ; AL = 0: resume, on every path
    pop es
    jmp short .fin
.nosusp:
    or word [vs_flags], 16
    jmp short .fin
.noclaim:
    or word [vs_flags], 32
.fin:
    mov si, vs_r_flags
    mov ax, [vs_flags]
    mov bx, 14
    call vs_put
    mov si, vs_f_txt                ; the report, beside the bench
    cmp byte [vs_tmode], 0
    je .sv
    mov si, vs_f_tone
.sv:
    call bl_save
    inc word [vs_done]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define PM_CORE                     ; SOUND.DRV's PicoMEM tier, less its
%include "picomem.inc"              ; driver-only half (picomem.inc)
sbl_drqmask:  db 0                  ; pm_runaway's verdict: 4 = channel 1 RACED
                                    ; and stays masked (vs_dma ORs it in)

; vs_pmup - a PicoMEM SOUND.DRV configured, configured again HERE (header).
; out: AL = 0 no card (the normal path), 1 configured: [vs_port] answered a
; DSP reset and [vs_irq] = [pm_irq], 2 a card whose DSP answers nowhere
vs_pmup:
    push si
    call pm_init                    ; the ramp: no card, nothing written
    xor al, al
    cmp word [pm_base], 0
    je .out
    mov byte [pm_up], 1             ; whatever the driver or that call left:
    call pm_undo                    ; the DSP off, as a reset leaves it
    mov byte [pm_up], 0
    mov byte [pm_irq], 0
    call pm_init                    ; ...and on afresh
    mov al, 2
    cmp byte [pm_up], 0
    je .out
    mov si, pm_sbports              ; the port the firmware took: the first of
.port:                              ; its walk that answers a reset
    lodsw
    or ax, ax
    jz .none
    mov [vs_port], ax
    call vs_reset
    jc .port
    mov al, [pm_irq]
    mov [vs_irq], al
    mov al, 1
    jmp short .out
.none:
    mov al, 2
.out:
    pop si
    ret

%define BL_ARENA_BYTES 4000
%define BL_NUMBERED                 ; a field run saves NAME1, NAME2... (benchlib)
%define BL_INTRO_HOW 'automatically, when a test ends' ; benchlib's banner
%include "benchlib.inc"

vs_tpl:
    dw 7, 22, 632, 300
    dw vs_ttl, vs_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS vs_cls, 0, PAINT, vs_paint, ONKEY, vs_onkey, \
               ONCLICK, vs_onclick

vs_ttl:       db 'Video Sound Bench', 0
vs_s_title:   db 'VIDSND - a frame interrupt off the card (VIDEO-PLAN W0 c,e)', 0
vs_s_hint:    db 'R: ~50 s, the desktop frozen, VIDSND.TXT. T: the tone, ~65 s.', 0
vs_s_hdrc:    db '-- (c) interrupts in 5 s: PCM8 30 fps, PCM8 60 fps, ADPCM4 --', 0
vs_s_hdre:    db '-- (e) tracks read in 5 s, the ISR burning 0/25/50/75% --', 0
vs_r_port:    db 'DSP base port', 0
vs_r_irq:     db 'IRQ line found', 0
vs_r_ver:     db 'DSP version', 0
vs_r_p22:     db 'IRQs PCM8 22k/735', 0
vs_r_p8:      db 'IRQs PCM8 8k/134', 0
vs_r_ad:      db 'IRQs ADPCM4 22k/368', 0
vs_r_c0:      db 'tracks, burn 0%', 0
vs_r_c25:     db 'tracks, burn 25%', 0
vs_r_c50:     db 'tracks, burn 50%', 0
vs_r_c75:     db 'tracks, burn 75%', 0
vs_r_c50i:    db 'tracks, 50% ints on', 0
vs_f_txt:     db 'VIDSND.TXT', 0
vs_r_spt:     db 'sectors per track', 0
vs_r_flags:   db 'flags', 0
vs_r_pm:      db 'PicoMEM base (0 none)', 0
vs_r_pmirq:   db "...the card's own IRQ", 0
vs_r_drq:     db 'DMA ch 1 masked (4)', 0
vs_s_noion:   db 'tracks, 50% ints on: NOT RUN - a PicoMEM ISR may not sti', 0
vs_cands:     db 7, 5, 3, 2, 0
vs_s_ttone:   db 'VIDSND T - a 635 Hz tone, held and read under (PicoMEM 2)', 0
vs_s_thdr:    db '-- (t) reads in 5 s, each row after 2 s of silence --', 0
vs_s_tirq:    db '  ...IRQs (151 = 5 s)', 0
vs_s_tnohd:   db 'NOT RUN - no fixed disk', 0
vs_f_tone:    db 'VSTONE.TXT', 0
vs_tl_a:      db 'A tone alone', 0
vs_tl_b:      db 'B hold 16 ms, no disk', 0
vs_tl_c:      db 'C tracks, hold 0', 0
vs_tl_d:      db 'D tracks, hold 4 ms', 0
vs_tl_e:      db 'E tracks, hold 8 ms', 0
vs_tl_f:      db 'F tracks, hold 16 ms', 0
vs_tl_g:      db 'G 1 sector/call, hold 16', 0
vs_tl_h:      db 'H 1 sector/call masked, 16', 0
vs_tl_i:      db 'I IRQ0 holds 16, 1 sector', 0
vs_tl_j:      db 'J IRQ0 16, 1 sector masked', 0
; the rows: the hold in ms; the disk (0 none, 1 tracks, 2 sectors, 3 sectors
; with the holder's line masked for each call); the holder (0 the card's ISR,
; 1 IRQ0 - the timer, as Video Battle's and Video Player's decode is); the label
vs_trows:     dw 0
              db 0, 0
              dw vs_tl_a
              dw 16
              db 0, 0
              dw vs_tl_b
              dw 0
              db 1, 0
              dw vs_tl_c
              dw 4
              db 1, 0
              dw vs_tl_d
              dw 8
              db 1, 0
              dw vs_tl_e
              dw 16
              db 1, 0
              dw vs_tl_f
              dw 16
              db 2, 0
              dw vs_tl_g
              dw 16
              db 3, 0
              dw vs_tl_h
              dw 16
              db 2, 1
              dw vs_tl_i
              dw 16
              db 3, 1
              dw vs_tl_j
              dw 0xFFFF
vs_sine:      db 128, 139, 150, 161, 170, 178, 184, 189, 191, 192, 190, 187
              db 181, 174, 166, 156, 145, 134, 122, 111, 100, 90, 82, 75
              db 69, 66, 64, 65, 67, 72, 78, 86, 95, 106, 117

vs_win:       dw 0
vs_buf:       dw 0
vs_tbuf:      dw 0
vs_port:      dw 0x220
vs_ver:       dw 0
vs_irqs:      dw 0
vs_burn:      dw 0
vs_t0:        dw 0
vs_trk:       dw 0
vs_done:      dw 0
vs_flags:     dw 0
vs_ovec:      dw 0, 0
vs_irq:       db 0
vs_omask:     db 0
vs_susp:      db 0
vs_bsti:      db 0
vs_tmode:     db 0
vs_thold:     db 0
vs_tburn:     dw 0
vs_t0vec:     dw 0, 0
vs_tdisk:     db 0
vs_sec:       db 0
vs_spt:       db 0
vs_heads:     db 0
vs_cyl:       db 0
vs_head:      db 0
vs_res:       times 16 dw 0
vs_recs:      times DQ_MAXREC * DQ_SIZE db 0

VS_BSS_OWN  equ 0
    OS88_BSS BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

    BL_BSS os88_image_end
