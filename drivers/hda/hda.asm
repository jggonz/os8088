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

HDA_DMA_KB     equ 36
HDA_STAGE_KB   equ 32
HDA_BDL_OFF    equ 0x0000
HDA_RING_OFF   equ 0x0400
HDA_PERIOD     equ 8192
HDA_FRAMES     equ HDA_PERIOD / 4
HDA_PERIODS   equ 4
HDA_RING_BYTES equ HDA_PERIOD * HDA_PERIODS
HDA_RATE       equ 44100
HDA_FMT        equ 0x4011       ; 44.1 kHz, 16-bit, stereo

; controller registers
HDA_GCAP       equ 0x00
HDA_GCTL       equ 0x08
HDA_STATESTS   equ 0x0e
HDA_ICOI       equ 0x60
HDA_ICII       equ 0x64
HDA_ICIS       equ 0x68
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
hda_attach:
    cmp byte [hda_up], 0
    je .probe
    mov al, DRVE_TWICE
    stc
    ret
.probe:
    call hda_pci_probe
    jc .nohw
    mov ax, HDA_DMA_KB
    call OSAPI_MEM_CLAIM_HI
    jc .nomem
    mov [hda_dmaseg], dx
    call hda_bdl_init
    call hda_ctl_reset
    jc .undo
    call hda_codec_init
    jc .undo
    mov byte [hda_up], 1
    mov si, hda_services
    clc
    ret
.undo:
    mov dx, [hda_dmaseg]
    call OSAPI_MEM_FREE
    mov word [hda_dmaseg], 0
    call hda_pci_restore
.nohw:
    mov al, DRVE_HW
    stc
    ret
.nomem:
    call hda_pci_restore
    mov al, DRVE_MEM
    stc
    ret

hda_detach:
    push ax
    push cx
    push dx
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
    mov dx, [hda_dmaseg]
    or dx, dx
    jz .pci
    call OSAPI_MEM_FREE
    mov word [hda_dmaseg], 0
.pci:
    call hda_pci_restore
    pop dx
    pop cx
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
    cmp byte [hda_active], 0
    jne .busy
    cmp byte [hda_clip], 0
    jne .busy
    cmp byte [hda_tone_on], 0
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
    test ah, SND_OPENF_RING
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
    mov ax, cx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .space
    mov [hda_gr_seg], dx
    mov [hda_gr_size], bx       ; exact requested capacity, not KB rounding
    mov [hda_gr_owner], dh
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
    mov dx, ax
    mov es, [hda_dmaseg]
    mov di, HDA_RING_OFF
    mov ecx, HDA_RING_BYTES / 4
    mov eax, 0x40004000
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
    mov cx, 0xffff
.wait:
    call hda_rd32
    test al, 1
    jnz .up
    loop .wait
    stc
    ret
.up:
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
    mov bx, hda_codec_verbs
.next:
    mov eax, [bx]
    test eax, eax
    jz .ok
    call hda_verb
    jc .bad
    add bx, 4
    jmp .next
.ok:
    clc
    ret
.bad:
    stc
    ret

hda_verb:
    push bx
    push cx
    push si
    mov ebx, eax
    mov cx, 0xffff
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
    mov cx, 0xffff
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
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

hda_hw_start:
    call hda_hw_stop
    mov si, [hda_sd]
    add si, HDA_SD_CTL
    mov al, 1                   ; stream-descriptor reset, asserted then clear
    call hda_wr8
    call hda_pause
    xor al, al
    call hda_wr8
    call hda_pause
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
    mov ax, HDA_FMT
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
    mov eax, 0x00100002         ; stream tag 1, RUN
    mov si, [hda_sd]
    add si, HDA_SD_CTL
    call hda_wr32
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

hda_pause:
    push cx
    mov cx, 0xffff
.p:
    loop .p
    pop cx
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
hda_codec_verbs:
    dd 0x00170500, 0x00270500, 0x00c70500, 0x01470500, 0x01570500
    dd 0x00224011               ; converter format = HDA_FMT
    dd 0x00270610               ; stream tag 1, channel 0
    dd 0x00c70100               ; first mixer connection
    dd 0x01470100, 0x01570100
    dd 0x01470740, 0x01570740   ; output pins enabled
    dd 0x01470c02, 0x01570c02   ; external amplifier enabled
    dd 0x0023b000, 0x00c3b000, 0x0143b000, 0x0153b000
    dd 0

hda_services:
    dw SND_CAP_PCM_BG, 0, hda_stream, 0, hda_release_inst, hda_name, hda_tone
    dw 1 << SND_RT_PCM
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
hda_gr_live:   db 0
hda_gr_owner:  db 0
hda_dmaseg:    dw 0
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

hda_gdt:
    dq 0
    dw 0xffff, 0, 0x9200, 0x00cf
hda_gdt_end:
hda_gdtr:
    dw hda_gdt_end - hda_gdt - 1
    dd 0

    OS88_DRV_END
