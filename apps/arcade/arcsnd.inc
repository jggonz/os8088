; =============================================================================
; os8088 - apps/arcade/arcsnd.inc - the boards' SOUND (docs/ARCADE-SPEC.md 8)
;
; One route a bracket: the STREAM (a Sound Blaster playing an external ring
; the board's mixer fills), FM (the OPL claimed whole, the board's voices on
; its channels) or the SPEAKER (one tone, as before). ar_snd_prep runs on
; the desktop before the bracket, ar_snd_open and ar_snd_close inside it at
; each end, ar_snd_frame once a frame. Space Invaders' halves are here too:
; its clips mixed (8.3) and its FM voices. Pac-Man's WSG is arcpm.inc's.
; =============================================================================

AR_SB_RL    equ 4096                ; the ring, then its two control words
AR_SB_KB    equ 5                   ;   ...in a claim this size, all page-safe
AR_SB_LEAD  equ 768                 ; bytes kept queued past the consumed
AR_SB_MOST  equ 1024                ; ...and mixed in one frame at most
AR_SB_OPEN  equ 2048                ; the silence the open stages: verb 0
                                    ;   takes no less than the FULL half
AR_SB_BLK   equ 256                 ; the card's block (BLKSH code 3)
AR_SB_ASK   equ 12                  ; frames the consumed count may stand still
                                    ;   before the driver is asked why

AR_RT_SPK   equ 0                   ; ar_sroute
AR_RT_FM    equ 1
AR_RT_SB    equ 2

; -----------------------------------------------------------------------------
; ar_snd_prep - on the desktop, before the bracket: when the stream route is
; possible here, whatever the board needs read for it (a lazy part - which
; may toast, so never inside the bracket). Sets ar_sbok
; -----------------------------------------------------------------------------
ar_snd_prep:
    mov byte [ar_sbok], 0
    mov byte [ar_swhy], 1
    call OSAPI_SND_CAPS
    or bl, bl                       ; the Control Panel's speaker route
    jz .out
    mov byte [ar_swhy], 2
    and ax, SND_CAP_PCM_BG | SND_CAP_EXTBLK
    cmp ax, SND_CAP_PCM_BG | SND_CAP_EXTBLK
    jne .out
    mov bx, [ar_bd]
    cmp word [bx+BD_MIX], 0
    je .out
    mov byte [ar_swhy], 3
    mov ax, [bx+BD_SBHZ]            ; the rate, which the preparation may
    mov [ar_sbhz], ax               ;   lower for a slow machine
    mov ax, [bx+BD_SBPREP]
    or ax, ax
    jz .ok
    push es
    call ax
    pop es
    jc .out
.ok:
    mov byte [ar_sbok], 1
    mov byte [ar_swhy], 0
.out:
    ret

; -----------------------------------------------------------------------------
; ar_snd_open - in the bracket: the best route the board offers and the
; machine has (8). A route that fails falls to the next
; -----------------------------------------------------------------------------
ar_snd_open:
    push es
    mov byte [ar_sroute], AR_RT_SPK
    mov word [ar_hz], 0
    call OSAPI_SND_CAPS
    or bl, bl
    jz .out
    mov [ar_scaps], ax
    cmp byte [ar_sbok], 0       ; (ar_swhy says why not, from ar_snd_prep)
    je .fm
    call ar_sb_open
    jnc .out
.fm:
    mov bx, [ar_bd]
    cmp word [bx+BD_FMPAT], 0
    je .out
    test word [ar_scaps], SND_CAP_FM
    jz .out
    call ar_fm_open
.out:
    mov es, [ar_vseg]               ; told, for a test to read (2.1)
    mov al, [ar_sroute]
    mov ah, [ar_swhy]
    mov [es:V_SROUTE], ax
    pop es
    ret

; ar_snd_close - every way out of the bracket: the route given back
ar_snd_close:
    push es
    mov al, [ar_sroute]
    mov byte [ar_sroute], AR_RT_SPK
    cmp al, AR_RT_SB
    jne .fm
    call ar_sb_close
    jmp short .out
.fm:
    cmp al, AR_RT_FM
    jne .spk
    mov al, 3                       ; the chip back: our channels off, its
    call OSAPI_SND_FM               ;   default patches, the claim
    jmp short .out
