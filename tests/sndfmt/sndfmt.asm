; =============================================================================
; os8088 - tests/sndfmt/sndfmt.asm
;
; SNDFMT: the gate package for SPEC.md 34.5.5's stereo and 16-bit streams. It
; is NEVER shipped - `make sndfmttest` builds it onto scratch disks of its own,
; and tests/sndfmt.py drives it. It needs no input: the entry arms the window
; timer and every step after that is a timer callback (UI-task context, which
; the open verbs and verb 11 require - SPEC.md 20.3), so an \AUTOSTRT link
; starts the whole run and QEMU needs no mouse.
;
; What it does, in order, writing every answer into the RESULTS block below
; (found by the host through its magic, at a fixed offset in the image):
;
;   caps     OSAPI_SND_CAPS
;   R1       verb 11 with an unknown bit (4)                      -> 7
;   R2       verb 11 STEREO|16, then a LINEAR open                -> 7
;   R3       verb 11 STEREO|16, then a ring at 4,500 Hz           -> 2
;   R4       verb 11 STEREO|16, then a ring at an ODD grant offset -> 7
;   P0..P4   five phases, each: verb 11, a 16KB ring filled with a periodic
;            tone (1KB chunks, so the ring never needs refilling), verb 0,
;            then ~2.2 s fed every tick (total = consumed + RL), verb 9's
;            played bytes and the ticks it took, verb 3's state, close. A
;            phase whose verb 11 refuses is not opened.
;              P0  16-bit STEREO 22,050 Hz: left 344.5 Hz at 8,000, right
;                  1,378 Hz at 600 (-35 dBFS: 8 bits would be ~2 LSB of it)
;              P1   8-bit STEREO 22,050 Hz: left 689 Hz at 40, right
;                  344.5 Hz at 100
;              P2  16-bit MONO   22,050 Hz: 1,378 Hz at 6,000
;              P3  16-bit STEREO on an EXTERNAL ring (SPEC.md 34.5.3): a
;                  page-safe claim of our own, written directly, fed by
;                  writing SND_EXT_TOTAL - left 689 Hz at 5,000, right
;                  344.5 Hz at 12,000
;              P4  16-bit STEREO, STARVED: not fed for 15 ticks, so the ISR
;                  underruns it (D5h, the 16-bit halt) - verb 3's state is
;                  read then - and fed again, which resumes it (D6h) for the
;                  25 ticks left. Left 1,378 Hz at 3,000, right 689 Hz at 9,000
;            with ~0.5 s of nothing between phases.
;
; Window procs preserve every register (SPEC.md 20.5). The kernel reaches a
; callback through the package header's dispatcher, so each is a near proc.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SNDFMT', sf_entry

SF_RL      equ 16384                ; the ring: a power of two, 4 x the half
SF_CHUNK   equ 1024                 ; one staged chunk, a whole number of
                                    ; periods of every tone below
SF_RATE    equ 22050
SF_PLAY    equ 40                   ; ticks a phase plays (~2.2 s)
SF_GAP     equ 10                   ; ticks between phases (~0.55 s)
SF_NPH     equ 5
SF_STARVE  equ 15                   ; P4: ticks unfed
PF_EXT     equ 1                    ; a phase's flags: an external ring...
PF_STARVE  equ 2                    ; ...or one left to underrun

; the timer's states
ST_START   equ 0                    ; caps and the four refusals
ST_OPEN    equ 1                    ; open phase [sf_ph]
ST_FEED    equ 2                    ; keep it fed; close at the end
ST_GAP     equ 3                    ; silence, then the next phase
ST_DONE    equ 4

; -----------------------------------------------------------------------------
; sf_entry - package entry (SPEC.md 20.2): the window, its timer, armed
; -----------------------------------------------------------------------------
sf_entry:
    push si
    push ax
    mov si, sf_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov ax, sf_tick
    call OSAPI_WM_ONTIMER
    mov ax, 9                       ; half a second for the desktop to settle
    call OSAPI_WM_TIMER
    clc
