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
; It SUSPENDS the drivers (OSAPI_DRV_SUSPEND, SPEC.md 51.11) and programs the
; DSP, DMA channel 1 and the PIC itself, as the fallback of VIDEO-PLAN 4.4
; would, and resumes them on every path out. The IRQ line is found here with
; DSP F2h (SPEC.md 96.17.1: the driver defers discovery to first use).
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
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call vs_run
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
    out 0x0A, al                    ; unmask
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
    call bl_sline
    cmp word [vs_buf], 0
    jne .have
    mov ax, VS_BUFKB
    mov cx, VS_BUFKB
    call OSAPI_MEM_CLAIM_DMA
    jc .noclaim
    mov [vs_buf], dx
    mov ax, VS_TRKKB                ; the tracks are DMA'd here too: a
    mov cx, VS_TRKKB                ; claim across a 64 KB page read NOTHING
    call OSAPI_MEM_CLAIM_DMA        ; on the 286 (error 09h every call)
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
    call vs_reset
    jc .nodsp
    call vs_discover
    jc .noirq
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
    mov word [vs_burn], VS_B50
    mov byte [vs_bsti], 1
    call vs_ceil
    mov byte [vs_bsti], 0
    mov si, vs_r_c50i
    mov bx, 15
    call vs_put
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

%define BL_ARENA_BYTES 4000
%include "benchlib.inc"

vs_tpl:
    dw 7, 22, 632, 300
    dw vs_ttl, vs_paint, vs_onkey, vs_onclick

vs_ttl:       db 'Video Sound Bench', 0
vs_s_title:   db 'VIDSND - a frame interrupt off the card (VIDEO-PLAN W0 c,e)', 0
vs_s_hint:    db 'Press R: ~50 s, the desktop frozen. Saves VIDSND.TXT.', 0
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
vs_cands:     db 7, 5, 3, 2, 0

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
