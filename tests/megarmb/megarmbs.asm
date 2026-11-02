; =============================================================================
; os8088 - tests/megarmb/megarmbs.asm
;
; MEGARMBS, a TEST instrument (nothing under tests/ ships): the SOUND side of
; kern_mega's REAL-MODE BRACKET (SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-
; REALMODE-PLAN.md W4) - SOUND.DRV carried below 1MB by FSXL_DRV DRVC_SOUND,
; a Sound Blaster stream through the OSAPI_SND_STREAM twin, the stream
; carried ACROSS the bracket both ways with the card as the clock, the
; watchdog that only the tick's DSV_TICK can fire, and the speaker door's
; twin writing IVT 20h. OS88_HIGH | OS88_HIGHMEM, so its region is above 1MB
; and the bracket runs a COPY of it (W1).
;
; ONE STREAM, a DIRECT EXTERNAL RING (SPEC.md 34.5.3): 32 KB of a sawtooth
; (byte n = n & FFh) in a conventional DMA claim, its two control words after
; it. The feeder keeps the total one ring ahead of what the card reports, so
; it never underruns and the card plays the ring over and over; every
; consumed count the block ISR publishes is summed (32 bits) per leg.
;
;   Z  THE CONTROL: protected mode alone, before any bracket, opens,
;      plays 2 s and closes - what every leg below is measured against
;   P  PLAY: a real-mode bracket OPENS the stream (verb 0 through the twin)
;      and feeds it for 2 s, then returns with it PLAYING - carried OUT
;   Q  ...protected mode feeds it for 2 s: the ISR there reads the words the
;      return renamed (a stream opened in real mode stored paragraphs)
;   L  a real-mode bracket again - carried IN - 2 s
;   M  protected mode, 2 s
;   K  a real-mode bracket, 1 s, and the stream CLOSED inside
;   O  LIVE: protected mode OPENS a second stream and feeds it 2 s
;   I  ...carried IN to a real-mode bracket, 2 s
;   J  ...protected mode, 2 s, and closed there
;   W  THE WATCHDOG: a real-mode bracket opens a stream, plays 0.5 s, then
;      masks IRQ 3, 5 and 7 - the block IRQs stop, and only SOUND.DRV's
;      DSV_TICK, which the real-mode tick runs through the low copy, can
;      notice: verb 3 must answer ENDED (2), and in how many ticks
;   S  THE SPEAKER: a real-mode FSXF_RATE bracket (divisor 4096) with NO
;      list: OSAPI_SND_STREAM answers AX = 4 (no sink carried), FSX_SPK AL = 0
;      refuses AX = 1, AL = 2 opens with this package's sample ISR at
;      N = 100 and answers K; channel 0 at N for 2 s; closed; and IVT 20h
;      read back after the close
;
; THE RESULT BLOCK is the bss (ms_res), read by tests/vidrmbsnd.py off the
; package's own memory; every leg also prints
;      MEGARMBS: <leg> cf C why WW feat FF err EEEE st SSSS t TTTT c CCCCCCCC
;                x X1 X2 X3 X4 X5 X6 X7 X8
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARMBS', ms_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

MS_WAIT     equ 36                  ; the timer: two seconds after launch
MS_RL       equ 32768               ; the ring: size code 3 - 4 s of lead,
                                    ; so a desktop repaint between legs
                                    ; cannot underrun it
MS_RATE     equ 8000
MS_T        equ 36                  ; a leg's play: 2 s of ticks
MS_DIV      equ 4096                ; S's rate (291 Hz)
MS_N        equ 100                 ; S's sample: 11,932 Hz
MS_OPEN     equ SND_OPENF_RING | SND_OPENF_EXT | (3 << SND_OPENF_RLSH)
LEG_SZ      equ 32
L_CF        equ 0                   ; db: FFh = OSAPI_FSX_RUN refused
L_WHY       equ 1                   ; db: SI's answer
L_FEAT      equ 2                   ; db: OSAPI_CPU_INFO's AH inside
L_RAN       equ 3                   ; db: 1 = the leg's body ran
L_ERR       equ 4                   ; dw: the first failure's AX|8000h (0 none)
L_ST        equ 6                   ; dw: verb 3's state at the leg's end
L_T         equ 8                   ; dw: the ticks the leg played
L_C         equ 10                  ; dd: the bytes the card consumed
L_X         equ 14                  ; dw x 8: the leg's own
LEGS        equ 11                  ; P Q L M K O I J W S Z