.out:
    pop ax
    pop si
    ret

; -----------------------------------------------------------------------------
; sf_paint - W_PAINT: one line saying which step the run is on
; -----------------------------------------------------------------------------
sf_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov al, [sf_state]
    add al, '0'
    mov [sf_line+6], al
    mov si, sf_line
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sf_nop:
    ret

; -----------------------------------------------------------------------------
; sf_tick - the window timer: one step of the run, then re-arm
; in:  SI = the window; UI task, gfx lock held (OSAPI_WM_ONTIMER)
; -----------------------------------------------------------------------------
sf_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push si                         ; the window, for the re-arm
    mov al, [sf_state]
    cmp al, ST_START
    jne .n0
    call sf_refusals
    mov byte [sf_state], ST_OPEN
    jmp .again
.n0:
    cmp al, ST_OPEN
    jne .n1
    call sf_open
    jmp .again
.n1:
    cmp al, ST_FEED
    jne .n2
    call sf_feed
    jmp .again
.n2:
    cmp al, ST_GAP
    jne .stop
    dec byte [sf_wait]
    jnz .again
    mov byte [sf_state], ST_OPEN
.again:
    pop bx
    cmp byte [sf_state], ST_DONE
    je .done
    mov ax, 1
    call OSAPI_WM_TIMER
    jmp short .out
.stop:
    pop bx
.done:
    mov byte [sf_done], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sf_refusals - caps, then R1..R4. Every grant taken here is freed here.
; -----------------------------------------------------------------------------
sf_refusals:
    call OSAPI_SND_CAPS
    mov [sf_caps], ax
    ; R1: an unknown format bit
    mov ax, 0x0400 | SND_V_FMT
    call OSAPI_SND_STREAM
    mov [sf_r1], ax
    ; R2: a format, then a LINEAR open of a 4KB grant
    mov cx, 4096
    mov ax, 0x0007
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .r2x
    mov [sf_g1], si
    mov al, SND_FMT_STEREO | SND_FMT_16
    call sf_fmt
    mov ax, 0x0000                  ; verb 0, AH = 0: linear
    mov si, [sf_g1]
    mov cx, 4096
    mov dx, SF_RATE
    call OSAPI_SND_STREAM
    mov [sf_r2], ax
    call sf_close_if_open           ; (only if it wrongly opened)
    mov si, [sf_g1]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
.r2x:
    ; R3: a format, then a ring at 4,500 Hz - below the SB16's 41h range
    mov cx, SF_RL
    mov ax, 0x0007
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .r3x
    mov [sf_g1], si
    mov al, SND_FMT_STEREO | SND_FMT_16
    call sf_fmt
    mov ax, (SND_OPENF_RING << 8) | 0
    mov si, [sf_g1]
    mov cx, SF_RL
    mov dx, 4500
    call OSAPI_SND_STREAM
    mov [sf_r3], ax
    call sf_close_if_open
    mov si, [sf_g1]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
.r3x:
    ; R4: a format, then a ring whose grant starts at an ODD offset - a
    ; one-byte grant first, so the ring's lands at pool offset 1
    mov cx, 1
    mov ax, 0x0007
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .r4x
    mov [sf_g2], si
    mov cx, SF_RL
    mov ax, 0x0007
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .r4f
    mov [sf_g1], si
    mov [sf_r4off], si
    mov al, SND_FMT_STEREO | SND_FMT_16
    call sf_fmt
    mov ax, (SND_OPENF_RING << 8) | 0
    mov si, [sf_g1]
    mov cx, SF_RL
    mov dx, SF_RATE
    call OSAPI_SND_STREAM
    mov [sf_r4], ax
    call sf_close_if_open
    mov si, [sf_g1]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
.r4f:
    mov si, [sf_g2]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
.r4x:
    ret