.spk:
    call ar_quiet
.out:
    pop es
    ret

; ar_snd_frame - once a frame, after the picture
ar_snd_frame:
    mov bx, [ar_bd]
    mov al, [ar_sroute]
    cmp al, AR_RT_SB
    je ar_sb_frame
    cmp al, AR_RT_FM
    jne .spk
    call [bx+BD_FMF]
    jmp ar_fm_flush
.spk:
    jmp [bx+BD_SPK]

; =============================================================================
; the stream (8.1)
; =============================================================================
; ar_sb_open - the ring claimed, silence staged, verb 0. CF=1 refused, and
; nothing kept
ar_sb_open:
    mov byte [ar_swhy], 4
    mov ax, AR_SB_KB
    mov cx, AR_SB_KB
    MEMKB_DMA_HI                    ; born PINNED, and never declared: it
    jc .no                          ;   lives as long as the bracket
    mov [ar_sbseg], dx
    mov es, dx
    xor di, di
    mov ax, 0x8080
    mov cx, AR_SB_RL / 2
    cld
    rep stosw
    mov word [es:AR_SB_RL+SND_EXT_TOTAL], AR_SB_OPEN
    mov word [es:AR_SB_RL+SND_EXT_CONS], 0
    mov word [ar_sbtot], AR_SB_OPEN
    mov dx, [ar_sbhz]
    mov di, [ar_sbseg]
    xor si, si
    mov cx, AR_SB_OPEN
    mov byte [ar_swhy], 5
    mov ah, SND_OPENF_RING + SND_OPENF_EXT + (3 << SND_OPENF_BLKSH)
    xor al, al                      ; (RLSH 0: 4KB)
    call OSAPI_SND_STREAM
    jc .free
    mov [ar_sbhand], ah
    mov byte [ar_sroute], AR_RT_SB
    mov byte [ar_swhy], 0
    clc
    ret
.free:
    mov dx, [ar_sbseg]
    call OSAPI_MEM_FREE
    mov word [ar_sbseg], 0
.no:
    stc
    ret

; ar_sb_close - verb 2, then the claim
ar_sb_close:
    push es
    mov es, [ar_vseg]
    mov word [es:V_SBSEG], 0
    pop es
    mov al, 2
    mov ah, [ar_sbhand]
    call OSAPI_SND_STREAM
    mov dx, [ar_sbseg]
    call OSAPI_MEM_FREE
    mov word [ar_sbseg], 0
    ret

; ar_sb_frame - the board's state for the frame, then the ring topped up to
; the lead past what the card has consumed, the new total written last; and
; an underrun resumed. BX = the board
ar_sb_frame:
    call [bx+BD_SBTICK]
    mov es, [ar_sbseg]
    mov cx, AR_SB_LEAD
    mov ax, [ar_sbtot]
    sub ax, [es:AR_SB_RL+SND_EXT_CONS]
    sub cx, ax                      ; CX = what is missing (none when the
    jbe .state                      ;   lead is there; a total behind the
    cmp cx, AR_SB_MOST              ;   count wraps big and mixes nothing)
    jbe .mix
    mov cx, AR_SB_MOST
.mix:
    mov di, [ar_sbtot]
    and di, AR_SB_RL - 1
    mov dx, AR_SB_RL
    sub dx, di                      ; DX = room to the ring's end
    cmp cx, dx
    jbe .last
    push cx
    mov cx, dx
    call ar_sb_mix
    pop cx
    sub cx, dx
    xor di, di
.last:
    call ar_sb_mix
    mov ax, [ar_sbtot]              ; the total last: the card reads it at a
    mov [es:AR_SB_RL+SND_EXT_TOTAL], ax ;   block's end, and finds it whole
.state:
    push es                         ; told, for a test to read (2.1)
    mov ax, [ar_sbtot]
    mov dx, [ar_sbseg]
    mov es, [ar_vseg]
    mov [es:V_SBTOT], ax
    mov [es:V_SBSEG], dx
    pop es
    mov ax, [es:AR_SB_RL+SND_EXT_CONS]  ; the driver asked only once the count
    cmp ax, [ar_sbcons]             ;   has stood still AR_SB_ASK frames: a
    mov [ar_sbcons], ax             ;   block is 256 bytes, a few frames of
    je .still                       ;   sound, so a stall is the card's
    mov byte [ar_sbstill], 0
    ret
