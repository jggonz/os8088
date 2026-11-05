; =============================================================================
; os8088 - apps/mousechk/ps2.inc - PS/2: the boot record (SPEC.md 126.7)
;
; The section "PS/2 mouse". What the kernel's own probe (SPEC.md 9.9.1) left
; in the 'MO' block's PS/2 span (SPEC.md 9.4.4, moblk.inc's MOP_*), in words;
; what the BIOS thinks (the equipment word's bit 2, the EBDA's pointing-device
; bytes); what the machine looks like NOW (port 64h, the three mask bits,
; int 74h's vector); and SPEC.md 9.9.6's decision table applied to what a
; SHIPPED kernel can see, as one line the user can act on.
;
; NOTHING HERE WRITES A PORT. 64h status and the two 8259 mask registers are
; reads with no side effect, and none is read on CPU_8086, where there is no
; 8042 and 64h is not decoded (SPEC.md 9.9.2). The live probe, which does talk
; to the 8042, is probe.inc's, behind p2_canprobe's refusals (SPEC.md 126.8).
;
;   p2_init      once at open: CPU tier, the span's address, two low windows
;   p2_rows      the section, every rebuild (ES = KERNEL_SEG, DS = package)
;   p2_canprobe  out CF = 0 may probe; CF = 1 refused, SI = a NUL reason
;   p2_probe     the live probe, or (pressed again soon) its BIOS leg
; =============================================================================

P2_KSEG     equ 0x0060          ; KERNEL_SEG as the IVT holds it (real mode)
P2_LOWP     equ 0x001D          ; the low window's paragraph: int 74h's cell
P2_LOWN     equ 0x0330          ; ...to the BDA's end (0x1D0 .. 0x4FF)
P2_V74      equ 0               ; int 74h's vector, in that window
P2_EQUIP    equ 0x410 - 0x1D0   ; the BDA's equipment word (int 11h's answer)
P2_EBDAW    equ 0x40E - 0x1D0   ; the BDA's EBDA segment word
P2_EBN      equ 0x28            ; EBDA bytes the window reaches