; sf_fmt - verb 11 with AL = the format; AX = its answer. Clobbers AX.
sf_fmt:
    mov ah, al
    mov al, SND_V_FMT
    call OSAPI_SND_STREAM
    ret

; sf_close_if_open - a refusal test that OPENED (AL = 0): close it, so the
; next step is not refused busy. In: AX = verb 0's answer. Clobbers AX.
sf_close_if_open:
    or al, al
    jnz .out
    mov al, 2                       ; AH = the handle verb 0 answered
    call OSAPI_SND_STREAM
.out:
    ret

; -----------------------------------------------------------------------------
; sf_open - phase [sf_ph]: verb 11, a 16KB ring filled with its tone, verb 0
; -----------------------------------------------------------------------------
sf_open:
    mov bl, [sf_ph]
    cmp bl, SF_NPH
    jb .go
    mov byte [sf_state], ST_DONE
    ret
.go:
    xor bh, bh
    mov di, bx                      ; DI = the phase's result record
    mov cl, 3
    shl di, cl
    shl bx, 1
    shl bx, 1
    add di, bx                      ; x 12
    add di, sf_p
    mov si, bx
    shl si, 1
    shl si, 1                       ; x 16: the phase's parameters
    add si, sf_ptab
    mov [sf_pp], si
    mov [sf_rec], di
    mov al, [si+0]                  ; the format
    call sf_fmt
    mov [di+0], ax                  ; verb 11's answer
    or ax, ax
    jnz .skip                       ; refused: this card cannot, so no open
    call sf_build                   ; the 1KB chunk
    mov si, [sf_pp]
    test byte [si+1], PF_EXT
    jnz .ext
    mov cx, SF_RL
    mov ax, 0x0007
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .nogrant
    mov [sf_g1], si
    mov di, si                      ; stage the chunk SF_RL / SF_CHUNK times
    mov dx, SF_RL / SF_CHUNK
.stage:
    mov ax, 0x0006
    mov si, sf_chunk
    mov cx, SF_CHUNK
    call OSAPI_SND_STREAM
    add di, SF_CHUNK
    dec dx
    jnz .stage
    mov ax, (SND_OPENF_RING << 8) | 0
    mov si, [sf_g1]
    jmp short .open
.ext:                               ; AN EXTERNAL RING (SPEC.md 34.5.3): our
    mov ax, (SF_RL + 1023) / 1024 + 1   ; own claim, page-safe whole - the
    mov cx, ax                      ; ring and its control words after it
    call OSAPI_MEM_CLAIM_DMA_HI     ; DX = its segment
    jc .nogrant
    mov [sf_xseg], dx
    push es
    mov es, dx
    xor di, di
    mov dx, SF_RL / SF_CHUNK
    cld
.copy:
    mov si, sf_chunk
    mov cx, SF_CHUNK / 2
    rep movsw
    dec dx
    jnz .copy
    mov word [es:SF_RL + SND_EXT_TOTAL], SF_RL
    mov word [es:SF_RL + SND_EXT_CONS], 0
    pop es
    mov di, [sf_xseg]               ; DI:SI = the ring
    xor si, si
    mov ax, ((SND_OPENF_RING | SND_OPENF_EXT | (2 << SND_OPENF_RLSH)) << 8) | 0
%if (4096 << 2) != SF_RL
  %error "P3's size code 2 is a 16KB ring"
%endif
.open:
    mov cx, SF_RL
    mov dx, SF_RATE
    call OSAPI_SND_STREAM
    mov di, [sf_rec]
    mov [di+2], ax                  ; verb 0's answer
    or al, al
    jnz .refused
    mov [sf_hand], ah
    call OSAPI_GET_TICKS
    mov [di+8], ax                  ; t0
    mov byte [sf_wait], SF_PLAY
    mov byte [sf_state], ST_FEED
    ret
.refused:
    call sf_release
    jmp short .skip
.nogrant:
    mov di, [sf_rec]
    mov word [di+2], 0xEE00         ; no grant or claim at all