ms_entry:
    push si
    mov si, ms_s_start
    call ms_say
    call ms_nl
    mov si, ms_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [ms_win], bx
    mov ax, ms_tick
    call OSAPI_WM_ONTIMER
    mov ax, MS_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    pop si
    ret

ms_paint:
    ret

; the timer: a window callback under the gfx lock - the bracket's context
ms_tick:
    cmp byte [ms_ran], 0
    je .go
    ret
.go:
    mov byte [ms_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, ms_ring2                ; TWO rings: the streams protected mode
    call ms_ringc                   ; opens play ms_ring, the ones a bracket
    jc .nomem                       ; opens ms_ring2 - so a stream opened
    mov [ms_rsel], dx               ; inside stores a segment the drop never
    mov bx, ms_ring                 ; made, and the return must find its
    call ms_ringc                   ; claim by its base (mega_p2d)
    jc .nomem
    mov si, ms_list
    mov di, ms_list_bank
    call os88real_prep              ; the rings' real segments, banked
    mov si, ms_s_rings              ; (E9: the two claims, and the banks)
    call ms_say
    mov ax, [ms_ring]
    call ms_hex4
    mov al, ' '
    call ms_out1
    mov ax, [ms_ring2]
    call ms_hex4
    mov al, ' '
    call ms_out1
    mov ax, [ms_list_bank]
    call ms_hex4
    mov al, ' '
    call ms_out1
    mov ax, [ms_list_bank+2]
    call ms_hex4
    call ms_nl
    ; --- Z: the CONTROL - protected mode alone, before any bracket -------
    mov word [ms_rp], ms_ring
    mov al, 10
    call ms_rec
    mov di, [ms_cur]
    mov byte [di+L_RAN], 1
    call ms_open
    jc .oz
    mov cx, MS_T
    call ms_play
    call ms_close
.oz:
    call ms_rep
    ; --- PLAY: opened inside, carried out, in, out, in and closed ---------
    mov word [ms_rp], ms_ring2
    mov al, 0
    mov word [ms_body], ms_b_open
    call ms_leg
    mov al, 1
    call ms_pm
    mov al, 2
    mov word [ms_body], ms_b_feed
    call ms_leg
    mov al, 3
    call ms_pm
    mov al, 4
    mov word [ms_body], ms_b_close
    call ms_leg
    ; --- LIVE: opened in protected mode, carried in, out and closed ------
    mov word [ms_rp], ms_ring
    mov al, 5
    call ms_rec
    call ms_open
    jc .o5
    mov cx, MS_T
    call ms_play
.o5:
    mov di, [ms_cur]
    mov byte [di+L_RAN], 1
    call ms_rep
    mov al, 6
    mov word [ms_body], ms_b_feed
    call ms_leg
    mov al, 7
    call ms_rec
    mov di, [ms_cur]
    mov byte [di+L_RAN], 1
    mov cx, MS_T
    call ms_play
    call ms_close
    call ms_rep
    ; --- the watchdog, and the speaker --------------------------------------
    mov word [ms_rp], ms_ring2
    mov al, 8
    mov word [ms_body], ms_b_wd
    call ms_leg
    mov al, 9
    mov word [ms_body], ms_b_spk
    call ms_leg
.nomem:
    mov si, ms_s_done
    call ms_say
    call ms_nl
    mov byte [ms_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ms_ringc - BX = a ring word: a conventional, page-safe claim of the ring
; and its words, filled with the sawtooth. out DX the claim, CF
ms_ringc:
    mov ax, (MS_RL + 1024) / 1024   ; the ring and its words, conventional
    mov cx, ax                      ; and all of it page-safe (the DMA door)
    push bx
    call OSAPI_MEM_CLAIM_DMA
    pop bx
    jc .o
    mov [bx], dx
    mov es, dx                      ; the sawtooth, and the words zero
    xor di, di
    xor ax, ax
    mov cx, MS_RL
    cld
.fill:
    stosb
    inc al
    loop .fill
    xor ax, ax
    stosw
    stosw
    clc
.o:
    ret

; ms_rec - AL = the leg's index: [ms_cur] its record, clean, the letter kept
ms_rec:
    push ax
    push cx
    push di
    push es
    mov [ms_leg_n], al
    mov ah, LEG_SZ
    mul ah
    add ax, ms_legs
    mov [ms_cur], ax
    mov di, ax
    push ds
    pop es
    xor ax, ax
    mov cx, LEG_SZ / 2
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; ms_pm - AL = the leg: protected mode feeds the open stream for MS_T ticks
ms_pm:
    call ms_rec
    mov di, [ms_cur]
    mov byte [di+L_RAN], 1
    mov cx, MS_T
    call ms_play
    jmp ms_rep

; ms_leg - AL = the leg, [ms_body] its body: a REAL-MODE bracket (S's with
; FSXF_RATE and no list, every other with FSXL_DRV DRVC_SOUND)
ms_leg:
    call ms_rec
    mov bx, [ms_win]
    mov ax, ms_main
    mov cx, FSXF_REAL
    mov bp, ms_lsnd
    cmp word [ms_body], ms_b_spk
    jne .run
    mov cx, FSXF_REAL | FSXF_RATE
    mov dx, MS_DIV
    mov di, ms_hook
    xor bp, bp
.run:
    xor si, si                      ; FSXR_REAL in
    call OSAPI_FSX_RUN
    sbb dl, dl
    mov di, [ms_cur]
    mov [di+L_CF], dl
    mov ax, si
    mov [di+L_WHY], al
    jmp ms_rep

; THE ENTRY, in whichever bracket the kernel gave
ms_main:
    mov di, [ms_cur]
    call OSAPI_CPU_INFO
    mov [di+L_FEAT], ah
    mov byte [di+L_RAN], 1
    mov si, ms_list
    mov di, ms_list_bank
    call os88real_swap              ; the ring's word REAL, in a real bracket
    call [ms_body]
    mov si, ms_list
    mov di, ms_list_bank
    call os88real_swap
    ret

ms_b_open:                          ; P: opened here, left playing
    call ms_open
    jc .o
    mov cx, MS_T
    call ms_play
.o:
    ret
ms_b_feed:                          ; L, I: the stream carried in
    mov cx, MS_T
    jmp ms_play
ms_b_close:                         ; K: half the play, then closed here
    mov cx, MS_T / 2
    call ms_play
    jmp ms_close
ms_b_wd:                            ; W: the IRQs stopped, the tick decides
    call ms_open
    jc .o
    mov cx, MS_T / 4
    call ms_play
    in al, 0x21
    mov [ms_imr], al
    or al, 0xA8                     ; IRQ 3, 5 and 7: the card's candidates
    out 0x21, al
    call OSAPI_GET_TICKS
    mov [ms_t0], ax
.w:
    call ms_feed
    call ms_stat                    ; AX = the state
    cmp ax, SND_ST_ENDED
    je .end
    call OSAPI_GET_TICKS
    sub ax, [ms_t0]
    cmp ax, 90
    jb .w
.end:
    call OSAPI_GET_TICKS
    sub ax, [ms_t0]
    mov di, [ms_cur]
    mov [di+L_X], ax                ; the ticks the watchdog took
    mov al, [ms_imr]
    out 0x21, al
    call ms_close
.o:
    ret

; S: the speaker door's twin, in a real-mode FSXF_RATE bracket
ms_b_spk:
    mov di, [ms_cur]
    mov al, 3                       ; no list, so no sink: AX = 4, CF
    mov ah, 0
    call OSAPI_SND_STREAM
    sbb dx, dx
    and dx, 0x8000
    or ax, dx
    mov [di+L_X], ax
    xor ax, ax                      ; the hook's counters, armed
    mov [ms_per], ax
    mov [ms_per+2], ax
    mov [ms_hn], ax
    mov [ms_hif], ax
    mov [ms_samp], ax
    mov [ms_samp+2], ax
    mov byte [ms_rdy], 1
    pushf
    cli
    xor al, al                      ; AL = 0: the caller would write IVT
    mov bx, cs                      ; 08h - refused here
    mov dx, MS_N
    call OSAPI_FSX_SPK
    sbb dx, dx
    and dx, 0x8000
    or ax, dx
    mov di, [ms_cur]
    mov [di+L_X+2], ax
    mov al, 2                       ; AL = 2: the door writes IVT 20h
    mov bx, cs
    mov dx, MS_N
    mov si, ms_isr
    call OSAPI_FSX_SPK
    jc .no
    mov [ms_k], ax
    mov [ms_kc], ax
    mov [ms_chain], dx
    mov [ms_chain+2], cx
    mov di, [ms_cur]
    mov [di+L_X+4], ax              ; K
    mov [di+L_X+6], dx              ; the chain
    mov [di+L_X+8], cx
    mov al, 0x34                    ; channel 0 at N: a sample an IRQ0
    out 0x43, al
    mov ax, MS_N
    out 0x40, al
    mov al, ah
    out 0x40, al
    popf
    call OSAPI_GET_TICKS
    mov [ms_t0], ax
.w:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    call OSAPI_GET_TICKS
    sub ax, [ms_t0]
    cmp ax, MS_T
    jb .w
    mov di, [ms_cur]
    mov [di+L_T], ax
    pushf
    cli
    mov al, 1                       ; closed: IVT 20h the tick's again
    call OSAPI_FSX_SPK
    mov byte [ms_rdy], 0
    push es
    xor ax, ax
    mov es, ax
    mov ax, [es:0x20*4]
    mov dx, [es:0x20*4+2]
    pop es
    popf
    mov di, [ms_cur]
    mov [di+L_X+10], ax
    mov [di+L_X+12], dx
    mov ax, [ms_samp]
    mov [di+L_C], ax
    mov ax, [ms_samp+2]
    mov [di+L_C+2], ax
    mov ax, [ms_hn]
    mov [di+L_X+14], ax
    mov ax, [ms_hif]                ; (L_ST: the hook's calls at IF = 1)
    mov [di+L_ST], ax
    ret
.no:
    popf
    or ax, 0x8000
    mov di, [ms_cur]
    mov [di+L_ERR], ax
    mov byte [ms_rdy], 0
    ret

; the rate hook (S): the periods, the calls, and the calls at IF = 1
ms_hook:
    cmp byte [ms_rdy], 0
    je .o
    add [ms_per], ax
    adc word [ms_per+2], 0
    inc word [ms_hn]
    pushf
    pop ax
    test ah, 2
    jz .o
    inc word [ms_hif]
.o:
    ret

; the SAMPLE ISR (S), in real mode: counts, and every K-th entry is the
; chain's - the kernel's IRQ0, which sends the EOI
ms_isr:
    inc word [cs:ms_samp]
    jnz .k
    inc word [cs:ms_samp+2]
.k:
    dec word [cs:ms_kc]
    jz .chain
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
.chain:
    push ax
    mov ax, [cs:ms_k]
    mov [cs:ms_kc], ax
    pop ax
    jmp far [cs:ms_chain]

; --- the stream ---------------------------------------------------------------
; ms_open - verb 0, an external ring at [ms_ring]:0; [ms_h] the handle, the
; consumed count's base taken. CF = 1 refused, the leg's L_ERR
ms_open:
    mov al, 0
    mov ah, MS_OPEN
    mov dx, MS_RATE
    xor si, si
    mov bx, [ms_rp]
    mov di, [bx]
    mov cx, MS_RL
    push es
    mov es, di                      ; the words: a ring ahead, before the
    mov word [es:MS_RL + SND_EXT_TOTAL], MS_RL ; card reads them
    mov word [es:MS_RL + SND_EXT_CONS], 0
    pop es
    call OSAPI_SND_STREAM
    jc .err
    mov [ms_h], ah
    mov word [ms_lastc], 0
    clc
    ret
.err:
    or ax, 0x4000
    jmp short ms_err

; ms_close - verb 2
ms_close:
    mov al, 2
    mov ah, [ms_h]
    call OSAPI_SND_STREAM
    jnc .o
    or ax, 0x2000
    jmp short ms_err
.o:
    ret

ms_err:
    mov di, [ms_cur]
    cmp word [di+L_ERR], 0
    jne .o
    or ax, 0x8000
    mov [di+L_ERR], ax
.o:
    stc
    ret

; ms_stat - verb 3: AX = the state
ms_stat:
    push dx
    mov al, 3
    mov ah, [ms_h]
    call OSAPI_SND_STREAM
    pop dx
    ret

; ms_play - CX ticks of feeding: the consumed count summed into L_C, the
; ticks into L_T, verb 3's state at the end into L_ST
ms_play:
    call OSAPI_GET_TICKS
    mov [ms_t0], ax
    call ms_feed                    ; (the base: what was consumed before)
    mov di, [ms_cur]
    xor ax, ax
    mov [di+L_C], ax
    mov [di+L_C+2], ax
.l:
    call ms_feed
    call OSAPI_GET_TICKS
    sub ax, [ms_t0]
    cmp ax, cx
    jb .l
    mov di, [ms_cur]
    mov [di+L_T], ax
    call ms_stat
    mov di, [ms_cur]
    mov [di+L_ST], ax
    ret

; ms_feed - the consumed count the card published, its step summed; the
; total one ring past it. Clobbers AX, DX, DI
ms_feed:
    push es
    push bx
    mov bx, [ms_rp]
    mov es, [bx]
    pop bx
    mov ax, [es:MS_RL + SND_EXT_CONS]
    mov dx, ax
    sub dx, [ms_lastc]
    mov [ms_lastc], ax
    add ax, MS_RL
    mov [es:MS_RL + SND_EXT_TOTAL], ax
    pop es
    mov di, [ms_cur]
    add [di+L_C], dx
    adc word [di+L_C+2], 0
    ret

; ms_rep - the leg's line on E9
ms_rep:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, [ms_cur]
    mov si, ms_name0
    call ms_puts
    mov bl, [ms_leg_n]
    xor bh, bh
    mov al, [ms_letters+bx]
    call ms_out1
    mov si, ms_s_cf
    call ms_puts
    mov al, [di+L_CF]
    call ms_hex2
    mov si, ms_s_why
    call ms_puts
    mov al, [di+L_WHY]
    call ms_hex2
    mov si, ms_s_feat
    call ms_puts
    mov al, [di+L_FEAT]
    call ms_hex2
    mov si, ms_s_err
    call ms_puts
    mov ax, [di+L_ERR]
    call ms_hex4
    mov si, ms_s_st
    call ms_puts
    mov ax, [di+L_ST]
    call ms_hex4
    mov si, ms_s_t
    call ms_puts
    mov ax, [di+L_T]
    call ms_hex4
    mov si, ms_s_c
    call ms_puts
    mov ax, [di+L_C+2]
    call ms_hex4
    mov ax, [di+L_C]
    call ms_hex4
    mov si, ms_s_x
    call ms_puts
    mov cx, 8
.x:
    mov al, ' '
    call ms_out1
    mov ax, [di+L_X]
    call ms_hex4
    inc di
    inc di
    loop .x
    call ms_nl
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- E9 ----------------------------------------------------------------------
ms_say:
    push si
    mov si, ms_name0
    call ms_puts
    pop si
ms_puts:
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
ms_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
ms_out1:
    out 0xE9, al
    ret
ms_hex4:
    push ax
    mov al, ah
    call ms_hex2
    pop ax
ms_hex2:
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

%include "os88real.inc"

    OS88REAL_LIST ms_list, ms_ring, ms_ring2 ; the rings' claim words

ms_ring     dw 0
ms_ring2    dw 0
ms_rp       dw ms_ring              ; the stream's ring word
ms_k        dw 0                    ; S: the samples a period...
ms_kc       dw 0                    ; ...counting down to the chain
ms_chain    dw 0, 0                 ; ...the chain
ms_samp     dw 0, 0                 ; ...the samples the ISR took
ms_rdy      db 0                    ; ...1 = the hook counts
ms_lsnd:    db FSXL_DRV, DRVC_SOUND, FSXL_END
ms_letters: db 'PQLMKOIJWSZ'
ms_tpl:
    dw 8, 300, 200, 40
    dw ms_ttl, ms_paint, 0, 0
ms_ttl:     db 'MegaRMB sound', 0
ms_name0:   db 'MEGARMBS: ', 0
ms_s_start: db 'start', 0
ms_s_done:  db 'done', 0
ms_s_rings: db 'rings ', 0
ms_s_cf:    db ' cf ', 0
ms_s_why:   db ' why ', 0
ms_s_feat:  db ' feat ', 0
ms_s_err:   db ' err ', 0
ms_s_st:    db ' st ', 0
ms_s_t:     db ' t ', 0
ms_s_c:     db ' c ', 0
ms_s_x:     db ' x', 0

; THE RESULT BLOCK, in the bss (tests/vidrmbsnd.py reads it: its offset is
; os88_image_end, the .bin header's image size)
    OS88_BSS MS_RESN
    OS88_IMAGE_END

ms_res      equ os88_image_end
ms_fin      equ ms_res + 0          ; byte: 1 = every leg ran
ms_ran      equ ms_res + 1
ms_win      equ ms_res + 2
ms_per      equ ms_res + 4          ; dd: S's rate periods, by the hook
ms_hn       equ ms_res + 8          ; S's hook calls...
ms_hif      equ ms_res + 10         ; ...at IF = 1
ms_rsel     equ ms_res + 12         ; the ring's claim, its selector
ms_legs     equ ms_res + 16         ; P Q L M K O I J W S Z: LEG_SZ each
ms_cur      equ ms_legs + LEGS * LEG_SZ
ms_body     equ ms_cur + 2
ms_h        equ ms_body + 2         ; byte: the stream's handle
ms_leg_n    equ ms_h + 1
ms_lastc    equ ms_h + 2
ms_t0       equ ms_lastc + 2
ms_imr      equ ms_t0 + 2
MS_RESN     equ ms_imr + 2 - ms_res