.still:
    inc byte [ar_sbstill]
    cmp byte [ar_sbstill], AR_SB_ASK
    jae .ask
    ret
.ask:
    mov byte [ar_sbstill], 0
    mov al, 3
    mov ah, [ar_sbhand]
    call OSAPI_SND_STREAM           ; AX = the state, DX = consumed
    jc .gone
    cmp ax, SND_ST_UNDER
    je .under
    cmp ax, SND_ST_PLAYING
    je .out
.gone:
    call ar_sb_close                ; the watchdog gave up: the speaker, for
    mov byte [ar_sroute], AR_RT_SPK ;   the rest of the bracket
.out:
    ret
.under:
    mov cx, [ar_sbtot]              ; the Video Player's vp_skeep: resumed
    mov ax, cx                      ;   once a block is queued
    sub ax, dx
    cmp ax, AR_SB_BLK
    jb .out
    mov al, 1
    mov ah, [ar_sbhand]
    call OSAPI_SND_STREAM
    ret

; ar_sb_mix - ES:DI = the ring at the total, CX = samples: the board's
; mixer WRITES them, and the total moves on. Kept: CX, DX, ES
ar_sb_mix:
    push cx
    push dx
    push cx
    cld
    mov bx, [ar_bd]
    push bp
    call [bx+BD_MIX]
    pop bp
    pop cx
    add [ar_sbtot], cx
    pop dx
    pop cx
    ret

; =============================================================================
; FM (8.2) - RealDMG's rw_fmput, for a channel a Hz and a volume
; =============================================================================
; ar_fm_open - the chip claimed whole and the board's patches written; the
; shadows unknown, so a frame writes what it means
ar_fm_open:
    mov al, [ar_swhy]               ; (kept: why the stream declined)
    push ax
    mov byte [ar_swhy], 7
    mov bh, DRVC_SOUND              ; ASK IT FIRST (SPEC.md 20.11.1)
    mov bl, SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .no
    cmp ax, SND_SIG
    jne .no
    mov byte [ar_swhy], 8
    mov al, SND_FM_CLAIM
    xor cl, cl                      ; OPL2 mode
    call OSAPI_SND_FM
    jc .no
    pop ax
    mov [ar_swhy], al
    mov byte [ar_sroute], AR_RT_FM
    push ds
    pop es
    mov di, ar_fmsh
    mov cx, AR_FMCH * 3
    mov al, 0xFF
    cld
    rep stosb
    mov bx, [ar_bd]
    mov si, [bx+BD_FMPAT]
    lodsw
    mov cx, ax
    jmp ar_fm_send