.skip:
    inc byte [sf_ph]
    mov byte [sf_wait], 1
    mov byte [sf_state], ST_GAP
    ret

; sf_release - the phase's ring back: the grant, or the external claim
sf_release:
    mov si, [sf_pp]
    test byte [si+1], PF_EXT
    jnz .ext
    mov si, [sf_g1]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    ret
.ext:
    mov dx, [sf_xseg]
    call OSAPI_MEM_FREE
    ret

; -----------------------------------------------------------------------------
; sf_feed - keep the ring full (its content is periodic in SF_CHUNK, so the
;           bytes already there ARE the next ones); at the end, the readings
;           and the close
; -----------------------------------------------------------------------------
sf_feed:
    mov si, [sf_pp]
    test byte [si+1], PF_STARVE
    jz .fed
    cmp byte [sf_wait], SF_PLAY - SF_STARVE
    ja .tick                        ; P4: left to underrun...
    jne .fed
    mov ah, [sf_hand]               ; ...and read the moment before its feed
    mov al, SND_V_PPOS
    call OSAPI_SND_STREAM
    mov [sf_ust], ax                ; must be 1: underrun-paused
    mov [sf_upl], dx
.fed:
    test byte [si+1], PF_EXT
    jz .verb1
    push es                         ; EXTERNAL: the feed is a memory write
    mov es, [sf_xseg]
    mov ax, [es:SF_RL + SND_EXT_CONS]
    add ax, SF_RL
    mov [es:SF_RL + SND_EXT_TOTAL], ax
    pop es
    jmp short .tick
.verb1:
    mov ah, [sf_hand]
    mov al, 3                       ; verb 3: DX = consumed (block-granular)
    call OSAPI_SND_STREAM
    jc .out
    add dx, SF_RL                   ; the lead as long as the ring allows
    mov cx, dx
    mov ah, [sf_hand]
    mov al, 1                       ; (and the resume, after an underrun)
    call OSAPI_SND_STREAM
.tick:
    dec byte [sf_wait]
    jnz .out
    mov di, [sf_rec]
    mov ah, [sf_hand]
    mov al, SND_V_PPOS              ; verb 9: the bytes played, exactly
    call OSAPI_SND_STREAM
    mov [di+6], dx
    mov [di+4], ax                  ; ...and the state
    call OSAPI_GET_TICKS
    mov [di+10], ax                 ; t1
    mov ah, [sf_hand]
    mov al, 2
    call OSAPI_SND_STREAM
    call sf_release
    inc byte [sf_ph]
    mov byte [sf_wait], SF_GAP
    mov byte [sf_state], ST_GAP
.out:
    ret

; -----------------------------------------------------------------------------
; sf_build - the phase's 1KB chunk from the 64-entry sine table
; in:  [sf_pp] -> the phase's parameters: db fmt, pad; dw left step, left
;      amplitude, right step, right amplitude (step = 64 / period)
; -----------------------------------------------------------------------------
sf_build:
    push es
    push ds
    pop es
    cld
    mov si, [sf_pp]
    mov di, sf_chunk
    xor bx, bx                      ; BX = the frame index
.frame:
    mov ax, [si+2]                  ; left
    mov cx, [si+4]
    call sf_sample
    call sf_put
    test byte [si+0], SND_FMT_STEREO
    jz .next
    mov ax, [si+6]                  ; right
    mov cx, [si+8]
    call sf_sample
    call sf_put
.next:
    inc bx
    cmp di, sf_chunk + SF_CHUNK
    jb .frame
    pop es
    ret

; sf_sample - AX = sin[(BX * AX) & 63] * CX >> 15, signed. Clobbers CX, DX.
sf_sample:
    push bx
    mul bx                          ; AX = frame x step (low bits are all
    and ax, 63                      ; that matter: the table is 64)
    shl ax, 1
    mov bx, ax
    mov ax, [sf_sin+bx]
    imul cx                         ; DX:AX = sin x amplitude
    shl ax, 1
    rcl dx, 1                       ; >> 15 is the high word of << 1
    mov ax, dx
    pop bx
    ret