; -----------------------------------------------------------------------------
; p2_init - once at open. Keeps every register (the shell's BX is a window)
; -----------------------------------------------------------------------------
p2_init:
    push ax
    push bx
    push cx
    push dx
    push es
    mov [p2_kseg], es               ; the kernel as WE were handed it (a
                                    ; selector on kern_mega), so p2_canprobe
                                    ; never depends on its caller's ES
    call OSAPI_CPU_INFO             ; AL = the tier, AH = CPU_F_*
    mov [p2_tier], al
    mov [p2_cpuf], ah
    mov ax, DBG_TAG_MOUSE
    call mc_dbgfind
    jc .nomo
    mov ax, [es:bx+MOB_P2]
    mov [p2_sp], ax
.nomo:
    ; The IVT cell and the BDA through ONE window: a real-mode kernel answers
    ; the paragraph itself, kern_mega a data selector over it (SPEC.md
    ; 50.6.11). Paragraph 0 is not asked for: on kern_mega 0 is the null
    ; selector.
    mov dx, P2_LOWP
    mov cx, P2_LOWN
    call p2_win
    jc .out
    mov [p2_lowsel], dx
    inc byte [p2_lowok]
    cmp byte [p2_tier], CPU_8086    ; on an XT 40:0E is LPT4's port, not an
    je .out                         ; EBDA's segment
    mov es, dx
    mov dx, [es:P2_EBDAW]
    cmp dx, 0x4000                  ; an EBDA lives under 640KB and above
    jb .out                         ; anything os8088 boots on
    cmp dx, 0xA000
    jae .out
    mov [p2_ebseg], dx
    mov cx, P2_EBN
    call p2_win
    jc .out
    mov [p2_ebsel], dx
    inc byte [p2_ebok]
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; p2_win - DX = a paragraph, CX = bytes: DX = a window over it, CF = 1 none
p2_win:
    xor ax, ax
    mov bl, SEG_WIN
    call OSAPI_SEG                  ; (a far call: never jmp to a slot)
    ret

; -----------------------------------------------------------------------------
; p2_canprobe - may Probe PS/2 run? Cheap and side-effect free: the shell asks
; on every button paint and every tick (SPEC.md 126.3).
; out: CF = 0 yes; CF = 1 no, SI = the reason (24 characters at most: it is
;      toasted). Every other register kept.
;
; THE RULE (SPEC.md 126.8.1): the kernel's device is never touched.
;   - CPU_8086: no 8042, and 64h is not decoded (SPEC.md 9.9.2)
;   - 64h reads FFh: no controller answers there either
;   - no 'MO' record: the kernel's claim on the port cannot be known
;   - [mou_p2] set: the PS/2 mouse is LIVE, the kernel's pointer
;   - [mou_p2st] = 9: the handshake completed. A serial mouse may have won
;     since and mou_p2_off retired it, but the device is the kernel's, and
;     its record already says everything a probe could
;   - and, whatever the record says, int 74h pointing into the kernel with
;     IRQ12 open at the slave: the kernel's ISR is armed
; Step 8 (the vector installed, the enable failed) is ALLOWED: .fail left
; IRQ12 masked, the probe keeps it masked and never writes the vector, and
; that failure is exactly what a trace exists to explain.
; -----------------------------------------------------------------------------
p2_canprobe:
    push ax
    push bx
    push es
    mov si, p2_r_xt
    cmp byte [p2_tier], CPU_8086
    je .no
    in  al, 0x64                    ; a status read, no side effect: FFh is
    mov si, p2_r_ff                 ; no controller at all (every bit, IBF
    cmp al, 0xFF                    ; and the error bits too, is never a
    je .no                          ; real 8042's answer), and nothing is
    mov si, p2_r_norec              ; written where nothing is listening
    mov bx, [p2_sp]
    or bx, bx
    jz .no
    mov es, [p2_kseg]
    mov si, p2_r_live
    cmp byte [es:bx+MOP_P2], 0
    jne .no
    mov si, p2_r_found
    cmp byte [es:bx+MOP_ST], 9
    jae .no
    cmp byte [p2_lowok], 0
    je .yes
    mov es, [p2_lowsel]
    cmp word [es:P2_V74+2], P2_KSEG
    jne .yes
    in  al, 0xA1                    ; the slave's mask: a read, no side effect
    test al, 0x10
    mov si, p2_r_armed
    jz .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; p2_probe - the button (SPEC.md 126.8). The live probe; pressed again within
; P2X_AGAIN ticks of one that finished, the opt-in BIOS leg instead
; -----------------------------------------------------------------------------
p2_probe:
    call p2x_again                  ; CF = 0: the BIOS leg is on offer
    jnc p2x_bios
    jmp p2x_run

; -----------------------------------------------------------------------------
; p2_rows - the section
; -----------------------------------------------------------------------------
p2_rows:
    mov si, p2_s_head
    mov al, LP_INFO
    call mc_head
    cmp byte [p2_tier], CPU_8086
    jne .at
    mov si, p2_rv_xt
    call p2_rv
    mov al, LP_NA
    jmp mc_lamp

.at:
    mov bx, [p2_sp]
    or bx, bx
    jnz .rec
    mov si, p2_rv_norec
    call p2_rv
    jmp short .warn

.rec:
    mov di, p2_rec                  ; the span, + mou_p2cmdr one past it
    mov cx, MOP_SIZE + 1
.cp:
    mov al, [es:bx]
    mov [di], al
    inc bx
    inc di
    loop .cp
    cmp byte [mc_kok], 0
    jne .dec
    mov si, p2_rv_raw               ; O1: another build, so raw bytes only
    call p2_rv
    mov si, p2_rec
    mov cl, MOP_SIZE
.rw:
    lodsb
    call mc_hex8
    mov al, ' '
    call mc_putc
    loop .rw
.warn:
    mov al, LP_WARN
    call mc_lamp
    jmp .live

.dec:
    ; --- how far the kernel's handshake got (SPEC.md 9.9.1) ----------------
    mov si, p2_rv_kp
    call p2_rv
    mov bl, [p2_rec+MOP_ST]
    cmp bl, 9
    jbe .stok
    mov bl, 7                       ; (a value no build writes: "?")
.stok:
    mov al, bl
    add al, '0'
    call mc_putc
    mov si, p2_s_of9
    call mc_put
    call p2_stword

    cmp byte [p2_rec+MOP_ST], 7     ; the ID is banked past step 6
    jb .noid
    mov si, p2_rv_id
    call p2_rv
    mov al, [p2_rec+MOP_ID]
    call mc_hex8
    mov si, p2_s_id0
    or al, al
    jz .idw
    mov si, p2_s_id3
    cmp al, 3
    je .idw
    mov si, p2_s_id4
    cmp al, 4
    jne .noid
.idw:
    call mc_put
.noid:

    cmp byte [p2_rec+MOP_ST], 9
    jne .nopk
    mov si, p2_rv_pkt               ; byte 0's bit 3 is always set, so a
    call p2_rv                      ; synced byte 0 is never 0 (SPEC.md 9.9.3)
    mov al, [p2_rec+MOP_B0]
    call mc_hex8
.nopk:

    cmp byte [p2_rec+MOP_ST], 2     ; the command byte was read at step 1
    jb .nocmd
    mov si, p2_rv_cmd
    call p2_rv
    mov al, [p2_rec+MOP_CMD0+1]     ; mou_p2cmdr, the RAW byte (SPEC.md
    call mc_hex8                    ; 126.7.2): readable because this IS the
    call p2_cmdline                 ; build the package was made with
.nocmd:

    ; --- the verdict: SPEC.md 9.9.6's table, as far as a shipped kernel sees
    mov bl, [p2_rec+MOP_ST]
    cmp bl, 9
    jb .vt
    mov si, p2_v_lost
    cmp byte [p2_rec+MOP_P2], 0
    je .vs
    mov si, p2_v_live
    test byte [p2_rec+MOP_B0], 0x08
    jnz .vs
    mov si, p2_v_wait
    jmp short .vs
.vt:
    xor bh, bh
    shl bx, 1
    mov si, [p2_vtab+bx]
.vs:
    call p2_vlist                   ; its lamp, then its lines
    cmp byte [p2_rec+MOP_ST], 9
    jae .live
    call p2_equip                   ; ...and the BIOS saw one at POST
    test al, 0x04
    jz .live
    mov si, p2_v_bios
    call p2_vlist

    ; --- the BIOS's opinion and the machine as it is now -------------------
.live:
    mov si, p2_rv_eq
    call p2_rv
    call p2_equip
    mov si, p2_s_eq1
    test al, 0x04
    jnz .eq
    mov si, p2_s_eq0
.eq:
    call mc_put

    cmp byte [p2_ebok], 0
    je .noeb
    mov si, p2_rv_eb                ; IBM's layout: 26h/27h the flags, 22h
    call p2_rv                      ; the int 15h C207 handler. Other BIOSes
    mov ax, [p2_ebseg]              ; may differ: shown, not decoded
    call mc_hex16
    push es
    mov es, [p2_ebsel]
    mov bx, 0x26
.ebf:
    mov al, ' '
    call mc_putc
    mov al, [es:bx]
    call mc_hex8
    inc bx
    cmp bl, 0x28
    jb .ebf
    mov al, ' '
    call mc_putc
    mov ax, [es:0x24]
    mov dx, [es:0x22]
    pop es
    call p2_farp
.noeb:

    cmp byte [p2_lowok], 0
    je .novec
    mov si, p2_rv_v74
    call p2_rv
    push es
    mov es, [p2_lowsel]
    mov bx, [es:P2_V74+2]
    mov dx, [es:P2_V74]
    pop es
    mov ax, bx
    call p2_farp
    mov si, p2_vcls                 ; whose it is, by its segment: the first
.vc:                                ; row whose floor it reaches
    lodsw
    cmp bx, ax
    lodsw                           ; (lodsw leaves the flags)
    jb .vc
    xchg ax, si
    call mc_put
.novec:

    mov si, p2_rv_irq               ; the three mask bits, read
    call p2_rv
    mov si, p2_mtab
.m:
    lodsw                           ; AL = the port, AH = the bit
    or al, al
    jz .md
    xor dx, dx
    mov dl, al
    in  al, dx
    push si
    mov si, p2_s_open
    test al, ah
    jz .mo
    mov si, p2_s_shut
.mo:
    call mc_put
    pop si
    jmp short .m
.md:

    mov si, p2_rv_64                ; port 64h, live: a status read
    call p2_rv
    in  al, 0x64
    call mc_hex8
    mov si, p2_s_ff
    cmp al, 0xFF
    je .s64
    mov ah, al
    mov si, p2_ftab
.f:
    lodsb
    or al, al
    jz .fd
    test ah, al
    jz .fs
    call mc_put
.fs:
    lodsb
    or al, al
    jnz .fs
    jmp short .f
.fd:
    mov si, p2_s_idle
    test ah, 0x03
    jnz .s3
.s64:
    call mc_put
.s3:
    jmp p2x_rows                    ; the probe's trace, or its offer

; p2_rv - SI = 'label', 0, 'value', 0: a row with that label and value
p2_rv:
    call mc_row
.n:
    lodsb
    or al, al
    jnz .n
    jmp mc_put

; p2_stword - BL = a step (0-9): its words. Keeps every register but SI
p2_stword:
    push bx
    xor bh, bh
    shl bx, 1
    mov si, [p2_stw+bx]
    pop bx
    jmp mc_put

; p2_farp - "AX:DX", AX the segment
p2_farp:
    call mc_hex16
    mov al, ':'
    call mc_putc
    mov ax, dx
    jmp mc_hex16

; p2_equip - AL = the BDA equipment word's low byte (int 11h's answer); 0 when
; the low window is not ours. Keeps every other register
p2_equip:
    xor al, al
    cmp byte [p2_lowok], 0
    je .out
    push es
    mov es, [p2_lowsel]
    mov al, [es:P2_EQUIP]
    pop es
.out:
    ret

; p2_vlist - SI = a verdict: db lamp, then NUL lines, then a second NUL
p2_vlist:
    lodsb
    call mc_lamp
.l:
    cmp byte [si], 0
    je .out
    call mc_line
.n:
    lodsb
    or al, al
    jnz .n
    jmp short .l
.out:
    ret

; p2_cmdline - the command byte in AL, in words. SPEC.md 9.9.7: bit 5 is the
; aux clock on a PS/2 8042 and PC MODE on an AT one, so it gets its own line
p2_cmdline:
    mov ah, al
    mov si, p2_s_none
    call mc_line
    mov si, p2_ctab
.c:
    lodsb                           ; the bit
    or al, al
    jz .b5
    call mc_put                     ; its name
.s:
    inc si
    cmp byte [si-1], 0
    jne .s
    push si
    mov si, p2_s_on
    test ah, al
    jnz .p
    mov si, p2_s_off
.p:
    call mc_put
    pop si
    jmp short .c
.b5:
    test ah, 0x20
    jz .out
    mov si, p2_s_b5
    jmp mc_line
.out:
    ret

; --- strings and tables -------------------------------------------------------
p2_s_head:  db 'PS/2 mouse', 0
p2_rv_xt:   db 'Controller', 0, 'none on an 8088', 0
p2_rv_norec: db 'Boot record', 0, 'not published', 0
p2_rv_raw:  db 'Boot record (raw)', 0, 0
p2_rv_kp:   db 'Kernel probe', 0, 'step ', 0
p2_s_of9:   db '/9 ', 0
p2_rv_id:   db 'Device ID', 0, 0
p2_s_id0:   db ' plain', 0
p2_s_id3:   db ' wheel', 0
p2_s_id4:   db ' 5-button', 0
p2_rv_pkt:  db 'Last byte 0', 0, 0
p2_rv_cmd:  db 'Command byte', 0, 0
p2_ctab:    db 0x01, 'IRQ1 ', 0, 0x02, ', IRQ12 ', 0, 0x40, ', xlate ', 0
            db 0x20, ', bit 5 ', 0, 0
p2_s_on:    db 'on', 0
p2_s_off:   db 'off', 0
p2_s_b5:    db '(bit 5: aux off, or PC mode on an AT)', 0
p2_rv_eq:   db 'int 11h bit 2', 0, 0
p2_s_eq1:   db '1: a mouse at POST', 0
p2_s_eq0:   db '0: none at POST', 0
p2_rv_eb:   db 'EBDA, 26h 27h 22h', 0, 0
p2_rv_v74:  db 'int 74h', 0, 0
p2_vcls:    dw 0xF000, p2_s_vrom, 0xC000, p2_s_vopt, P2_KSEG + 1, p2_s_vram
            dw P2_KSEG, p2_s_vk, 0, p2_s_vram
p2_s_vk:    db ' the kernel', 0
p2_s_vrom:  db ' BIOS ROM', 0
p2_s_vopt:  db ' option ROM', 0
p2_s_vram:  db ' RAM', 0
p2_rv_irq:  db 'IRQ 1, 2, 12', 0, 0
p2_mtab:    db 0x21, 0x02, 0x21, 0x04, 0xA1, 0x10, 0
p2_s_open:  db 'open ', 0
p2_s_shut:  db 'shut ', 0
p2_rv_64:   db 'Port 64h', 0, 0
p2_s_ff:    db ' no 8042', 0
p2_ftab:    db 0x02, ' IBF', 0, 0x01, ' OBF', 0, 0x20, ' aux', 0, 0
p2_s_idle:  db ' idle', 0
p2_s_none:  db 0

p2_r_xt:    db 'No 8042 on an 8088 PC', 0       ; (24 at most: toasted)
p2_r_ff:    db 'No 8042: 64h reads FF', 0
p2_r_norec: db 'No kernel mouse record', 0
p2_r_live:  db "Kernel's PS/2 mouse live", 0
p2_r_found: db 'Kernel already found it', 0
p2_r_armed: db "Kernel's IRQ12 is armed", 0

p2_stw:     dw p2_w0, p2_w1, p2_w2, p2_w3, p2_w4
            dw p2_w5, p2_w6, p2_w7, p2_w8, p2_w9
p2_w0:      db 'not run', 0
p2_w1:      db 'no command byte', 0
p2_w2:      db 'no aux port', 0
p2_w3:      db 'command write failed', 0
p2_w4:      db 'reset not ACKed', 0
p2_w5:      db 'no self-test AA', 0
p2_w6:      db 'no device ID', 0
p2_w7:      db '?', 0
p2_w8:      db 'enable failed', 0
p2_w9:      db 'armed', 0

; the verdicts (SPEC.md 126.7.3): db lamp, its lines, 0
p2_vtab:    dw p2_v0, p2_v1, p2_v2, p2_v3, p2_v4
            dw p2_v5, p2_v6, p2_v8, p2_v8
p2_v0:      db LP_WARN, 'Probe not in this kernel, or 0xAD refused.', 0, 0
p2_v1:      db LP_BAD, 'No command byte from the 8042.', 0, 0
p2_v2:      db LP_WARN, "No aux port, or it is off in BIOS Setup.", 0, 0
p2_v3:      db LP_BAD, 'The 8042 refused a command write.', 0, 0
p2_v4:      db LP_WARN, 'No reply to the reset: is a mouse in?', 0, 0
p2_v5:      db LP_BAD, 'Reset ACKed; its self-test failed.', 0, 0
p2_v6:      db LP_BAD, 'Self-test passed; no ID came.', 0, 0
p2_v8:      db LP_BAD, 'Reset OK; the IRQ12/0xF4 enable failed.', 0, 0
p2_v_live:  db LP_OK, "The PS/2 mouse is the pointer.", 0, 0
p2_v_wait:  db LP_WARN, 'Armed; no packet yet: move it.', 0, 0
p2_v_lost:  db LP_INFO, 'It answered; a serial mouse won.', 0, 0
p2_v_bios:  db LP_BAD, 'Yet the BIOS saw one at POST: report it.', 0, 0

; --- loader-zeroed bss --------------------------------------------------------
    OS88_ABYTE p2_tier              ; OSAPI_CPU_INFO's AL
    OS88_ABYTE p2_cpuf              ; ...and AH
    OS88_ABYTE p2_lowok             ; p2_lowsel is ours
    OS88_ABYTE p2_ebok              ; p2_ebsel is ours
    OS88_AWORD p2_kseg              ; ES as the entry proc was handed it
    OS88_AWORD p2_sp                ; the PS/2 span in KERNEL_SEG, 0 = none
    OS88_AWORD p2_lowsel            ; int 74h's cell + the BDA
    OS88_AWORD p2_ebsel             ; the EBDA's first P2_EBN bytes
    OS88_AWORD p2_ebseg             ; ...and its real segment
    OS88_ABUF  p2_rec, MOP_SIZE + 1 ; the span this rebuild, + mou_p2cmdr