.no:
    pop ax                          ; (the FM step's reason stands)
    ret

; ar_fm_flush - the frame's batch to the chip
ar_fm_flush:
    mov cx, [ar_fmn]
    mov word [ar_fmn], 0
    mov si, ar_fmbuf
; ar_fm_send - DS:SI = CX register/value pairs
ar_fm_send:
    jcxz .r
    push es
    push ds
    pop es
    mov bh, DRVC_SOUND
    mov bl, SNDV_OPLW
    call OSAPI_DRV_CALL
    pop es
.r:
    ret

; ar_fmset - BX = the channel (0..AR_FMCH-1), AX = Hz (0: keyed off), CL =
; the volume 0-15 (0: keyed off), CH = 1 to strike the note again (a key
; off and on in one batch). Onto the frame's batch, each register only if
; it changes. Kept: BX, BP
ar_fmset:
    or cl, cl
    jz .off
    or ax, ax
    jz .off
    call ar_fmfreq                  ; AX = B0h:A0h
    jc .off
    or ah, 0x20                     ; the key
    jmp short .put
.off:
    xor cx, cx
.put:
    push dx
    mov si, bx                      ; SI = the shadows: B0h, A0h, carrier TL
    shl si, 1
    add si, bx
    add si, ar_fmsh
    mov dl, [ar_fmcar+bx]           ; DL = the carrier's slot
    test ah, 0x20
    jnz .on
    mov ah, [si]                    ; off: the last B0h, its key clear
    cmp ah, 0xFF
    je .r
    and ah, 0x1F
    mov al, [si+1]
    jmp short .b0
.on:
    or ch, ch                       ; struck again: the key off first
    jz .a0
    test byte [si], 0x20
    jz .a0
    cmp byte [si], 0xFF
    je .a0
    push ax
    mov ah, [si]
    and ah, 0x1F
    mov [si], ah
    mov al, 0xB0
    add al, bl
    call ar_fmpair
    pop ax
.a0:
    cmp al, [si+1]                  ; A0h: the F-number's low 8
    je .tl
    mov [si+1], al
    push ax
    mov ah, al
    mov al, 0xA0
    add al, bl
    call ar_fmpair
    pop ax
.tl:
    push bx
    mov bl, cl                      ; the level before the key, so a note
    mov bh, 0                       ;   keyed on starts at it
    mov cl, [ar_fmtl+bx]
    pop bx
    cmp cl, [si+2]
    je .b0
    mov [si+2], cl
    push ax
    mov al, dl
    add al, 0x40
    mov ah, cl
    call ar_fmpair
    pop ax
.b0:
    cmp ah, [si]                    ; B0h: key, block, the F-number's top 2
    je .r
    mov [si], ah
    mov al, 0xB0
    add al, bl
    call ar_fmpair
.r:
    pop dx
    ret

; ar_fmpair - AL = register, AH = value onto the batch
ar_fmpair:
    push di
    mov di, [ar_fmn]
    cmp di, AR_FMMAX
    jae .r
    inc word [ar_fmn]
    shl di, 1
    mov [ar_fmbuf+di], ax
.r:
    pop di
    ret

; ar_fmfreq - AX = Hz: AX = the OPL's block (bits 10-12) and F-number,
; f = fnum x 49,716 / 2^(20 - block). CF = 1 past what block 7 says.
; Kept: all but AX
ar_fmfreq:
    push cx
    push dx
    mov cx, 5400                    ; 2^20 / 49,716 = 21.09 = 5,400 / 256
    mul cx
    mov al, ah                      ; DX:AX = Hz x 21.09, the F-number at
    mov ah, dl                      ;   block 0
    mov dl, dh
    xor dh, dh
    xor cl, cl
.n:
    or dx, dx
    jnz .sh
    cmp ax, 1024
    jb .b
.sh:
    shr dx, 1
    rcr ax, 1
    inc cl
    jmp short .n
.b:
    cmp cl, 7
    ja .hi
    shl cl, 1
    shl cl, 1
    or ah, cl
    clc
    jmp short .r
.hi:
    stc
.r:
    pop dx
    pop cx
    ret

AR_FMCH     equ 4                   ; the channels a board plays
AR_FMMAX    equ 32                  ; pairs a frame's batch holds
; the carrier's operator slot, a channel (its modulator three below)
ar_fmcar:   db 3, 4, 5, 11
; a volume 0-15 (an amplitude, v/15) as the OPL's attenuation, 0.75 dB
; steps: 20 log10(v/15) / 0.75 - RealDMG's rw_fmtl
ar_fmtl:    db 63, 31, 23, 19, 15, 13, 11, 9, 7, 6, 5, 4, 3, 2, 1, 0

; a patch: channel, its modulator's slot, mod 20h, car 20h, mod TL, attack/
; decay mod, car, sustain/release mod, car, then C0h's value - 11 pairs
%macro AR_FMPATCH 10
    db 0x20 + %2, %3,  0x23 + %2, %4
    db 0x40 + %2, %5,  0x43 + %2, 63
    db 0x60 + %2, %6,  0x63 + %2, %7
    db 0x80 + %2, %8,  0x83 + %2, %9
    db 0xE0 + %2, 0,   0xE3 + %2, 0
    db 0xC0 + %1, %10
%endmacro

; =============================================================================
; Space Invaders (8.3)
; =============================================================================
INV_SBHZ    equ 8000                ; arcsfx.py's rate
INV_NV      equ 3                   ; clips sounding at once: three at +-42
                                    ;   cannot wrap a byte
INV_CUFO    equ 0                   ; arcsfx.py's clip order
INV_V_POS   equ 0                   ; a voice: where it is, where it ends,
INV_V_END   equ 2                   ;   where a loop starts again (0: none),
INV_V_LOOP  equ 4                   ;   its clip + 1 (0: silent)
INV_V_CLIP  equ 6
INV_V_SIZE  equ 8

; inv_sbprep - the clips' part read, once; and the rate: every other
; sample, 4,000 Hz, on an 8086-class machine (8.3)
inv_sbprep:
    call OSAPI_CPU_INFO
    mov word [ar_sbhz], INV_SBHZ
    mov byte [inv_mstep], 1
    cmp al, CPU_8086
    jne .fetch
    mov word [ar_sbhz], INV_SBHZ / 2
    mov byte [inv_mstep], 2
.fetch:
    mov al, AR_P_INVSFX
    call op_fetch                   ; idempotent; CF=1 has toasted
    jc .r
    mov al, AR_P_INVSFX             ; MOVABLE (op_lazymv): every use asks
    call op_movable                 ;   op_seg again, the mix included
    mov al, AR_P_INVSFX
    call op_seg
    push es
    mov es, ax
    cmp word [es:0], 'AS'           ; the part is ours, but a header that
    pop es                          ;   does not say so is not played
    je .r
    stc
.r:
    ret

; inv_sbtick - a frame: each rising bit starts its clip from the top, the
; saucer loops while its bit is held, the amplifier silences everything
inv_sbtick:
    call ar_sbits                   ; AX: the board's sound bits (X_SDEC)
    mov bx, [ar_sbold]
    mov [ar_sbold], ax
    not bx
    and bx, ax                      ; BX = the bits that rose
    test al, 0x20
    jnz .amp
    mov di, inv_v                   ; the amplifier off: every voice stops
    mov cx, INV_NV
.stop:
    mov byte [di+INV_V_CLIP], 0
    add di, INV_V_SIZE
    loop .stop
    ret
.amp:
    mov si, [ar_bx]
    test bx, [si+X_CMASK]           ; nothing rose but perhaps the held
    jz .ufo1                        ;   sound: the cues not walked
    push ax
    mov si, [si+X_CBITS]
.c:
    lodsw                           ; AX = the bit, then the clip
    or ax, ax
    jz .ufo
    mov dx, ax
    lodsb
    test bx, dx
    jz .c
    push si
    push bx
    call inv_start
    pop bx
    pop si
    jmp .c
.ufo:
    pop ax
.ufo1:
    mov si, [ar_bx]                 ; the held sound (the saucer, the rescue
    mov dx, [si+X_LOOP]             ;   ship): its clip loops while its bit
    mov cl, [si+X_LCLIP]            ;   is up
    mov ch, cl
    inc ch                          ; CH = clip + 1, as a voice holds it
    mov di, inv_v
    mov bx, INV_NV
.f:
    cmp [di+INV_V_CLIP], ch
    je .have
    add di, INV_V_SIZE
    dec bx
    jnz .f
    test ax, dx
    jz .r
    mov al, cl                      ; held and silent: start it, looping
    call inv_start
    mov ax, [di+INV_V_POS]
    mov [di+INV_V_LOOP], ax
.r:
    ret
.have:
    test ax, dx
    jnz .r
    mov byte [di+INV_V_CLIP], 0     ; let go: it stops
    ret

; inv_start - AL = a clip: a voice starts it from the top - the voice already
; playing it, or a silent one, or the one nearest its end. Out DI = the voice
inv_start:
    mov ah, al
    inc ah                          ; AH = clip + 1
    mov di, inv_v
    mov cx, INV_NV
.same:
    cmp [di+INV_V_CLIP], ah
    je .go
    add di, INV_V_SIZE
    loop .same
    mov di, inv_v
    mov cx, INV_NV
.free:
    cmp byte [di+INV_V_CLIP], 0
    je .go
    add di, INV_V_SIZE
    loop .free
    mov di, inv_v                   ; all busy: the one with least to go
    mov si, di
    mov dx, 0xFFFF
    mov cx, INV_NV
.least:
    mov bx, [si+INV_V_END]
    sub bx, [si+INV_V_POS]
    cmp bx, dx
    jae .nl
    mov dx, bx
    mov di, si
.nl:
    add si, INV_V_SIZE
    loop .least
.go:
    mov [di+INV_V_CLIP], ah
    mov word [di+INV_V_LOOP], 0
    push es
    push ax
    mov al, AR_P_INVSFX
    call op_seg
    mov es, ax
    pop ax
    mov bl, al
    mov bh, 0
    shl bx, 1
    shl bx, 1
    mov si, [es:bx+4]               ; the clip's offset and length
    mov [di+INV_V_POS], si
    add si, [es:bx+6]
    mov [di+INV_V_END], si
    pop es
    ret

; inv_mix - WRITE CX samples at ES:DI: the sounding clips summed in one
; pass. A chunk runs until the first voice ends, through the kernel for that
; many voices and that step - SI, BX and BP are the clips, nothing is read
; back from the ring - and a voice that ended loops or falls silent (8.3)
inv_mix:
    mov [inv_mn], cx
    mov [inv_md], di
    mov al, AR_P_INVSFX
    call op_seg
    mov [inv_mseg], ax
.chunk:
    mov cx, [inv_mn]
    or cx, cx
    jnz .some
    ret
.some:
    xor dx, dx                      ; DX = the voices sounding, x 2
    mov bx, inv_v
.g:
    cmp byte [bx+INV_V_CLIP], 0
    je .gn
    mov ax, [bx+INV_V_END]
    sub ax, [bx+INV_V_POS]          ; AX = the bytes it has left...
    cmp byte [inv_mstep], 1
    je .r1
    inc ax                          ; ...as samples at this step
    shr ax, 1
.r1:
    cmp ax, cx
    jae .gk
    mov cx, ax                      ; CX = the chunk: to the first end
.gk:
    mov si, dx
    mov [inv_act+si], bx
    inc dx
    inc dx
.gn:
    add bx, INV_V_SIZE
    cmp bx, inv_v + INV_NV * INV_V_SIZE
    jb .g
    sub [inv_mn], cx
    mov si, dx                      ; the kernel: voices x 2 + the step's
    add si, si
    cmp byte [inv_mstep], 1
    je .k1
    add si, 2
.k1:
    mov ax, [inv_kern+si]
    push dx
    mov si, [inv_act]               ; the clips' places
    mov si, [si+INV_V_POS]
    mov bx, [inv_act+2]
    mov bx, [bx+INV_V_POS]
    mov bp, [inv_act+4]
    mov bp, [ds:bp+INV_V_POS]
    mov di, [inv_md]
    push ds
    mov ds, [inv_mseg]
    call ax
    pop ds
    mov [inv_md], di
    pop dx
    mov ax, bx                      ; where they got to - the slots in use
    or dx, dx                       ;   only: a slot past them is stale and
    jz .ends                        ;   may name a voice sounding in another
    mov bx, [inv_act]
    mov [bx+INV_V_POS], si
    cmp dx, 4
    jb .ends
    mov bx, [inv_act+2]
    mov [bx+INV_V_POS], ax
    cmp dx, 6
    jb .ends
    mov bx, [inv_act+4]
    mov [bx+INV_V_POS], bp
.ends:
    xor si, si                      ; an end: round again, or silent
.e:
    cmp si, dx
    jae .chunk
    mov bx, [inv_act+si]
    mov ax, [bx+INV_V_POS]
    cmp ax, [bx+INV_V_END]
    jb .en
    mov ax, [bx+INV_V_LOOP]
    or ax, ax
    jz .stop
    mov [bx+INV_V_POS], ax
    jmp short .en
.stop:
    mov byte [bx+INV_V_CLIP], 0
.en:
    inc si
    inc si
    jmp .e

; the kernels: DS:SI, DS:BX, DS:BP the clips (as many as the kernel sums),
; ES:DI out, CX samples. A signed sum + 80h is the sum XOR 80h
inv_kern:   dw inv_k0, inv_k0, inv_k11, inv_k12, inv_k21, inv_k22, inv_k31, inv_k32
inv_k0:
    mov al, 0x80
    rep stosb
    ret
inv_k11:
    lodsb
    xor al, 0x80
    stosb
    loop inv_k11
    ret
inv_k12:
    lodsw
    xor al, 0x80
    stosb
    loop inv_k12
    ret
inv_k21:
    lodsb
    add al, [bx]
    inc bx
    xor al, 0x80
    stosb
    loop inv_k21
    ret
inv_k22:
    lodsw
    add al, [bx]
    inc bx
    inc bx
    xor al, 0x80
    stosb
    loop inv_k22
    ret
inv_k31:
    lodsb
    add al, [bx]
    add al, [ds:bp]
    inc bx
    inc bp
    xor al, 0x80
    stosb
    loop inv_k31
    ret
inv_k32:
    lodsw
    add al, [bx]
    add al, [ds:bp]
    inc bx
    inc bx
    inc bp
    inc bp
    xor al, 0x80
    stosb
    loop inv_k32
    ret

inv_cbits:                          ; OUT 3 low, OUT 5 high: the bit, the clip
    dw 0x0002
    db 1                            ; a shot
    dw 0x0004
    db 2                            ; the base hit
    dw 0x0008
    db 3                            ; an invader hit
    dw 0x0010
    db 4                            ; an extra base
    dw 0x0100
    db 5                            ; the fleet's four steps
    dw 0x0200
    db 6
    dw 0x0400
    db 7
    dw 0x0800
    db 8
    dw 0x1000
    db 9                            ; the saucer hit
    dw 0

; --- Space Invaders on FM ----------------------------------------------------
; inv_fmframe - channel 0 the fleet, 1 the saucer, 2 the noises, 3 the extra
; base and the saucer hit (8.3)
inv_fmframe:
    call ar_sbits                   ; AX: the board's sound bits (X_SDEC)
    mov dx, [ar_sbold]
    mov [ar_sbold], ax
    not dx
    and dx, ax                      ; DX = the bits that rose
    mov [inv_frise], dx
    test al, 0x20
    jnz .amp
    xor bx, bx                      ; the amplifier off: all four off
.off:
    xor ax, ax
    xor cx, cx
    call ar_fmset
    inc bx
    cmp bx, 4
    jb .off
    mov byte [inv_fnz], 0
    mov word [inv_fseq], 0
    ret
.amp:
    mov [inv_fheld], ax
    ; channel 0: a step's note, struck on its rise; the patch decays it
    mov si, [ar_bx]
    mov si, [si+X_FSTEP]
.step:
    lodsw                           ; the bit
    or ax, ax
    jz .ufo
    mov dx, ax
    lodsw                           ; Hz
    test [inv_frise], dx
    jz .step
    xor bx, bx
    mov cx, 0x010F                  ; full, struck again
    call ar_fmset
.ufo:
    ; channel 1: the saucer, swept 600-1,000 Hz and back each 16 frames
    xor ax, ax
    xor cx, cx
    mov si, [ar_bx]
    mov dx, [si+X_LOOP]
    test [inv_fheld], dx
    jz .usnd
    inc byte [inv_fwarb]
    mov al, [inv_fwarb]
    and al, 15
    cmp al, 8
    jb .up
    xor al, 15                      ; 8..15 back down 7..0
.up:
    mov ah, 50
    mul ah
    add ax, 600
    mov cl, 10
.usnd:
    mov bx, 1
    call ar_fmset
    ; channel 2: the noises - the loudest new one starts its level
    mov si, [ar_bx]
    mov si, [si+X_FNOISE]
.nz:
    lodsw                           ; the bit
    or ax, ax
    jz .nzplay
    mov dx, ax
    lodsw                           ; Hz
    mov cx, ax
    lodsw                           ; AL = the level, AH = frames a step
    test [inv_frise], dx
    jz .nz
    cmp al, [inv_fnz]
    jb .nz
    mov [inv_fnz], al
    mov [inv_fnzr], ah
    mov [inv_fnzc], ah
    mov [inv_fnzhz], cx
    mov byte [inv_fnzs], 1
    jmp .nz
.nzplay:
    mov ax, [inv_fnzhz]
    mov cl, [inv_fnz]
    mov ch, [inv_fnzs]
    mov byte [inv_fnzs], 0
    mov bx, 2
    call ar_fmset
    cmp byte [inv_fnz], 0           ; the level falls a step each AH frames
    je .tones
    dec byte [inv_fnzc]
    jnz .tones
    mov al, [inv_fnzr]
    mov [inv_fnzc], al
    dec byte [inv_fnz]
.tones:
    ; channel 3: note sequences - the extra base and the saucer hit on
    ; Space Invaders - the speaker's own tables
    mov si, [ar_bx]
    mov si, [si+X_FTONE]
.tn:
    lodsw
    or ax, ax
    jz .seq
    mov dx, ax
    lodsw
    test [inv_frise], dx
    jz .tn
    mov [inv_fseq], ax
    mov byte [inv_fwait], 0
    jmp .tn
.seq:
    mov si, [inv_fseq]
    or si, si
    jz .t0
    cmp byte [inv_fwait], 0
    je .note
    dec byte [inv_fwait]
    ret
.note:
    mov ax, [si]
    or ax, ax
    jz .tend
    mov cl, [si+2]
    mov [inv_fwait], cl
    add si, 3
    mov [inv_fseq], si
    mov cx, 0x010C
    mov bx, 3
    jmp ar_fmset
.tend:
    mov word [inv_fseq], 0
.t0:
    xor ax, ax
    xor cx, cx
    mov bx, 3
    jmp ar_fmset

inv_fsteps:                         ; the fleet's four steps, the speaker's
    dw 0x0100, 110, 0x0200, 98, 0x0400, 87, 0x0800, 82, 0
inv_ftones:
    dw 0x1000, ar_n_ufohit, 0x0010, ar_n_extra, 0

; Lunar Rescue on FM and on the stream (MAME 8080bw_a.cpp's lrescue_sh_*)
lr_fsteps:                          ; the footsteps, high and low; the count
    dw 0x0100, 660, 0x0200, 440, 0x0400, 1500, 0
lr_fnoise:                          ; the bit, Hz, the level, frames a step
    dw 0x0004, 300                  ; the death
    db 15, 5
    dw 0x0008, 900                  ; an alien hit
    db 15, 1
    dw 0x0002, 1600                 ; a shot
    db 12, 1
    dw 0x0001, 180                  ; the thrust
    db 10, 2
    dw 0
lr_ftones:
    dw 0x0010, ar_n_extra, 0
lr_cbits:                           ; the bit, the clip (arcsfx.py's order)
    dw 0x0001
    db 10                           ; the thrust
    dw 0x0002
    db 1                            ; a shot
    dw 0x0004
    db 2                            ; the death
    dw 0x0008
    db 3                            ; an alien hit
    dw 0x0010
    db 4                            ; the bonus ship
    dw 0x0100
    db 11                           ; a footstep, high
    dw 0x0200
    db 12                           ; ...and low
    dw 0x0400
    db 13                           ; the count of the men saved
    dw 0
inv_fnoise:                         ; the bit, Hz, the level, frames a step
    dw 0x0004, 300                  ; the base hit: long
    db 15, 5
    dw 0x0008, 900                  ; an invader hit
    db 15, 1
    dw 0x0002, 1600                 ; a shot
    db 12, 1
    dw 0

inv_fmpat:
    dw (inv_fmpate - inv_fmpat - 2) / 2
    ; channel 0 (slots 0, 3): the fleet - a struck bass, decaying (EG type 0)
    AR_FMPATCH 0, 0, 0x01, 0x01, 18, 0xF4, 0xF5, 0xF6, 0xF6, 0x0A
    ; channel 1 (slots 1, 4): the saucer - near a sine, held
    AR_FMPATCH 1, 1, 0x21, 0x21, 40, 0xF0, 0xF0, 0x0F, 0x0F, 0x00
    ; channel 2 (slots 2, 5): the noises - RealDMG's: a modulator at x15 and
    ; full feedback, held, its level stepped down by the frame
    AR_FMPATCH 2, 2, 0x2F, 0x21, 0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0E
    ; channel 3 (slots 8, 11): the tones - a square's odd harmonics
    AR_FMPATCH 3, 8, 0x22, 0x21, 32, 0xF0, 0xF0, 0x0F, 0x0F, 0x00
inv_fmpate:



