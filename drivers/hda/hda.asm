; =============================================================================
; os8088 - Intel High Definition Audio output driver
;
; Initial hardware target: ASUS Eee PC 1015PN, whose analogue audio function
; is the Intel NM10/ICH controller at 00:1b.0 and a Realtek ALC269 codec.
; The implementation is original code from Intel's HDA register/verb spec.
; It exposes os8088's native unsigned 8-bit mono stream ABI and converts it to
; a continuously-running 44.1-kHz signed 16-bit stereo HDA ring.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Intel HDA', DRVC_SOUND, hda_entry

HDA_DMA_KB     equ 37           ; 36 of BDL + ring, plus the paragraphs it
                                ; takes to put both on a 128-byte boundary
HDA_STAGE_KB   equ 32
HDA_BDL_OFF    equ 0x0000
HDA_POS_OFF    equ 0x0080       ; the controller's DMA position buffer: 8
                                ; streams x 8 bytes, 128-aligned, written by
                                ; the DEVICE (SPEC.md 34.11.1's memory proof)
HDA_RING_OFF   equ 0x0400
HDA_PERIOD     equ 8192
HDA_FRAMES     equ HDA_PERIOD / 4
HDA_PERIODS   equ 4
HDA_RING_BYTES equ HDA_PERIOD * HDA_PERIODS
HDA_RATE       equ 44100
HDA_FMT        equ 0x4011       ; 44.1 kHz, 16-bit, stereo
HDA_POLL       equ 512          ; MMIO polls per bounded hardware wait
HDA_MAXPINS    equ 8            ; output pins hda_pin_scan keeps
HDA_SETTLE     equ 2048         ; legacy-port delay, safely over 521 us

; controller registers
HDA_GCAP       equ 0x00
HDA_GCTL       equ 0x08
HDA_STATESTS   equ 0x0e
HDA_ICOI       equ 0x60
HDA_ICII       equ 0x64
HDA_ICIS       equ 0x68
HDA_DPLBASE    equ 0x70
HDA_DPUBASE    equ 0x74
HDA_SD_BASE    equ 0x80
HDA_SD_CTL     equ 0x00
HDA_SD_LPIB    equ 0x04
HDA_SD_CBL     equ 0x08
HDA_SD_LVI     equ 0x0c
HDA_SD_FMT     equ 0x12
HDA_SD_BDLPL   equ 0x18
HDA_SD_BDLPU   equ 0x1c

hda_entry:
    cmp al, DRVV_ATTACH
    je hda_attach
    cmp al, DRVV_DETACH
    je hda_detach
    cmp al, DRVV_READY
    je .ready
    cmp al, DRVV_TIER
    je .tier
    stc
    ret
.ready:
    clc
    ret
.tier:
    cmp ah, SND_RT_PCM
    jb .off
    mov word [hda_services+DSV_CAPS], SND_CAP_PCM_BG
    mov word [hda_services+DSV_STREAM], hda_stream
    mov si, hda_services
    clc
    ret
.off:                          ; speaker route: keep hardware attached but
    call hda_stream_stop        ; withdraw digital playback from applications
    xor ax, ax
    call hda_tone
    mov word [hda_services+DSV_CAPS], 0
    mov word [hda_services+DSV_STREAM], 0
    mov si, hda_services
    clc
    ret

; Probe 00:1b.0, take a pinned conventional-memory ring, reset the controller,
; and configure the known ALC269 analogue path.  No stream runs until open.
; BX IS THE DRIVER-TABLE ROW and the kernel reads it back after this far
; call: drv_attach saves AX, SI, BP and ES around the verb and then does
; `mov es, [bx+DRVR_SEG]` / drv_publish with whatever BX came back. The codec
; verb walk below leaves BX at the end of hda_codec_verbs, so the first build
; published nothing - the row read 'Loaded' while the Sound page greyed
; Digital Audio (drv_owner 0, service table all zero). Outputs are CF, SI on
; success and AL on refusal; everything else goes back as it came, which is
; what SOUND.DRV's sbl_attach does.
hda_attach:
    push bx
    push cx
    push dx
    push di
    cmp byte [hda_up], 0
    je .probe
    mov al, DRVE_TWICE
    jmp short .fail
.probe:
    call hda_pci_probe
    jc .nohw
    mov ax, HDA_DMA_KB
    call OSAPI_MEM_CLAIM_HI
    jc .nomem
    mov [hda_dmaraw], dx        ; what MEM_FREE gets back...
    add dx, 7                   ; ...and the 128-byte-aligned segment the
    and dx, 0xfff8              ; controller is given: BDLPL's low seven bits
    mov [hda_dmaseg], dx        ; are reserved and every buffer must start on
                                ; that boundary too (HDA spec 3.3.38, 3.6.3).
                                ; A paragraph-aligned claim is right one time
                                ; in eight, and QEMU does not mind either way
    call hda_bdl_init
    call hda_ctl_reset
    jc .undo
    call hda_posbuf_on
    call hda_codec_init
    jc .undo
    mov byte [hda_up], 1
    mov si, hda_services
    clc
    jmp short .out
.undo:
    call hda_posbuf_off
    mov dx, [hda_dmaraw]
    call OSAPI_MEM_FREE
    mov word [hda_dmaseg], 0
    mov word [hda_dmaraw], 0
    call hda_pci_restore
.nohw:
    mov al, DRVE_HW
    jmp short .fail
.nomem:
    call hda_pci_restore
    mov al, DRVE_MEM
.fail:
    stc
.out:                          ; pop writes no flag: CF is the answer
    pop di
    pop dx
    pop cx
    pop bx
    ret

hda_detach:
    push ax
    push bx
    push cx
    push dx
    push si                     ; hda_hw_stop and hda_pci_restore spend SI/DX
    mov byte [hda_up], 0
    call hda_stream_stop
    call hda_hw_stop
    mov byte [hda_tone_on], 0
    mov cx, 40
.wait:
    cmp byte [hda_wtask], 0
    je .free
    call OSAPI_TASK_YIELD
    loop .wait
.free:
    call hda_grant_drop
    call hda_posbuf_off         ; the device writes there until told not to
    mov dx, [hda_dmaraw]
    or dx, dx
    jz .pci
    call OSAPI_MEM_FREE
    mov word [hda_dmaseg], 0
    mov word [hda_dmaraw], 0
.pci:
    call hda_pci_restore
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- native stream ABI -------------------------------------------------------
hda_stream:
    cmp byte [hda_up], 0
    je .e4
    cmp al, 0
    je hda_open
    cmp al, 1
    je hda_feed
    cmp al, 2
    je hda_close
    cmp al, 3
    je hda_status
    cmp al, 4
    je .e4                    ; input is deliberately not advertised
    cmp al, 5
    je .e7
    cmp al, 6
    je hda_stage
    cmp al, 7
    je hda_grant
    cmp al, 8
    je hda_busy
.e7:
    mov ax, 7
    stc
    ret
.e4:
    mov ax, 4
    stc
    ret

hda_busy:
    mov [hda_clip], dl
    xor ax, ax
    cmp byte [hda_active], 0
    jne .yes
    cmp byte [hda_tone_on], 0
    jne .yes
    cmp byte [hda_probe_on], 0
    je .out
.yes:
    inc ax
.out:
    clc
    ret

hda_open:
    push bx
    push cx
    push dx
    push si
    mov [hda_oflags], ah        ; the SND_OPENF_* bits, banked FIRST: the
                                ; grant arithmetic below runs through AX, and
                                ; the first build tested AH after it - so every
                                ; stream opened LINEAR, the ring flag read as
                                ; the grant size's high byte. A linear open
                                ; refuses the first feed past the grant's end,
                                ; the ring never wraps, and the stream ends
                                ; one ring in: Tracker stopped 1.5 s into
                                ; BEVERLY.MOD on QEMU and on the 1015PN alike
    cmp byte [hda_active], 0
    jne .busy
    cmp byte [hda_clip], 0
    jne .busy
    cmp byte [hda_tone_on], 0
    jne .busy
    cmp byte [hda_probe_on], 0
    jne .busy
    cmp bx, 4000
    jb .rate
    cmp bx, HDA_RATE
    ja .rate
    or cx, cx
    jz .bad
    call hda_range
    jc .bad
    mov ax, [hda_gr_size]
    sub ax, si
    mov [hda_src_max], ax
    mov [hda_rate], bx
    mov [hda_src_off], si
    mov [hda_total], cx
    mov word [hda_fed], 0
    mov word [hda_consumed], 0
    mov word [hda_phase], 0
    mov byte [hda_state], SND_ST_PLAYING
    mov byte [hda_ring], 0
    test byte [hda_oflags], SND_OPENF_RING
    jz .linear
    mov byte [hda_ring], 1
    mov ax, [hda_gr_size]
    sub ax, si
    cmp ax, 4096
    jb .bad
    cmp ax, 32768
    ja .bad
    mov dx, ax
    dec dx
    test ax, dx
    jnz .bad
    cmp cx, ax
    ja .bad
    mov [hda_rmask], dx
.linear:
    inc byte [hda_gen]
    mov byte [hda_active], 1
    mov byte [hda_eof], 0
    mov byte [hda_tail], HDA_PERIODS
    xor bx, bx
.prime:
    call hda_fill_period
    inc bl
    cmp bl, HDA_PERIODS
    jb .prime
    mov byte [hda_lastper], 0
    mov dh, [hda_gen]
    mov ax, hda_worker
    call OSAPI_DRV_TASK
    jc .task
    call hda_hw_start
    mov ah, [hda_gen]
    xor al, al
    clc
    jmp .out
.task:
    mov byte [hda_active], 0
    mov ax, 6
    stc
    jmp .out
.busy:
    mov ax, 1
    stc
    jmp .out
.rate:
    mov ax, 2
    stc
    jmp .out
.bad:
    mov ax, 7
    stc
 .out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

hda_feed:
    pushf
    cli
    call hda_handle
    jc .stale
    cmp byte [hda_ring], 0
    jne .ring
    cmp cx, [hda_total]
    jb .bad
    cmp cx, [hda_src_max]
    ja .bad
    jmp .store
.ring:
    mov ax, cx
    sub ax, [hda_total]
    test ax, ax
    js .bad
    mov ax, cx
    sub ax, [hda_fed]
    cmp ax, [hda_rmask]
    ja .bad
.store:
    mov [hda_total], cx
    mov byte [hda_eof], 0
    mov byte [hda_state], SND_ST_PLAYING
    popf
    xor ax, ax
    clc
    ret
.bad:
    popf
    mov ax, 7
    stc
    ret
.stale:
    popf
    mov ax, 0xffff
    stc
    ret

hda_close:
    call hda_handle
    jc .stale
    call hda_stream_stop
    xor ax, ax
    clc
    ret
.stale:
    mov ax, 0xffff
    stc
    ret

hda_status:
    pushf
    cli
    call hda_handle
    jc .stale
    xor ax, ax
    mov al, [hda_state]
    mov dx, [hda_consumed]
    popf
    clc
    ret
.stale:
    popf
    mov ax, 0xffff
    xor dx, dx
    stc
    ret

hda_handle:
    cmp byte [hda_active], 1
    jne .bad
    cmp ah, [hda_gen]
    jne .bad
    clc
    ret
.bad:
    stc
    ret

; One staging grant is enough: os8088 has one global stream.  The segment is
; private to the driver; packages only see offsets and copy through verb 6.
hda_grant:
    push bx
    push cx
    push dx
    or ah, ah
    jz .alloc
    cmp ah, 1
    jne .bad
    cmp byte [hda_gr_live], 0
    je .bad
    cmp dh, [hda_gr_owner]
    jne .bad
    cmp si, 0
    jne .bad
    cmp byte [hda_active], 0
    jne .bad
    call hda_grant_drop
    xor ax, ax
    clc
    jmp .out
.alloc:
    cmp byte [hda_gr_live], 0
    jne .space
    or cx, cx
    jz .bad
    cmp cx, HDA_STAGE_KB * 1024
    ja .bad
    mov bx, cx
    mov [hda_gr_owner], dh      ; the caller's instance slot - BEFORE the claim,
                                ; whose output is DX and so overwrites DH. Read
                                ; afterwards it was the claim's segment high
                                ; byte, and hda_range then refused every open
                                ; and stage from the instance that owned it
    mov ax, cx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .space
    mov [hda_gr_seg], dx
    mov [hda_gr_size], bx       ; exact requested capacity, not KB rounding
    mov byte [hda_gr_live], 1
    xor si, si
    xor ax, ax
    clc
    jmp .out
.space:
    mov ax, 8
    stc
    jmp .out
.bad:
    mov ax, 7
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

hda_grant_drop:
    push dx
    cmp byte [hda_gr_live], 0
    je .out
    mov dx, [hda_gr_seg]
    mov byte [hda_gr_live], 0
    mov word [hda_gr_seg], 0
    call OSAPI_MEM_FREE
.out:
    pop dx
    ret

hda_range:                     ; SI..SI+CX must be in caller's grant
    cmp byte [hda_gr_live], 0
    je .bad
    cmp dh, [hda_gr_owner]
    jne .bad
    mov ax, si
    add ax, cx
    jc .bad
    cmp ax, [hda_gr_size]
    ja .bad
    clc
    ret
.bad:
    stc
    ret

hda_stage:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push si
    mov si, di
    call hda_range
    pop si
    jc .bad
    mov es, [hda_gr_seg]
    push ds
    mov ds, bx                 ; caller segment supplied by kernel wrapper
    cld
    rep movsb
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    xor ax, ax
    clc
    ret
.bad:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov ax, 7
    stc
    ret

; Convert one HDA period. BL names the period. Bresenham phase conversion
; preserves duration for every native rate from 4 kHz through 44.1 kHz.
hda_fill_period:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    xor bh, bh
    mov ax, bx
    mov cl, 13
    shl ax, cl
    add ax, HDA_RING_OFF
    mov di, ax
    mov es, [hda_dmaseg]
    mov ds, [hda_gr_seg]
    cld                         ; stosw below: never inherit a caller's DF
    mov cx, HDA_FRAMES
.frame:
    mov al, 0x80               ; silence if the producer has no next byte
    mov dx, [cs:hda_fed]
    cmp dx, [cs:hda_total]
    je .sample
    mov si, [cs:hda_src_off]
    cmp byte [cs:hda_ring], 0
    je .idx
    and dx, [cs:hda_rmask]
.idx:
    add si, dx
    mov al, [si]
    mov dx, [cs:hda_phase]
    add dx, [cs:hda_rate]
    cmp dx, HDA_RATE
    jb .phase
    sub dx, HDA_RATE
    inc word [cs:hda_fed]
    inc word [cs:hda_consumed]
.phase:
    mov [cs:hda_phase], dx
    mov byte [cs:hda_state], SND_ST_PLAYING
    jmp .sample
.sample:
    xor al, 0x80               ; unsigned 8-bit -> signed 16-bit, high byte
    xor ah, ah
    xchg al, ah
    stosw
    stosw
    loop .frame
    cmp byte [cs:hda_ring], 0
    jne .under
    mov ax, [cs:hda_fed]
    cmp ax, [cs:hda_total]
    jne .out
    mov byte [cs:hda_eof], 1
    jmp .out
.under:
    mov ax, [cs:hda_fed]
    cmp ax, [cs:hda_total]
    jne .out
    mov byte [cs:hda_state], SND_ST_UNDER
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hda_worker:
    mov byte [hda_wtask], 1
.loop:
    cmp byte [hda_active], 1
    jne .exit
    cmp dh, [hda_gen]
    jne .exit
    call hda_hw_period
    cmp al, [hda_lastper]
    je .sleep
.advance:
    mov bl, [hda_lastper]
    call hda_fill_period
    inc byte [hda_lastper]
    and byte [hda_lastper], 3
    cmp byte [hda_eof], 0
    je .again
    dec byte [hda_tail]
    jnz .again
    mov byte [hda_state], SND_ST_ENDED
    call hda_hw_stop
    jmp .exit
.again:
    cmp al, [hda_lastper]
    jne .advance
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP
    call OSAPI_TASK_PARK
    jmp .loop
.exit:
    mov byte [hda_wtask], 0
    xor ax, ax
    call OSAPI_DRV_TASK         ; never returns

hda_stream_stop:
    cmp byte [hda_active], 0
    je .out
    mov byte [hda_active], 0
    call hda_hw_stop
.out:
    ret

; --- controller and codec ---------------------------------------------------
cpu 386                         ; entire section is behind DRVR_MINCPU

; Tone sink for the native router and the Sound panel's Test button. HDA has
; no oscillator, so build one loop of signed 16-bit stereo square wave in the
; existing DMA ring. The target is an Atom-class 386+ machine; 8,192 bounded
; stores complete well inside a timer tick even though the router calls us in
; its atomic ownership window. Tone-off is only one MMIO write.
; ES:DI = the ring, BX = frames per half wave, EAX = the first half's level
; (both channels): one whole ring of square wave, the sign flipped every BX
; frames. The tone sink's loop, and HDAV_PROBE's
hda_fill_square:
    push cx
    push dx
    mov dx, bx
    mov ecx, HDA_RING_BYTES / 4
.sample:
    mov [es:di], eax
    add di, 4
    dec dx
    jnz .next
    mov dx, bx
    xor eax, 0x80008000
.next:
    dec ecx
    jnz .sample
    pop dx
    pop cx
    ret

hda_tone:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    or ax, ax
    jz .off
    or dl, dl
    jnz .bad
    cmp byte [hda_active], 0
    jne .bad
    cmp ax, 19
    jb .bad
    cmp ax, 20000
    ja .bad
    mov bx, ax
    call hda_hw_stop
    mov ax, HDA_RATE / 2
    xor dx, dx
    div bx                      ; frames per half wave, at least one
    or ax, ax
    jnz .period
    inc ax
.period:
    mov bx, ax
    mov es, [hda_dmaseg]
    mov di, HDA_RING_OFF
    mov eax, 0x40004000
    call hda_fill_square
    call hda_hw_start
    mov byte [hda_tone_on], 1
    clc
    jmp .out
.off:
    cmp byte [hda_tone_on], 0
    je .ok
    call hda_hw_stop
    mov byte [hda_tone_on], 0
.ok:
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the package door (SPEC.md 34.11.1): what HDADIAG reads ------------------
; Two verbs, both UI-task only (they poll the immediate command interface and
; nothing here takes a lock). ES = the calling package's segment.
;   HDAV_INFO  in  CX = capacity of ES:DI, at least HDA_INFO_SZ
;              out CF=0 and ES:DI filled: 0x000 the PCI function's 256 bytes
;                  of configuration space, 0x100 the controller's first 0x80
;                  bytes, 0x180 our stream descriptor's 0x20, 0x1A0 32 bytes
;                  of driver state (hda_info_state's order), 0x1C0 the BDL as
;                  it is in memory, 0x200 the ring's first 32 bytes,
;                  0x220 the pins hda_pin_scan found and 0x228 their flags,
;                  0x230 the SKU word and 0x232 COEF 0 (hda_rt_init),
;                  0x234 the ring's foreign-byte count (hda_ring_foreign),
;                  0x236 the last stream start's record (hda_hw_start: SRST
;                  polls to 1, to 0, the control byte after RUN with the
;                  probe pattern above it, LPIB a settle later, the format),
;                  0x240 the DMA position buffer, 8 streams x 8 bytes,
;                  0x280 COEFs 00-1f as the link reset left them.
;                  CF=1 and AX=0: the buffer is too small.
;   HDAV_VERB  in  DX:AX = a 32-bit codec verb, exactly as the codec takes it
;              out CF=0 and DX:AX = the response; CF=1 on a timeout.
;   HDAV_PROBE in  AL = a pattern - 0 digital silence, or a 1 kHz square at
;              +/-0x4000 with one thing done BEFORE the stream starts:
;              1 nothing (the control); 2 the function group taken to D3
;              and back to D0 with long waits and the actual state polled,
;              then the DAC and pins to D0 again; 3 a codec function reset
;              (verb 7FF) and the whole codec initialisation again; 4 a
;              link reset and the whole codec initialisation again - a
;              second attach; 5 every speaker pin's control 0, so only the
;              headphone jack drives. 0xFF stops and puts the pins back.
;              The ring is filled and the engine started on the driver's
;              own stream, no router: what the ear then hears against what
;              the file says the engine read is the experiment (34.11.1).
;              CF=1 and AX=1 when a stream or the router's tone owns the
;              engine.
HDAV_INFO      equ 1
HDAV_VERB      equ 2
HDAV_PROBE     equ 3
HDA_INFO_SZ    equ 0x2c0

hda_pkgcall:
    cmp bl, HDAV_INFO
    je hda_v_info
    cmp bl, HDAV_VERB
    je hda_v_verb
    cmp bl, HDAV_PROBE
    je hda_v_probe
    xor ax, ax
    stc
    ret

hda_v_probe:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [hda_pattern], al       ; BANKED FIRST: hda_hw_stop zeroes EAX, and
                                ; report 3's five probes were all pattern 0
    cmp al, 0xff
    je .stop
    cmp byte [hda_active], 0
    jne .busy
    cmp byte [hda_tone_on], 0
    jne .busy
    cmp byte [hda_clip], 0
    jne .busy
    call hda_hw_stop
    mov es, [hda_dmaseg]
    mov di, HDA_RING_OFF
    cmp byte [hda_pattern], 0
    jne .square
    xor eax, eax                ; pattern 0: every sample zero
    mov ecx, HDA_RING_BYTES / 4
    cld
    rep stosd
    jmp .go
.square:
    mov bx, HDA_RATE / 2000     ; 1 kHz at +/-0x4000, at 44.1 kHz
    mov eax, 0x40004000
    call hda_fill_square
    mov al, [hda_pattern]
    cmp al, 2
    jne .n2
    mov eax, 0x00170503         ; 2: the function group to D3...
    call hda_verb
    mov cx, 50
    call hda_pauses             ; ...100 ms...
    mov eax, 0x00170500         ; ...to D0...
    call hda_verb
    mov cx, 250                 ; ...polled until PS-Act reads D0, half a
.d0:                            ; second at most, the way Linux syncs it
    mov eax, 0x001f0500
    call hda_verb
    mov [hda_dbg_ps], ax
    test al, 0xf0
    jz .d0ok
    call hda_pause
    loop .d0
.d0ok:
    mov cx, 50
    call hda_pauses             ; ...and 100 ms more for the bias to ramp
    mov bx, hda_power_verbs     ; the DAC and mixer to D0 again...
    call hda_verb_walk
    xor si, si
.pd0:
    movzx cx, byte [hda_npins]  ; ...and every pin
    cmp si, cx
    jae .pd0done
    movzx eax, byte [hda_pins+si]
    shl eax, 20
    or eax, 0x00070500
    call hda_verb
    inc si
    jmp .pd0
.pd0done:
    mov cx, 50
    call hda_pauses
    jmp short .go
.n2:
    cmp al, 3
    jne .n3
    mov eax, 0x0017ff00         ; 3: function reset, then the whole codec
    call hda_verb               ; initialisation again
    mov cx, 50
    call hda_pauses
    call hda_codec_init
    jmp short .go
.n3:
    cmp al, 4
    jne .n4
    call hda_ctl_reset          ; 4: the link reset and the whole codec
    call hda_codec_init         ; initialisation again - a second attach
    jmp short .go
.n4:
    cmp al, 5
    jne .go
    xor si, si                  ; 5: every speaker pin's control 0
.spk:
    movzx cx, byte [hda_npins]
    cmp si, cx
    jae .go
    test byte [hda_pinfl+si], 1
    jnz .spkn
    movzx eax, byte [hda_pins+si]
    shl eax, 20
    or eax, 0x00070700
    call hda_verb
.spkn:
    inc si
    jmp .spk
.go:
    call hda_hw_start
    mov byte [hda_probe_on], 1
    xor ax, ax
    clc
    jmp .out
.stop:
    mov word [hda_fmt], HDA_FMT
    cmp byte [hda_probe_on], 0
    je .ok
    call hda_hw_stop
    mov byte [hda_probe_on], 0
    call hda_pin_setup          ; the pins' control back (5)
.ok:
    xor ax, ax
    clc
    jmp short .out
.busy:
    mov ax, 1
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

hda_v_verb:
    push ebx
    movzx eax, ax
    movzx edx, dx
    shl edx, 16
    or eax, edx
    call hda_verb
    jc .bad
    mov edx, eax
    shr edx, 16
    pop ebx
    clc
    ret
.bad:
    xor ax, ax
    xor dx, dx
    pop ebx
    stc
    ret

hda_v_info:
    cmp cx, HDA_INFO_SZ
    jb .small
    push eax
    push ebx
    push cx
    push edx
    push si
    push di
    push ds
    cld
    xor bx, bx                  ; the PCI function, all 64 dwords
.pci:
    mov eax, 0x8000d800
    or ax, bx
    call hda_pci_read
    stosd
    add bx, 4
    cmp bx, 0x100
    jb .pci
    xor si, si                  ; the controller, 0x00..0x7C
.ctl:
    call hda_rd32
    stosd
    add si, 4
    cmp si, 0x80
    jb .ctl
    mov si, [hda_sd]            ; our stream descriptor, 0x20 bytes
    mov cx, 8
.sd:
    call hda_rd32
    stosd
    add si, 4
    loop .sd
    mov ax, [hda_dmaraw]        ; driver state, 32 bytes
    stosw
    mov ax, [hda_dmaseg]
    stosw
    mov ax, [hda_sd]
    stosw
    mov eax, [hda_bar]
    stosd
    mov ax, [hda_gr_seg]
    stosw
    mov ax, [hda_pci_orig]
    stosw
    mov al, [hda_up]
    stosb
    mov al, [hda_active]
    stosb
    mov al, [hda_tone_on]
    stosb
    mov al, [hda_state]
    stosb
    mov ax, [hda_rate]
    stosw
    mov ax, [hda_total]
    stosw
    mov ax, [hda_fed]
    stosw
    mov ax, [hda_consumed]
    stosw
    mov eax, cr0
    stosd
    mov al, [hda_npins]
    mov ah, [hda_gpio]
    stosw
    mov ax, [hda_dmaseg]        ; the BDL and the ring's head, as memory has
    or ax, ax                   ; them - zeros when nothing is claimed
    jz .noring
    mov ds, ax
    xor si, si
    mov cx, 32
    rep movsw
    mov si, HDA_RING_OFF
    mov cx, 16
    rep movsw
    jmp short .done
.noring:
    mov cx, 48
    rep stosw
.done:
    pop ds
    mov si, hda_pins            ; 0x220: the pins found and their flags,
    mov cx, HDA_MAXPINS         ; 0x230 the SKU word, 0x232 COEF 0
    rep movsw
    mov ax, [hda_sku]
    stosw
    mov ax, [hda_coef0]
    stosw
    call hda_ring_foreign       ; 0x234: ring bytes that are not a square's
    stosw
    mov ax, [hda_dbg_rst1]      ; 0x236: the last start's reset record
    stosw
    mov ax, [hda_dbg_rst0]
    stosw
    mov al, [hda_dbg_ctl]
    mov ah, [hda_pattern]
    stosw
    mov ax, [hda_dbg_lpib]
    stosw
    mov ax, [hda_fmt]
    stosw
    mov ax, [hda_dmaseg]        ; 0x240: the position buffer, 8 streams x 8
    or ax, ax                   ; bytes - what the DEVICE wrote into our claim
    jz .nopos
    push ds
    mov ds, ax
    mov si, HDA_POS_OFF
    mov cx, 32
    rep movsw
    pop ds
    jmp short .posdone
.nopos:
    mov cx, 32
    rep stosw
.posdone:
    mov si, hda_coef_def        ; 0x280: the coefficient file as the link
    mov cx, 32                  ; reset left it, before any write
    rep movsw
    pop di
    pop si
    pop edx
    pop cx
    pop ebx
    pop eax
    clc
    ret
.small:
    xor ax, ax
    stc
    ret

; The DMA position buffer is not used for pacing (the worker reads LPIB); it
; is on so that the controller WRITES into the claim, which is the one proof
; a report can carry that the device and the CPU mean the same physical
; memory: a position the diag reads out of 0x93000-something that tracks
; LPIB says the BDL and ring addresses the engine was given reach the bytes
; the CPU put there. Report 2 had every register right and noise out, and
; no way to tell a wrong-memory read from a wrong-codec decode
hda_posbuf_on:
    push si
    movzx eax, word [hda_dmaseg]
    shl eax, 4
    add eax, HDA_POS_OFF | 1    ; bit 0: enable
    mov si, HDA_DPLBASE
    call hda_wr32
    xor eax, eax
    mov si, HDA_DPUBASE
    call hda_wr32
    pop si
    ret

hda_posbuf_off:
    push si
    cmp dword [hda_bar], 0
    je .out
    xor eax, eax
    mov si, HDA_DPLBASE
    call hda_wr32
.out:
    pop si
    ret

; AX = how many of the ring's 32,768 bytes are none of 00, 40, C0, the
; only three a square wave from hda_fill_square or hda_v_probe's patterns
; can contain (0x10 and 0xF0 for pattern 2). Meaningful only while a probe
; or the tone owns the ring; a stream's ring is anything
hda_ring_foreign:
    push cx
    push si
    push ds
    mov ds, [hda_dmaseg]
    mov si, HDA_RING_OFF
    mov cx, HDA_RING_BYTES
    xor dx, dx
.b:
    lodsb
    test al, 0x0f               ; 00 40 c0 10 f0: low nibble zero...
    jnz .odd
    and al, 0x30                ; ...and bits 5:4 clear
    jz .next
.odd:
    inc dx
.next:
    loop .b
    mov ax, dx
    pop ds
    pop si
    pop cx
    ret

hda_pci_probe:
    mov eax, 0x8000d800         ; bus 0, device 1b, function 0, register 0
    call hda_pci_read
    cmp ax, 0x8086
    jne .bad
    mov eax, 0x8000d808
    call hda_pci_read
    shr eax, 16
    cmp ax, 0x0403
    jne .bad
    mov eax, 0x8000d810
    call hda_pci_read
    and eax, 0xffffc000
    jz .bad
    mov [hda_bar], eax
    mov eax, 0x8000d804
    call hda_pci_read
    mov [hda_pci_orig], ax
    or ax, 6                    ; memory decoding and bus mastering
    mov dx, 0x0cfc
    out dx, ax
    clc
    ret
.bad:
    stc
    ret

hda_pci_restore:
    cmp word [hda_pci_orig], 0xffff
    je .out
    pushf
    cli
    mov dx, 0x0cf8
    mov eax, 0x8000d804
    out dx, eax
    mov dx, 0x0cfc
    mov ax, [hda_pci_orig]
    out dx, ax
    popf
    mov word [hda_pci_orig], 0xffff
.out:
    ret

hda_pci_read:                  ; EAX=config address -> EAX=value
    push dx
    pushf
    cli
    mov dx, 0x0cf8
    out dx, eax
    mov dx, 0x0cfc
    in eax, dx
    popf
    pop dx
    ret

hda_ctl_reset:
    xor eax, eax
    mov si, HDA_GCTL
    call hda_wr32
    call hda_pause
    mov eax, 1
    call hda_wr32
    mov cx, HDA_POLL
.wait:
    call hda_rd32
    test al, 1
    jnz .up
    loop .wait
    stc
    ret
.up:
    call hda_pause              ; codecs need their wake interval after CRST=1
    mov si, HDA_GCAP
    call hda_rd16
    mov dx, ax
    and ax, 0x0f00
    mov cl, 3
    shr ax, cl                  ; input streams * 0x20
    add ax, HDA_SD_BASE
    mov [hda_sd], ax
    mov ax, dx
    and ax, 0xf000
    jz .bad
    mov si, HDA_STATESTS
    call hda_rd16
    test al, 1                 ; codec address zero on the 1015PN
    jz .bad
    clc
    ret
.bad:
    stc
    ret

hda_codec_init:
    mov eax, 0x000f0000         ; node 0 GET_PARAMETER vendor id
    call hda_verb
    jc .bad
    cmp eax, 0x10ec0269
    jne .bad                    ; intentionally scoped to the Eee's ALC269
    mov bx, hda_power_verbs     ; D0 on the function group and the path...
    call hda_verb_walk
    jc .bad
    call hda_pin_scan           ; ...and on every output pin the codec's own
    jc .bad                     ; configuration defaults say is wired
    call hda_pause              ; ...and let them arrive there. A verb sent to
                                ; a widget still powering up is answered and
                                ; not applied, and QEMU has no such interval
    call hda_rt_init            ; Realtek's half: the SKU's amplifier switch
    jc .bad                     ; and the variant's coefficient fixes
    mov bx, hda_codec_verbs
    call hda_verb_walk
    jc .bad
    call hda_amp_init
    jc .bad
    call hda_pin_setup
    jc .bad
    clc
    ret
.bad:
    stc
    ret

; --- the pins, DISCOVERED (SPEC.md 34.11) ----------------------------------
; The first build drove speaker 14 and headphone 15 by name, which is the
; ALC269 reference topology and not necessarily the 1015PN's: an ALC269VB
; puts the headphone jack on 21, and a pin the board never wired answers its
; verbs and drives nothing. So the AFG's widgets are walked once at attach
; and every pin complex that can drive an output (pin caps bit 4) and whose
; configuration default says a line out, a speaker or a headphone is
; connected (port connectivity != 01) is put in hda_pins - up to HDA_MAXPINS,
; the headphone flagged - and powered up here, in the same burst as the
; function group and the DAC, so the settle that follows covers it. A codec
; whose defaults name nothing (a BIOS that never programmed them) gets the
; reference pair back, because refusing would leave the machine mute for a
; reason no report could show.
hda_pin_scan:
    push bx
    push cx
    push dx
    push si
    mov byte [hda_npins], 0
    mov eax, 0x001f0004         ; AFG: subordinate node start and count
    call hda_verb
    jc .bad
    mov cx, ax
    xor ch, ch                  ; CX = count
    shr eax, 16
    mov bl, al                  ; BL = the first widget
.node:
    or cx, cx
    jz .scanned
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f0009          ; widget capabilities
    call hda_verb
    jc .bad
    shr eax, 20
    and al, 0x0f
    cmp al, 4                   ; a pin complex
    jne .next
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f000c          ; pin capabilities
    call hda_verb
    jc .bad
    test al, 0x10               ; output capable
    jz .next
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f1c00          ; configuration default
    call hda_verb
    jc .bad
    mov edx, eax
    shr edx, 30
    cmp dl, 1                   ; port connectivity 01: nothing on it
    je .next
    shr eax, 20
    and al, 0x0f                ; default device: 0 line out, 1 speaker,
    cmp al, 2                   ; 2 headphone; nothing else drives sound
    ja .next
    movzx si, byte [hda_npins]
    cmp si, HDA_MAXPINS
    jae .next
    mov [hda_pins+si], bl
    cmp al, 2
    sete al
    mov [hda_pinfl+si], al      ; bit 0: a headphone jack
    inc byte [hda_npins]
    movzx eax, bl
    shl eax, 20
    or eax, 0x00070500          ; D0
    call hda_verb
    jc .bad
.next:
    inc bl
    dec cx
    jmp .node
.scanned:
    cmp byte [hda_npins], 0
    jne .ok
    mov byte [hda_pins], 0x14   ; the reference pair, powered like the rest
    mov byte [hda_pinfl], 0
    mov byte [hda_pins+1], 0x15
    mov byte [hda_pinfl+1], 1
    mov byte [hda_npins], 2
    mov eax, 0x01470500
    call hda_verb
    jc .bad
    mov eax, 0x01570500
    call hda_verb
    jc .bad
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; Each pin found: its connection select pointed at the entry that reaches
; the DAC (mixer 0c, or DAC 02 itself, whichever its list has first; left
; alone when neither is on it), output enabled - with HP-enable on the
; headphone - the external amplifier enabled, and its output amplifier
; unmuted at the 0 dB step of its own capabilities or the function group's,
; hda_amp_init's rule
hda_pin_setup:
    push bx
    push cx
    push dx
    push si
    xor si, si
.pin:
    cmp si, HDA_MAXPINS
    jae .ok
    movzx cx, byte [hda_npins]
    cmp si, cx
    jae .ok
    mov bl, [hda_pins+si]
    call hda_pin_select
    jc .bad
    movzx eax, bl
    shl eax, 20
    or eax, 0x00070740          ; pin control: OUT_EN
    test byte [hda_pinfl+si], 1
    jz .ctl
    or al, 0x80                 ; ...and HP_EN on the jack
.ctl:
    call hda_verb
    jc .bad
    movzx eax, bl
    shl eax, 20
    or eax, 0x00070c02          ; EAPD: the external amplifier on
    call hda_verb
    jc .bad
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f0012          ; its output amplifier's capabilities...
    call hda_verb
    jc .bad
    test eax, eax
    jnz .caps
    mov eax, 0x001f0012         ; ...or the function group's
    call hda_verb
    jc .bad
.caps:
    and eax, 0x7f
    movzx edx, bl
    shl edx, 20
    or edx, 0x0003b000          ; SET_AMP_GAIN_MUTE, output, both, unmuted
    or eax, edx
    call hda_verb
    jc .bad
    inc si
    jmp .pin
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

hda_pin_select:                ; BL = pin: select the entry that reaches the DAC
    push cx
    push dx
    push si
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f000e          ; connection list length
    call hda_verb
    jc .bad
    mov dh, al                  ; bit 7: the long form, two entries a word
    and al, 0x7f
    mov dl, al                  ; entries left
    xor si, si                  ; the index
.entry:
    or dl, dl
    jz .ok                      ; nothing on the list reaches the DAC: leave it
    movzx eax, bl
    shl eax, 20
    or eax, 0x000f0200          ; GET_CONNECTION_LIST_ENTRY, from a base
    mov cx, si
    test dh, 0x80
    jnz .long
    and cl, 0xfc                ; short form: four entries a response
    or al, cl
    call hda_verb
    jc .bad
    mov cx, si
    and cl, 3
    shl cl, 3
    shr eax, cl
    movzx eax, al
    jmp short .test
.long:
    and cl, 0xfe                ; long form: two
    or al, cl
    call hda_verb
    jc .bad
    mov cx, si
    and cl, 1
    shl cl, 4
    shr eax, cl
    movzx eax, ax
.test:
    cmp ax, 0x0c
    je .found
    cmp ax, 0x02
    je .found
    inc si
    dec dl
    jmp .entry
.found:
    movzx eax, bl
    shl eax, 20
    or eax, 0x00070100          ; SET_CONNECTION_SELECT
    mov cx, si
    or al, cl
    call hda_verb
    jc .bad
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    ret

; --- Realtek's half --------------------------------------------------------
; Two things an ALC269 wants that the HDA specification does not describe,
; both from Realtek's own programming conventions as every open driver
; applies them. First, the SKU: the low word of the codec's subsystem ID -
; or, when that word is the PCI function's own or has bit 0 clear, the
; configuration default of pin 1d, which the codec's firmware uses as a
; 16-bit SKU word with a popcount checksum in bits 19:16 - carries in bits
; 5:3 how the board switches its external amplifier: 1, 3 and 7 name GPIO
; masks 1, 2 and 3, and anything else means EAPD alone. A board that switches
; its speaker amplifier by GPIO is mute with every pin verb right, and the
; first report off the 1015PN could not say which kind it was. Second, the
; coefficient file behind vendor node 20 (index by verb 500, read C00, write
; 4): COEF 0's bits 7:4 name the variant, and each variant has a bit or two
; that the codec leaves wrong at reset - the VA's PLL bit, the VB's class-D
; and capless-output enables. Every write here is gated on the exact variant
; and revision it is for; a codec the table does not name gets no coef
; written. The Sound page cannot show any of this, so what was decided is
; in the HDAV_INFO block for HDADIAG to print
hda_rt_init:
    push bx
    push cx
    push dx
    mov eax, 0x001f2000         ; AFG: subsystem ID
    call hda_verb
    jc .bad
    mov edx, eax
    mov eax, 0x8000d82c         ; ...against the PCI function's
    call hda_pci_read
    shr eax, 16
    cmp dx, ax
    je .pin1d
    test dl, 1
    jz .pin1d
    jmp short .sku
.pin1d:
    mov eax, 0x01df1c00         ; pin 1d's configuration default as the SKU
    call hda_verb
    jc .bad
    test al, 1                  ; bit 0: the word is valid
    jz .coef
    mov dx, ax
    and dx, 0xfffe
    xor cl, cl
.pop:
    shr dx, 1
    adc cl, 0
    or dx, dx
    jnz .pop
    mov edx, eax
    shr edx, 16
    and dl, 0x0f
    cmp cl, dl                  ; bits 19:16: the popcount of 15:1
    jne .coef
    mov dx, ax
.sku:
    mov [hda_sku], dx
    mov al, dl
    shr al, 3
    and al, 7                   ; bits 5:3: external amplifier control
    mov cl, 1
    cmp al, 1
    je .gpio
    mov cl, 2
    cmp al, 3
    je .gpio
    mov cl, 3
    cmp al, 7
    jne .coef
.gpio:
    mov [hda_gpio], cl
    mov eax, 0x00171600         ; GPIO mask, direction (out) and data: on
    or al, cl
    call hda_verb
    jc .bad
    mov eax, 0x00171700
    or al, cl
    call hda_verb
    jc .bad
    mov eax, 0x00171500
    or al, cl
    call hda_verb
    jc .bad
.coef:
    cmp byte [hda_def_saved], 0 ; the file as the link reset left it, ONCE:
    jne .have                   ; a probe's stop runs this again to put the
    push si                     ; writes back, and must not re-save its own
    xor si, si
.save:
    mov ax, si
    call hda_coef_rd
    jc .savebad
    push si
    shl si, 1
    mov [hda_coef_def+si], ax
    pop si
    inc si
    cmp si, 32
    jb .save
    pop si
    mov byte [hda_def_saved], 1
    jmp short .have
.savebad:
    pop si
    jmp .bad
.have:
    xor al, al
    call hda_coef_rd
    jc .bad
    mov [hda_coef0], ax
    mov bx, ax
    and bl, 0xf0
    cmp bl, 0x10
    je .vb
    cmp bl, 0x20
    je .vc
    cmp bl, 0x30
    je .vd
    mov al, 4                   ; ALC269VA: the PLL bit, COEF 4 bit 15, off
    call hda_coef_rd
    jc .bad
    mov dx, ax
    and dh, 0x7f
    mov al, 4
    call hda_coef_wr
    jc .bad
    jmp .ok
.vc:
    mov al, 4                   ; ALC269VC: COEF 4 bit 15 off (EAPD by coef)
    call hda_coef_rd
    jc .bad
    mov dx, ax
    and dh, 0x7f
    mov al, 4
    call hda_coef_wr
    jc .bad
    jmp .ok
.vd:
    mov al, 0x10                ; ALC269VD: COEF 10 bit 9 off
    call hda_coef_rd
    jc .bad
    mov dx, ax
    and dh, 0xfd
    mov al, 0x10
    call hda_coef_wr
    jc .bad
    jmp .ok
.vb:
    mov al, 0x0d                ; ALC269VB: COEF d bit 14 on (EAPD by coef)
    call hda_coef_rd
    jc .bad
    mov dx, ax
    or dh, 0x40
    mov al, 0x0d
    call hda_coef_wr
    jc .bad
    mov ax, [hda_coef0]         ; ...and by revision, COEF 0's low byte:
    cmp al, 0x15
    jae .r16
    mov dx, 0x960b              ; under 0x15: COEF f = 960b, e = 8817
    mov al, 0x0f
    call hda_coef_wr
    jc .bad
    mov dx, 0x8817
    mov al, 0x0e
    call hda_coef_wr
    jc .bad
    jmp short .hp
.r16:
    cmp al, 0x16
    jne .r18
    mov dx, 0x960b              ; 0x16: COEF f = 960b, e = 8814
    mov al, 0x0f
    call hda_coef_wr
    jc .bad
    mov dx, 0x8814
    mov al, 0x0e
    call hda_coef_wr
    jc .bad
    jmp short .hp
.r18:
    cmp al, 0x18
    jne .hp
    mov al, 0x0d                ; 0x18: capless ramp-up clock, COEF d
    call hda_coef_rd            ; bits 11:10 = 01
    jc .bad
    mov dx, ax
    and ax, 0x0c00
    cmp ax, 0x0400
    je .classd
    or dh, 0x04
    mov al, 0x0d
    call hda_coef_wr
    jc .bad
.classd:
    mov al, 0x17                ; ...and class-D power-on reset, COEF 17
    call hda_coef_rd            ; bits 8:6 = 100
    jc .bad
    mov dx, ax
    and ax, 0x01c0
    cmp ax, 0x0100
    je .hp
    or dl, 0x80
    mov al, 0x17
    call hda_coef_wr
    jc .bad
.hp:
    mov al, 4                   ; every VB: COEF 4 bit 11 on, the headphone
    call hda_coef_rd            ; (and 0x17's "power up output pin")
    jc .bad
    mov dx, ax
    or dh, 0x08
    mov al, 4
    call hda_coef_wr
    jc .bad
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

hda_coef_def_wr:               ; AL = index: that coefficient back to the reset's
    push bx
    movzx bx, al
    shl bx, 1
    mov dx, [hda_coef_def+bx]
    call hda_coef_wr
    pop bx
    ret

hda_coef_rd:                   ; AL = index -> AX = the coefficient; CF timeout
    movzx eax, al
    or eax, 0x02050000          ; node 20 SET_COEF_INDEX
    call hda_verb
    jc .out
    mov eax, 0x020c0000         ; GET_PROC_COEF
    call hda_verb
.out:
    ret

hda_coef_wr:                   ; AL = index, DX = the value; CF on a timeout
    movzx eax, al
    or eax, 0x02050000
    call hda_verb
    jc .out
    movzx eax, dx
    or eax, 0x02040000          ; SET_PROC_COEF: the four-bit verb form
    call hda_verb
.out:
    ret

hda_verb_walk:                 ; BX -> dd verbs, 0-terminated; CF on a timeout
    mov eax, [bx]
    test eax, eax
    jz .ok
    call hda_verb
    jc .bad
    add bx, 4
    jmp hda_verb_walk
.ok:
    clc
    ret
.bad:
    stc
    ret

; Every amplifier on the path goes to 0 dB, and 0 dB is a NUMBER THE CODEC
; ANSWERS, not step 0: the OFFSET field of the amplifier capabilities (HDA
; spec 7.3.4.10) names the step that is 0 dB, and steps below it attenuate.
; The first build sent gain 0 everywhere, which on an ALC269's DAC is 0x57
; steps of 0.75 dB under 0 dB - silence with the DMA running. A widget answers
; its own capabilities when it overrides the function group's and 0 when it
; does not, in which case node 1's are the ones in force; the pins' amplifiers
; are mute-only (offset 0) and the read costs one verb each.
hda_amp_init:
    push bx
    mov bx, hda_amp_tab
.next:
    mov eax, [bx]               ; GET_PARAMETER for the caps; 0 ends the table
    test eax, eax
    jz .ok
    call hda_verb
    jc .bad
    test eax, eax
    jnz .caps
    mov eax, [bx]               ; no override: the function group's caps
    and eax, 0x000fffff
    or eax, 0x00100000
    call hda_verb
    jc .bad
.caps:
    and eax, 0x7f               ; offset = the 0 dB step
    or eax, [bx+4]              ; SET_AMP_GAIN_MUTE with that gain, unmuted
    call hda_verb
    jc .bad
    add bx, 8
    jmp .next
.ok:
    pop bx
    clc
    ret
.bad:
    pop bx
    stc
    ret

hda_verb:
    push bx
    push cx
    push si
    mov ebx, eax
    mov cx, HDA_POLL
.idle:
    mov si, HDA_ICIS
    call hda_rd16
    test al, 1
    jz .send
    loop .idle
    stc
    jmp .out
.send:
    mov ax, 2                   ; acknowledge the preceding valid response
    mov si, HDA_ICIS
    call hda_wr16
    mov eax, ebx
    mov si, HDA_ICOI
    call hda_wr32
    mov ax, 1
    mov si, HDA_ICIS
    call hda_wr16
    mov cx, HDA_POLL
.done:
    call hda_rd16
    and al, 3
    cmp al, 2
    je .response
    loop .done
    stc
    jmp .out
.response:
    mov si, HDA_ICII
    call hda_rd32
    clc
.out:
    pop si
    pop cx
    pop bx
    ret

hda_bdl_init:
    push ax
    push bx
    push cx
    push di
    push es
    mov es, [hda_dmaseg]
    xor di, di
    movzx eax, word [hda_dmaseg]
    shl eax, 4
    add eax, HDA_RING_OFF
    mov cx, HDA_PERIODS
.entry:
    mov [es:di], eax
    mov dword [es:di+4], 0
    mov dword [es:di+8], HDA_PERIOD
    mov dword [es:di+12], 0
    add eax, HDA_PERIOD
    add di, 16
    loop .entry
    mov dword [es:di-4], 1      ; IOC on the last: the engine sets BCIS in
                                ; SDSTS when it completes, which a report
                                ; can see and which proves the engine read
                                ; THIS list - no interrupt follows, IOCE and
                                ; INTCTL are never set (SPEC.md 34.11.1)
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; The stream reset is a HANDSHAKE, not a pulse (HDA spec 3.3.35): SRST reads
; back 1 only once the engine is in reset, and 0 only once it is out, and the
; descriptor registers written in between are the ones the reset clears. The
; first build wrote 1, spun 256 iterations - nanoseconds on an Atom - and
; wrote 0, so on the 1015PN the CBL/LVI/FMT/BDL writes below could land while
; the reset was still pending and be wiped by it: RUN then started a stream
; with no buffers, LPIB never moved, and Tracker refilled a 16KB ring at
; 11 kHz and stopped when the feed was refused, 1.5 s in. QEMU latches the
; bit instantly and cannot show it. Both waits are bounded by HDA_POLL and a
; timeout falls through: a stream that will not reset will not run either,
; and the worker's refill loop is safe on a stalled LPIB
; What the reset handshake actually saw is recorded for HDADIAG:
; hda_dbg_rst1 and _rst0 are the polls SRST took to read 1 and then 0
; (0xFFFF = never did), hda_dbg_ctl the control byte read back after RUN,
; hda_dbg_lpib LPIB one settle after RUN (about 350 bytes expected at
; 44.1 kHz). On the 1015PN both handshakes answer on the first poll and
; LPIB reads 0x258 a settle in (report 4). Report 3 read a stream started
; from a task as "RUN set, LPIB never moving" because five samples taken the
; same nine ticks after five fresh starts read the same LPIB - the same
; PHASE, not a stall - and report 4's two samples a probe show every one of
; them running at the format's exact rate. So the start runs with interrupts
; as the caller left them, as it always did
hda_hw_start:
    call hda_hw_stop
    mov si, [hda_sd]
    add si, HDA_SD_CTL
    mov al, 1
    call hda_wr8
    mov cx, HDA_POLL
.rst:
    call hda_rd8
    test al, 1
    jnz .inrst
    loop .rst
    mov cx, HDA_POLL + 1        ; never read 1: HDA_POLL - CX = 0xFFFF below
.inrst:
    mov ax, HDA_POLL
    sub ax, cx
    mov [hda_dbg_rst1], ax
    xor al, al
    call hda_wr8
    mov cx, HDA_POLL
.unrst:
    call hda_rd8
    test al, 1
    jz .ready
    loop .unrst
    mov cx, HDA_POLL + 1
.ready:
    mov ax, HDA_POLL
    sub ax, cx
    mov [hda_dbg_rst0], ax
    mov si, [hda_sd]
    add si, HDA_SD_CBL
    mov eax, HDA_RING_BYTES
    call hda_wr32
    mov si, [hda_sd]
    add si, HDA_SD_LVI
    mov ax, HDA_PERIODS-1
    call hda_wr16
    mov si, [hda_sd]
    add si, HDA_SD_FMT
    mov ax, [hda_fmt]           ; HDA_FMT, unless a probe is trying another
    call hda_wr16
    movzx eax, word [hda_dmaseg]
    shl eax, 4
    mov si, [hda_sd]
    add si, HDA_SD_BDLPL
    call hda_wr32
    xor eax, eax
    mov si, [hda_sd]
    add si, HDA_SD_BDLPU
    call hda_wr32
    ; THE CODEC'S HALF, AT EVERY START: the DAC's converter format and its
    ; stream/channel, the way the controller's SD FMT is written at every
    ; start. The first build sent them once at attach, in the same burst as
    ; the power-state verbs; on the 1015PN that came out as loud white
    ; noise - the link carried 16-bit stereo and the DAC decoded it as
    ; whatever it had been left in, which is the sound of a format mismatch
    ; (each word split at the wrong bit). QEMU's codec applies every verb
    ; instantly and cannot show it. The format is read back and re-sent once
    ; after a settle if it did not take
    mov cx, 2
.fmt:
    mov eax, 0x00220000         ; DAC 02 converter format = [hda_fmt]
    mov ax, [hda_fmt]
    call hda_verb
    mov eax, 0x00270610         ; DAC 02 stream tag 1, channel 0
    call hda_verb
    mov eax, 0x002a0000         ; GET_CONVERTER_FORMAT on the DAC
    call hda_verb
    cmp ax, [hda_fmt]
    je .fmtok
    call hda_pause
    loop .fmt
.fmtok:
    mov eax, 0x00100002         ; stream tag 1, RUN
    mov si, [hda_sd]
    add si, HDA_SD_CTL
    call hda_wr32
    call hda_rd8
    mov [hda_dbg_ctl], al
    call hda_pause
    mov si, [hda_sd]
    add si, HDA_SD_LPIB
    call hda_rd32
    mov [hda_dbg_lpib], ax
    ret

hda_hw_stop:
    cmp word [hda_sd], 0
    je .out
    xor eax, eax
    mov si, [hda_sd]
    add si, HDA_SD_CTL
    call hda_wr32
.out:
    ret

hda_hw_period:
    push si
    mov si, [hda_sd]
    add si, HDA_SD_LPIB
    call hda_rd32
    shr eax, 13
    and al, 3
    pop si
    ret

hda_pauses:                    ; CX settles of ~2 ms each
    call hda_pause
    loop hda_pauses
    ret

hda_pause:
    push ax
    push cx
    mov cx, HDA_SETTLE
.p:
    out 0x80, al                ; LPC/POST delay: bounded in iterations, not
                                ; CPU clocks, so an Atom cannot outrun it
    loop .p
    pop cx
    pop ax
    ret

; Each MMIO access re-arms FS and keeps interrupts out until the access is
; complete. The scheduler and BIOS only preserve 16-bit state.
hda_rd16:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov ax, [fs:edi]
    pop edi
    popf
    ret
hda_rd8:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov al, [fs:edi]
    pop edi
    popf
    ret
hda_rd32:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov eax, [fs:edi]
    pop edi
    popf
    ret
hda_wr16:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov [fs:edi], ax
    pop edi
    popf
    ret
hda_wr8:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov [fs:edi], al
    pop edi
    popf
    ret
hda_wr32:
    pushf
    cli
    call hda_arm
    push edi
    movzx edi, si
    add edi, [hda_bar]
    mov [fs:edi], eax
    pop edi
    popf
    ret

hda_arm:
    push eax
    push ebx
    push ecx
    push edx
    pushf
    cli
    mov al, 0x80
    out 0x70, al
    in al, 0x71
    mov ax, ds
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, hda_gdt
    adc dx, 0
    mov [hda_gdtr+2], ax
    mov [hda_gdtr+4], dx
    lgdt [hda_gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .p1
.p1:
    mov bx, 8
    mov fs, bx
    mov eax, cr0
    and al, 0xfe
    mov cr0, eax
    jmp short .p2
.p2:
    xor al, al
    out 0x70, al
    in al, 0x71
    popf
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; ALC269 nodes used by the 1015PN: DAC 02, mixer 0c, speaker 14, headphone 15.
; Standard 12-bit verbs are (nid<<20)|(verb<<8)|payload; converter format and
; amplifier are the HDA four-bit verb form.
hda_power_verbs:                ; the pins are hda_pin_scan's, in the same burst
    dd 0x00170500, 0x00270500, 0x00c70500
    dd 0
hda_codec_verbs:                ; ...after hda_pause: the routing. The DAC's
                                ; format and stream are hda_hw_start's, sent
                                ; at every start (hda_fmt_verbs); the pins'
                                ; control, EAPD and amplifiers are
                                ; hda_pin_setup's, per pin found
    dd 0x00c70100               ; first mixer connection
    dd 0x00c37180               ; mixer 0c input 1 (the analogue loopback)
                                ; muted; input 0, the DAC, is unmuted at 0 dB
                                ; by hda_amp_init below
    dd 0

; hda_amp_init's pairs: the caps parameter to read (0x12 output, 0x0d input),
; and the SET_AMP_GAIN_MUTE verb it ORs the 0 dB offset into. Payload bits:
; 15 output, 14 input, 13/12 left/right, 11-8 index, 7 mute, 6-0 gain.
hda_amp_tab:
    dd 0x002f0012, 0x0023b000   ; DAC 02, output amp
    dd 0x00cf000d, 0x00c37000   ; mixer 0c, input amp, index 0 (from DAC 02)
    dd 0x00cf0012, 0x00c3b000   ; mixer 0c, output amp (none on an ALC269:
                                ; the write is then ignored)
    dd 0                        ; the pins' are hda_pin_setup's

hda_services:
    dw SND_CAP_PCM_BG, 0, hda_stream, 0, hda_release_inst, hda_name, hda_tone
    dw 1 << SND_RT_PCM
    times DSV_PKGCALL - ($ - hda_services) db 0
    dw hda_pkgcall              ; HDADIAG's door (SPEC.md 34.11.1)
    times DSV_SIZE - ($ - hda_services) db 0
hda_name: db 'ALC269 HDA', 0

hda_release_inst:
    cmp al, [hda_gr_owner]
    jne .out
    cmp byte [hda_active], 0
    je .grant
    call hda_stream_stop
.grant:
    call hda_grant_drop
.out:
    ret

hda_up:        db 0
hda_clip:      db 0
hda_active:    db 0
hda_wtask:     db 0
hda_gen:       db 0
hda_state:     db 0
hda_ring:      db 0
hda_eof:       db 0
hda_tail:      db 0
hda_lastper:   db 0
hda_tone_on:   db 0
hda_probe_on:  db 0             ; HDAV_PROBE owns the engine
hda_pattern:   db 0             ; ...and which pattern
hda_dbg_rst1:  dw 0             ; hda_hw_start's record, for HDADIAG
hda_dbg_rst0:  dw 0
hda_dbg_ctl:   db 0
hda_dbg_lpib:  dw 0
hda_dbg_ps:    dw 0             ; the AFG's power state probe 2 last read
hda_fmt:       dw HDA_FMT       ; the stream's format, SD FMT and the DAC's
                                ; alike; a probe may try another, and its
                                ; stop puts HDA_FMT back
hda_oflags:    db 0
hda_gr_live:   db 0
hda_gr_owner:  db 0
hda_dmaseg:    dw 0
hda_dmaraw:    dw 0         ; the claim as made; hda_dmaseg is it aligned
hda_gr_seg:    dw 0
hda_gr_size:   dw 0
hda_src_off:   dw 0
hda_src_max:   dw 0
hda_rate:      dw 0
hda_phase:     dw 0
hda_total:     dw 0
hda_fed:       dw 0
hda_consumed:  dw 0
hda_rmask:     dw 0
hda_sd:        dw 0
hda_pci_orig:  dw 0xffff
hda_bar:       dd 0
hda_pins:      times HDA_MAXPINS db 0   ; hda_pin_scan's finds, then their
hda_pinfl:     times HDA_MAXPINS db 0   ; flags (bit 0 headphone) - one block,
hda_npins:     db 0                     ; HDAV_INFO copies both at once
hda_gpio:      db 0                     ; the GPIO mask the SKU asked for, or 0
hda_sku:       dw 0                     ; the SKU word hda_rt_init believed, or 0
hda_coef0:     dw 0                     ; COEF 0: the variant in bits 7:4
hda_def_saved: db 0
hda_coef_def:  times 32 dw 0            ; COEFs 00-1f as the link reset left
                                        ; them, before hda_rt_init's writes

hda_gdt:
    dq 0
    dw 0xffff, 0, 0x9200, 0x00cf
hda_gdt_end:
hda_gdtr:
    dw hda_gdt_end - hda_gdt - 1
    dd 0

    OS88_DRV_END