; sf_put - store the sample AX at ES:DI as the phase's format says
sf_put:
    test byte [si+0], SND_FMT_16
    jz .b8
    stosw
    ret
.b8:
    add al, 128                     ; unsigned 8-bit
    stosb
    ret

; --- the phases ----------------------------------------------------------------
sf_ptab:                            ; 16 bytes each: fmt, pad, L step, L amp,
                                    ; R step, R amp, pad
    db SND_FMT_STEREO | SND_FMT_16, 0   ; P0: 344.5 Hz / 1,378 Hz at 22,050
    dw 1, 8000, 4, 600, 0, 0, 0
    db SND_FMT_STEREO, 0                ; P1: 689 Hz / 344.5 Hz, 8-bit
    dw 2, 40, 1, 100, 0, 0, 0
    db SND_FMT_16, 0                    ; P2: 1,378 Hz, mono 16-bit
    dw 4, 6000, 0, 0, 0, 0, 0
    db SND_FMT_STEREO | SND_FMT_16, PF_EXT      ; P3: 689 / 344.5 Hz,
    dw 2, 5000, 1, 12000, 0, 0, 0               ; an external ring
    db SND_FMT_STEREO | SND_FMT_16, PF_STARVE   ; P4: 1,378 / 689 Hz,
    dw 4, 3000, 2, 9000, 0, 0, 0                ; starved, then resumed

sf_sin:                             ; round(32767 sin(2 pi i / 64))
    dw 0, 3212, 6393, 9512, 12539, 15446, 18204, 20787
    dw 23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609
    dw 32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329
    dw 23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212
    dw 0, -3212, -6393, -9512, -12539, -15446, -18204, -20787
    dw -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609
    dw -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329
    dw -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212

; --- the window ------------------------------------------------------------------
sf_tpl:
    dw 340, 88, 190, 60
    dw sf_ttl, sf_paint, sf_nop, sf_nop
sf_ttl:     db 'Snd Format', 0
sf_line:    db 'step: -', 0

; --- THE RESULTS, read by tests/sndfmt.py through the magic ---------------------
sf_res:     db 'SFMTRES1'           ; +0
sf_caps:    dw 0                    ; +8   OSAPI_SND_CAPS
sf_done:    db 0                    ; +10  1 = the run finished
sf_state:   db 0                    ; +11  the timer's state
sf_r1:      dw 0                    ; +12  R1..R4: verb 11 / verb 0 answers
sf_r2:      dw 0                    ; +14
sf_r3:      dw 0                    ; +16
sf_r4:      dw 0                    ; +18
sf_r4off:   dw 0                    ; +20  R4's grant offset (must be odd)
sf_ust:     dw 0                    ; +22  P4's state while starved (1)...
sf_upl:     dw 0                    ; +24  ...and its bytes played then
sf_p:       times SF_NPH * 12 db 0  ; +26  per phase: v11 AX, v0 AX, state,
                                    ;      played bytes, t0, t1 (ticks)

; --- bss --------------------------------------------------------------------------
sf_ph    equ os88_image_end + 0     ; byte: the phase
sf_wait  equ os88_image_end + 1     ; byte: ticks left in a phase or a gap
sf_hand  equ os88_image_end + 2     ; byte: the stream handle
sf_g1    equ os88_image_end + 4     ; word: a grant
sf_g2    equ os88_image_end + 6     ; word: another
sf_pp    equ os88_image_end + 8     ; word: -> the phase's parameters
sf_rec   equ os88_image_end + 10    ; word: -> the phase's results
sf_xseg  equ os88_image_end + 12    ; word: P3's external ring's claim
sf_chunk equ os88_image_end + 14    ; 1KB: the chunk staged into the ring

    OS88_BSS 14 + SF_CHUNK
    OS88_IMAGE_END
