; =============================================================================
; os8088 - apps/mousechk/probe.inc - PS/2: the live probe (SPEC.md 126.8)
;
; Probe PS/2 runs the kernel's own handshake (SPEC.md 9.9.1) again, step by
; step, then the roads 9.9.1 did not take, recording each command, the 8042's
; status before and after it, and each reply byte or the ticks it waited for
; one that never came. p2x_rows prints that TRACE through the row API, and
; its last line names the first step where the kernel's road parts from a road
; that worked.
;
; THE DISCIPLINE IS THE KERNEL'S, step for step (mou_p2_init):
;   - IRQ1 masked at the master and IRQ12 at the slave for the whole probe,
;     banked and put back bit by bit, so no BIOS handler takes a reply
;   - 0xAD first, and every command byte written inside the window keeps
;     bit 4 SET, so int 09h is never live behind the probe's back
;   - the command byte banked with bit 4 cleared, and that RAW byte is what
;     goes back - never one with bit 5 forced (SPEC.md 9.9.7)
;   - waits in TICKS (OSAPI_GET_TICKS), 2 a step, the self-test's the same
;     short wait retried 5 times - never a spin count
;   - flushes after 0xAD and 0xA8, and before 0xAE
;   - 0xF4 is NEVER sent and IRQ12 NEVER armed: the device is not ours, and a
;     reset leaves it with reporting off
;   - every leg ends with A7 and the bank; the probe ends with 0xAE and both
;     mask bits as found, on every path - a timed-out step, a deaf controller
;
; It runs on the UI task inside a callback with the gfx lock held. IRQ0 is not
; masked, so [ticks] advances underneath and every wait ends.
; =============================================================================

P2X_MAX     equ 42              ; trace entries kept
P2X_ESZ     equ 9               ; bytes an entry
P2X_STN     equ 20              ; bytes of per-probe state (p2x_st)
P2X_TMO     equ 2               ; ticks a step may take (MOU_P2TMO)
P2X_BAT     equ 5               ; ...retried for the self-test (MOU_P2BAT)
P2X_AGAIN   equ 182             ; ticks the BIOS leg stays on offer (~10 s)

P2E_OP      equ 0               ; P2O_*
P2E_ARG     equ 1               ; the byte written, or the leg
P2E_S0      equ 2               ; 64h before
P2E_S1      equ 3               ; 64h after
P2E_FL      equ 4               ; P2F_*
P2E_R       equ 5               ; up to three replies
P2E_T       equ 8               ; ticks spent waiting for them

P2F_NR      equ 0x03            ; replies kept (0-3)
P2F_KBD     equ 0x04            ; << n: reply n came WITHOUT 64h's aux bit
P2F_MORE    equ 0x20            ; more came than were kept
P2F_STALL   equ 0x40            ; the 8042 never took a write (IBF stuck)
P2F_TMO     equ 0x80            ; a reply never came (the BIOS leg: CF set)

P2O_LEG     equ 0               ; a leg's heading; ARG = the leg (1-6)
P2O_AD      equ 1
P2O_RD      equ 2
P2O_A8      equ 3
P2O_RST     equ 4
P2O_GID     equ 5
P2O_A9      equ 6
P2O_AE      equ 7
P2O_WAIT    equ 8
P2O_WC      equ 9               ; from here the ARG is printed after the name
P2O_REST    equ 10
P2O_BIOS    equ 11

RW_STALL    equ 1               ; a leg's result (p2x_res), a p2x_rwt string
RW_NOCMD    equ 2
RW_NOAUX    equ 3
RW_A8DEAD   equ 4
RW_WFAIL    equ 5
RW_NOACK    equ 6
RW_NOBAT    equ 7
RW_NOID     equ 8
RW_MOUSE    equ 9               ; ...+ the ID
RW_SKIP     equ 10
RW_B5CLR    equ 11
RW_B5SET    equ 12
RW_A9OK     equ 13
RW_A9BAD    equ 14
RW_A9NONE   equ 15
RW_DONE     equ 16
RW_SILENT   equ 0x80            ; ...and then the 8042 did not answer 0x20

; =============================================================================
; the 8042, one byte at a time (mou_p2wr / mou_p2rd's contracts, in ticks)
; =============================================================================

; p2x_poll - wait until ((64h XOR CH) AND CL) is not 0, P2X_TMO ticks at most.
; out AL = 64h, CF = 1 timed out (its ticks go on the open entry). Keeps the
; rest
p2x_poll:
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
.w:
    in  al, 0x64
    push ax
    xor al, ch
    test al, cl
    pop ax
    jnz .out                        ; (test clears CF: the ready answer)
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, P2X_TMO
    jb .w
    call p2x_tick
    stc
.out:
    pop dx
    ret

; p2x_wr - wait for IBF clear. CF = 1 timed out. Keeps every register
p2x_wr:
    push ax
    push cx
    mov cx, 0x0202
    call p2x_poll
    pop cx
    pop ax
    ret

; p2x_rd - the next byte the 8042 offers. out AL = it, AH = 64h as offered
; (bit 5: the AUX side's), CF = 1 none came. Keeps every other register
p2x_rd:
    push cx
    mov cx, 0x0001
    call p2x_poll
    jc .out
    mov ah, al
    in  al, 0x60
.out:
    pop cx
    ret

; p2x_tick - AL more ticks on the open entry, saturating. Keeps everything
p2x_tick:
    push bx
    mov bx, [p2x_cur]
    add [bx+P2E_T], al
    jnc .k
    mov byte [bx+P2E_T], 0xFF
.k:
    pop bx
    ret

; p2x_cmd / p2x_dat - AL to 64h / to 60h. CF = 1: the 8042 never took it, and
; the probe treats it as wedged from here on. Keep everything
p2x_cmd:
    call p2x_wr
    jc p2x_stall
    out 0x64, al
    ret
p2x_dat:
    call p2x_wr
    jc p2x_stall
    out 0x60, al
    ret
p2x_stall:
    push ax
    mov al, P2F_STALL
    call p2x_fl
    mov byte [p2x_wedge], 1
    pop ax
    stc
    ret

; p2x_get - one reply onto the open entry. out AL = it, CF = 1 none came
; (flagged). Keeps AH and the rest
p2x_get:
    push bx
    mov bh, ah
    call p2x_rd
    jc .t
    call p2x_put
    mov ah, bh
    pop bx
    clc
    ret
.t:
    mov al, P2F_TMO
    call p2x_fl
    mov ah, bh
    pop bx
    stc
    ret

; p2x_flush - empty the output buffer now, onto the open entry: bounded, no
; wait (mou_p2flush's shape). Keeps everything
p2x_flush:
    push ax
    push cx
    mov cx, 16
.f:
    in  al, 0x64
    test al, 0x01
    jz .out
    mov ah, al
    in  al, 0x60
    call p2x_put
    loop .f
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; the trace store
; =============================================================================

; p2x_beg - open an entry: AL = P2O_*, AH = its ARG; 64h read now as the
; "before". A full store writes into p2x_scr. Keeps everything
p2x_beg:
    push ax
    push cx
    push di
    mov di, p2x_scr
    mov cl, [p2x_n]
    cmp cl, P2X_MAX
    jae .set
    inc byte [p2x_n]
    push ax
    mov al, P2X_ESZ
    mul cl
    add ax, p2x_tab
    xchg ax, di
    pop ax
.set:
    mov [p2x_cur], di
    mov [di+P2E_OP], ax             ; OP, then ARG
    in  al, 0x64
    mov [di+P2E_S0], al
    mov [di+P2E_S1], al
    xor ax, ax
    mov [di+P2E_FL], ax             ; FL and the first reply
    mov [di+P2E_R+1], ax
    mov [di+P2E_T], al
    pop di
    pop cx
    pop ax
    ret

; p2x_end - close the entry: 64h as the "after". Keeps every register AND the
; flags, so a step's CF survives it
p2x_end:
    pushf
    push ax
    push bx
    mov bx, [p2x_cur]
    in  al, 0x64
    mov [bx+P2E_S1], al
    pop bx
    pop ax
    popf
    ret

; p2x_fl - OR AL into the open entry's flags. Keeps everything
p2x_fl:
    push bx
    mov bx, [p2x_cur]
    or [bx+P2E_FL], al
    pop bx
    ret

; p2x_put - reply AL, offered with status AH, onto the open entry. Keeps all
p2x_put:
    push bx
    push cx
    push di
    mov di, [p2x_cur]
    mov bl, [di+P2E_FL]
    mov cl, bl
    and cl, P2F_NR
    cmp cl, 3
    jb .keep
    or bl, P2F_MORE
    jmp short .st
.keep:
    xor ch, ch
    add di, cx
    mov [di+P2E_R], al
    test ah, 0x20                   ; AUXB: the aux side's byte
    jnz .aux
    mov ch, P2F_KBD
    shl ch, cl
    or bl, ch
.aux:
    inc bx                          ; one more kept (bits 0-1)
.st:
    mov di, [p2x_cur]
    mov [di+P2E_FL], bl
    pop di
    pop cx
    pop bx
    ret

; =============================================================================
; the steps, an entry each. Every one: CF = 1 it failed
; =============================================================================

; p2x_cfl - entry AL, the 8042 command AH, then a flush
p2x_cfl:
    call p2x_beg
    push ax
    mov al, ah
    call p2x_cmd
    jc .e
    call p2x_flush                  ; (clears CF)
.e:
    pop ax
    jmp p2x_end

p2x_a8:
    mov ax, P2O_A8 | (0xA8 << 8)
    jmp short p2x_cfl
p2x_ad:
    mov ax, P2O_AD | (0xAD << 8)
    jmp short p2x_cfl

; p2x_ae - 0xAE, flushing FIRST so a keystroke arriving as the keyboard comes
; back is not drained away (mou_p2_init's order)
p2x_ae:
    mov ax, P2O_AE
    call p2x_beg
    call p2x_flush
    mov al, 0xAE
    call p2x_cmd
    jmp p2x_end

; p2x_rdcmd - 0x20 and its answer. out AL = the command byte
p2x_rdcmd:
    mov ax, P2O_RD
    call p2x_beg
    mov al, 0x20
    call p2x_cmd
    jc .e
    call p2x_get
.e:
    jmp p2x_end

; p2x_wk - the kernel's step 3 byte: the bank, aux clock on (bit 5 clear),
; IRQ12 off (bit 1 clear), the keyboard held off (bit 4 SET) - and on into
; p2x_wcmd, which writes AL as the command byte (0x60, then AL)
p2x_wk:
    mov al, [p2x_bank]
    and al, 0xDD
    or  al, 0x10
p2x_wcmd:
    push ax
    mov ah, al
    mov al, P2O_WC
    call p2x_beg
    mov al, 0x60
    call p2x_cmd
    jc .e
    mov al, ah
    call p2x_dat
.e:
    pop ax
    jmp p2x_end

; p2x_devc - AL to the AUX DEVICE (0xD4, then AL) and its first reply in AL
p2x_devc:
    push ax
    mov al, 0xD4
    call p2x_cmd
    pop ax
    jc .e
    call p2x_dat
    jc .e
    call p2x_get
.e:
    ret

; p2x_reset - D4 FF: the ACK, the self-test (the short wait retried
; P2X_BAT times) and the ID. out AL = the kernel's step it reached (4 no ACK,
; 5 no AA, 6 no ID, 7 all three), BL = the RW_* result. A device reply that
; lacks 64h's aux bit sets p2x_kbd: the reset reached the keyboard
p2x_reset:
    mov ax, P2O_RST
    call p2x_beg
    mov al, 0xFF
    call p2x_devc
    mov bx, 4 | (RW_NOACK << 8)
    jc .out
    cmp al, 0xFA
    jne .out
    mov bx, 5 | (RW_NOBAT << 8)
    mov cx, P2X_BAT
.bat:
    call p2x_rd                     ; a timeout here is not yet a failure,
    jnc .b                          ; so it is not flagged until the last
    loop .bat
    mov al, P2F_TMO
    call p2x_fl
    jmp short .out
.b:
    call p2x_put
    cmp al, 0xAA
    jne .out
    mov bx, 6 | (RW_NOID << 8)
    call p2x_get
    jc .out
    mov [p2x_lid], al
    mov bx, 7 | (RW_MOUSE << 8)
.out:
    call p2x_end
    call p2x_kchk
    mov al, bl
    mov bl, bh
    cmp byte [p2x_wedge], 0
    je .r
    mov bl, RW_STALL
.r:
    ret

; p2x_kchk - did the open entry take a reply from the keyboard side? Keeps all
p2x_kchk:
    push bx
    mov bx, [p2x_cur]
    test byte [bx+P2E_FL], P2F_KBD * 7
    jz .k
    mov byte [p2x_kbd], 1
.k:
    pop bx
    ret

; p2x_getid - D4 F2 and the ID behind its ACK: not the kernel's step, asked
; after a reset that worked, to see the same ID twice
p2x_getid:
    mov ax, P2O_GID
    call p2x_beg
    mov al, 0xF2
    call p2x_devc
    jc .e
    call p2x_get
.e:
    call p2x_end
    jmp short p2x_kchk

; p2x_fixid - after a reset: AL = 7 means the ID question is asked as well
p2x_fixid:
    cmp al, 7
    jne .r
    call p2x_getid
.r:
    ret

; p2x_restore - a leg's end: A7, then the bank with bit 4 SET (the keyboard
; stays off until the probe's own end), then a flush. The kernel's .fail, with
; the RAW byte. Keeps BX (a leg's result)
p2x_restore:
    mov al, [p2x_bank]
    or  al, 0x10
    mov ah, al
    mov al, P2O_REST
    call p2x_beg
    mov al, 0xA7
    call p2x_cmd
    jc .e
    mov al, 0x60
    call p2x_cmd
    jc .e
    mov al, ah
    call p2x_dat
    jc .e
    call p2x_flush
.e:
    jmp p2x_end

; p2x_leg - a leg's heading entry, AL = the leg. out CF = 1 the leg may not
; run (the 8042 is wedged, or nothing was banked), BL = RW_STALL then
p2x_leg:
    mov ah, al
    mov al, P2O_LEG
    call p2x_beg
    call p2x_end
    mov bl, RW_STALL
    cmp byte [p2x_wedge], 0
    jne .no
    cmp byte [p2x_hb], 1            ; CF = 1 when 0
    ret
.no:
    stc
    ret

; p2x_mask - bank both masks, then IRQ1 and IRQ12 off. p2x_mask1 is the second
; half alone, for the BIOS leg, whose bank is the probe's. p2x_unmask puts the
; two bits back as banked, BIT BY BIT: nothing else in either byte is ours
p2x_mask:
    in  al, 0x21
    mov [p2x_m1], al
    in  al, 0xA1
    mov [p2x_m2], al
p2x_mask1:
    mov ax, 0x0210
    jmp short p2x_mset
p2x_unmask:
    mov ah, [p2x_m1]
    mov al, [p2x_m2]
    and ax, 0x0210
p2x_mset:                           ; AH = IRQ1's bit, AL = IRQ12's
    pushf
    cli
    push ax
    in  al, 0x21
    and al, 0xFD
    or  al, ah
    out 0x21, al
    pop ax
    mov ah, al
    in  al, 0xA1
    and al, 0xEF
    or  al, ah
    out 0xA1, al
    popf
    ret

; =============================================================================
; p2x_run - the probe (SPEC.md 126.8.2). UI task, synchronous
; =============================================================================
p2x_run:
    push es
    push ds
    pop es
    mov di, p2x_st
    mov cx, P2X_STN
    xor al, al
    cld
    rep stosb
    pop es
    call p2x_mask

    ; --- leg 1: SPEC.md 9.9.1, unchanged, up to the commitment --------------
    mov al, 1
    call p2x_leg                    ; (refuses: nothing is banked yet. Its BL
    call p2x_ad                     ; and CF are not used here)
    mov bl, RW_STALL
    jc .l1
    inc byte [p2x_k]                ; 1
    call p2x_rdcmd
    mov bl, RW_NOCMD
    jc .l1                          ; no bank: nothing may be written back
    mov [p2x_c1], al                ; as read: bit 4 is our own 0xAD's
    mov ah, al
    and al, 0xEF
    mov [p2x_bank], al              ; THE RAW BYTE, bit 4 alone cleared
    inc byte [p2x_hb]
    inc byte [p2x_k]                ; 2
    test ah, 0x02                   ; bit 1: the BIOS armed IRQ12 here
    jnz .have
    test ah, 0x20                   ; ...or bit 5 already clear
    jz .have
    call p2x_a8                     ; no evidence: the fallback road
    mov bl, RW_STALL
    jc .l1r
    call p2x_rdcmd
    mov bl, RW_A8DEAD               ; the Phoenix case (SPEC.md 9.9.1)
    jc .l1r
    test al, 0x20
    mov bl, RW_NOAUX
    jnz .l1r
.have:
    call p2x_a8
    mov bl, RW_STALL
    jc .l1r
    inc byte [p2x_k]                ; 3
    call p2x_wk
    mov bl, RW_WFAIL
    jc .l1r
    call p2x_reset
    mov [p2x_k], al
    call p2x_fixid
.l1r:
    call p2x_restore
.l1:
    mov [p2x_res+0], bl

    ; --- leg 2: a reset with the keyboard off, whatever the byte said. Leg 1
    ; got to its reset already when it reached step 4, and this IS that
    mov al, 2
    call p2x_leg
    jc .l2
    mov bl, RW_SKIP
    cmp byte [p2x_k], 4
    jae .l2
    call p2x_wk
    mov bl, RW_WFAIL
    jc .l2r
    call p2x_a8
    mov bl, RW_STALL
    jc .l2r
    call p2x_reset
    call p2x_fixid
.l2r:
    call p2x_restore
.l2:
    mov [p2x_res+1], bl

    ; --- leg 3: the same reset with the keyboard interface ON. The one thing
    ; that differs from the kernel is step 1's bracket. IRQ1 stays masked, so
    ; a keystroke is READ here rather than stolen, and the trace marks it
    mov al, 3
    call p2x_leg
    jc .l3
    call p2x_wk
    mov bl, RW_WFAIL
    jc .l3r
    mov ax, P2O_AE | (0xAE << 8)
    call p2x_cfl
    mov bl, RW_STALL
    jc .l3r
    call p2x_a8
    jc .l3r
    mov ax, P2O_WAIT                ; what the keyboard queued while it was
    call p2x_beg                    ; off - the Probe key's release - drained
    mov cx, 4                       ; before the reset, so it is not read as
.dr:                                ; the reset's answer
    call p2x_rd
    jc .drd
    call p2x_put
    loop .dr
.drd:
    call p2x_end
    call p2x_reset
    call p2x_fixid
    call p2x_ad                     ; the keyboard off again
.l3r:
    call p2x_restore
.l3:
    mov [p2x_res+2], bl

    ; --- leg 4: 0xA8, then the byte again (9.9.1's fallback road). Seeing
    ; A8 work needs bit 5 SET before it, and setting it ourselves is PC MODE
    ; on an AT 8042 (SPEC.md 9.9.7): with it clear in the bank, skipped
    mov al, 4
    call p2x_leg
    jc .l4
    mov bl, RW_SKIP
    test byte [p2x_bank], 0x20
    jz .l4
    call p2x_a8
    mov bl, RW_STALL
    jc .l4r
    call p2x_rdcmd
    mov bl, RW_A8DEAD
    jc .l4r
    test al, 0x20
    mov bl, RW_B5SET
    jnz .l4r
    mov bl, RW_B5CLR
.l4r:
    call p2x_restore
.l4:
    mov [p2x_res+3], bl

    ; --- leg 5: 0xA9, the aux interface test, LAST: 9.9.1 records it leaving
    ; a controller deaf, and a deaf one costs every leg after it ------------
    mov al, 5
    call p2x_leg
    jc .l5
    mov ax, P2O_A9 | (0xA9 << 8)
    call p2x_beg
    mov al, ah
    call p2x_cmd
    jc .a9e                         ; (BL = RW_STALL from p2x_leg)
    call p2x_get
    mov bl, RW_A9NONE
    jc .a9e
    mov bl, RW_A9OK
    or  al, al
    jz .a9e
    mov bl, RW_A9BAD
.a9e:
    call p2x_end
    cmp bl, RW_STALL
    je .l5
    call p2x_rdcmd                  ; does it still answer?
    jnc .l5r
    or  bl, RW_SILENT
.l5r:
    call p2x_restore
.l5:
    mov [p2x_res+4], bl

    ; --- the end: the byte read once more, then put back as found ----------
    cmp byte [p2x_hb], 0
    je .ae
    call p2x_rdcmd
    jc .noc2
    mov [p2x_c2], al
    inc byte [p2x_v]
.noc2:
    mov al, [p2x_bank]              ; bit 4 clear: the keyboard's own way
    call p2x_wcmd                   ; back, and 0xAE says it again
.ae:
    call p2x_ae
    call p2x_unmask
    call OSAPI_GET_TICKS
    or  al, 1                       ; a stamp, never 0
    mov [p2x_t0], ax
    ret

; p2x_again - is the BIOS leg on offer? CF = 0 yes. Keeps every register
p2x_again:
    push ax
    cmp byte [p2x_bdone], 0
    jne .no
    cmp byte [p2x_hb], 0
    je .no
    test byte [p2_cpuf], CPU_F_PM   ; kern_mega: not offered (SPEC.md 126.8.4)
    jnz .no
    call OSAPI_GET_TICKS
    sub ax, [p2x_t0]
    cmp ax, P2X_AGAIN               ; CF = 1 (on offer) while below
    cmc
    jmp short .out
.no:
    stc
.out:
    pop ax
    ret

; =============================================================================
; p2x_bios - the opt-in leg (SPEC.md 126.8.4): the BIOS's own int 15h AX=C2xx
; interface, with the machine as the probe left it - which is as it found it -
; then the controller put back our way. Never C207 (no handler is ours to
; give it) and never C200 BH=1 (enable): initialise, reset, type, disable
; =============================================================================
p2x_bios:
    inc byte [p2x_bdone]
    mov al, 6
    call p2x_leg
    mov si, p2x_btab
.call:
    lodsw                           ; AL = the function, AH = the BH it takes
    cmp al, 0xFF
    je .done
    push si
    xchg al, ah
    mov bh, al                      ; BH = its parameter
    mov al, P2O_BIOS                ; ARG (AH) = the function
    call p2x_beg
    mov al, ah
    mov ah, 0xC2
    push es
    push ds
    push bp
    int 0x15
    pop bp
    pop ds
    pop es
    pushf
    mov dx, bx
    mov al, ah
    mov ah, 0x20                    ; (never a keyboard-side byte)
    call p2x_put                    ; AH, the status...
    mov al, dh
    call p2x_put                    ; ...BH...
    mov al, dl
    call p2x_put                    ; ...BL
    popf
    jnc .ok
    mov al, P2F_TMO
    call p2x_fl
.ok:
    call p2x_end
    pop si
    jmp short .call
.done:
    mov byte [p2x_res+5], RW_DONE
    call p2x_mask1                  ; the masks back to the PROBE's bank below
    call p2x_ad
    call p2x_rdcmd                  ; what the BIOS left: on the trace
    call p2x_restore
    mov al, [p2x_bank]
    call p2x_wcmd
    call p2x_ae
    jmp p2x_unmask

p2x_btab:   db 0x05, 3, 0x01, 0, 0x04, 0, 0x00, 0, 0xFF, 0

; =============================================================================
; p2x_rows - the trace, or what the button would do
; =============================================================================
p2x_rows:
    cmp byte [p2x_n], 0
    jne .have
    call p2_canprobe
    jc .refused
    mov si, p2x_s_offer
    jmp mc_line
.refused:
    push si
    mov si, p2x_s_off
    call mc_line
    pop si
    jmp mc_put

.have:
    mov si, p2x_s_thead
    call mc_line
    xor cx, cx
.e:
    cmp cl, [p2x_n]
    jae .verdict
    push cx
    mov al, P2X_ESZ
    mul cl
    add ax, p2x_tab
    xchg ax, bx
    call p2x_row
    pop cx
    inc cx
    jmp short .e

.verdict:
    ; --- did the controller come back as it was? (SPEC.md 126.8.3) -------
    mov si, p2x_s_cb
    call mc_line
    cmp byte [p2x_hb], 0
    je .vno
    mov al, [p2x_c1]
    call mc_hex8
    mov al, '>'
    call mc_putc
    mov si, p2x_s_gone
    cmp byte [p2x_v], 0
    je .vbad
    mov al, [p2x_c2]
    call mc_hex8
    mov si, p2x_s_rest
    cmp al, [p2x_c1]
    je .v0
    mov si, p2_s_none
.vbad:
    call mc_put
    mov si, p2x_s_nrest
    mov al, LP_BAD
    call mc_lamp
.v0:
    call mc_put
    jmp short .v1
.vno:
    mov si, p2x_s_nob
    call mc_put
.v1:

    ; --- the first step where the kernel's road parts from one that worked
    mov si, p2x_s_vh
    call mc_line
    mov bl, [p2x_k]
    cmp bl, 7
    jne .k1
    mov si, p2x_v_kok
    call mc_put
    mov al, [p2x_lid]
    call mc_hex8
    mov si, p2x_v_late
    cmp byte [p2_rec+MOP_ST], 8     ; the boot got past the reset: it is the
    jne .vl                         ; enable that failed there
    mov si, p2x_v_en
    jmp short .vl
.k1:
    mov si, p2x_v_kst
    call mc_put
    mov al, bl
    add al, '0'
    call mc_putc
    mov al, ' '
    call mc_putc
    call p2_stword
    mov si, p2x_v_l2
    cmp byte [p2x_res+1], RW_MOUSE
    je .vl
    mov si, p2x_v_l3
    cmp byte [p2x_res+2], RW_MOUSE
    je .vl
    mov si, p2x_v_aux
    mov al, [p2x_res+4]
    and al, 0x7F
    cmp al, RW_A9OK
    je .vl
    cmp byte [p2x_res+3], RW_B5CLR
    je .vl
    mov si, p2x_v_phx
    cmp byte [p2x_res+0], RW_A8DEAD
    je .vl
    mov si, p2_v2                   ; ps2.inc's: no aux port, check Setup
.vl:
    call p2_vlist
    mov si, p2x_v_kbd
    cmp byte [p2x_kbd], 0
    je .bios
    call p2_vlist
.bios:
    call p2x_again
    jc .out
    mov si, p2x_s_boff
    jmp mc_line
.out:
    ret

; p2x_row - entry BX as one line
p2x_row:
    mov al, [bx+P2E_OP]
    cmp al, P2O_LEG
    jne .step
    ; --- a leg's heading: "n title: result" ---------------------------------
    mov si, p2_s_none
    call mc_line
    mov al, [bx+P2E_ARG]
    push ax
    add al, '0'
    call mc_putc
    pop si
    and si, 0x00FF
    mov al, [p2x_res-1+si]
    shl si, 1
    mov si, [p2x_ltit-2+si]
    call mc_put
    push ax
    and al, 0x7F
    xor ah, ah
    xchg ax, si
    shl si, 1
    mov si, [p2x_rwt-2+si]
    call mc_put
    pop ax
    push ax
    and al, 0x7F
    cmp al, RW_MOUSE
    jne .nid
    mov al, [p2x_lid]
    call mc_hex8
.nid:
    pop ax
    test al, RW_SILENT
    jz .hd
    mov si, p2x_s_silent
    call mc_put
.hd:
    ret

.step:
    ; --- a step: name[arg] padded to 13; "st xx>yy"; replies; flags; ticks
    mov di, p2_lb
    cbw
    xchg ax, si
    shl si, 1
    mov si, [p2x_onm-2+si]
.nm:
    lodsb
    or al, al
    jz .nme
    mov [di], al
    inc di
    jmp short .nm
.nme:
    cmp byte [bx+P2E_OP], P2O_WC
    jb .pad
    mov al, [bx+P2E_ARG]
    call p2x_hexdi
.pad:
    mov byte [di], ' '
    inc di
    cmp di, p2_lb + 13
    jb .pad
    mov byte [di], 0
    mov si, p2_lb
    call mc_line
    mov al, [bx+P2E_S0]
    call mc_hex8
    mov al, '>'
    call mc_putc
    mov al, [bx+P2E_S1]
    call mc_hex8
    mov dl, [bx+P2E_FL]
    mov cl, dl
    and cx, P2F_NR
    jcxz .rd
    mov dh, P2F_KBD                 ; the first reply's keyboard bit
    mov di, bx
.r:
    mov al, ' '
    call mc_putc
    mov al, [di+P2E_R]
    call mc_hex8
    test dl, dh
    jz .ra
    cmp byte [bx+P2E_OP], P2O_RST   ; a DEVICE's reply from the keyboard
    je .rk                          ; side is marked. The controller's own
    cmp byte [bx+P2E_OP], P2O_GID   ; answers never carry the aux bit
    jne .ra
.rk:
    mov al, 'k'
    call mc_putc
.ra:
    shl dh, 1
    inc di
    loop .r
.rd:
    mov si, p2x_s_more
    test dl, P2F_MORE
    jz .f1
    call mc_put
.f1:
    mov si, p2x_s_stall
    test dl, P2F_STALL
    jz .f2
    call mc_put
.f2:
    test dl, P2F_TMO
    jz .f3
    mov si, p2x_s_cf
    cmp byte [bx+P2E_OP], P2O_BIOS
    je .f2p
    mov si, p2x_s_tmo
.f2p:
    call mc_put
.f3:
    mov al, [bx+P2E_T]
    or al, al
    jz .f4
    push ax
    mov al, ' '
    call mc_putc
    pop ax
    xor ah, ah
    call mc_dec
    mov al, 't'
    jmp mc_putc
.f4:
    ret

; p2x_hexdi - AL as two hex digits at DS:DI, DI past them
p2x_hexdi:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .n
    mov al, ah
    call .n
    pop cx
    pop ax
    ret
.n:
    and al, 0x0F
    add al, 0x90                    ; the classic four-instruction nibble
    daa
    adc al, 0x40
    daa
    mov [di], al
    inc di
    ret

; --- strings ------------------------------------------------------------------
p2x_s_off:   db 'Probe PS/2 is off: ', 0
p2x_s_offer: db 'Probe PS/2 (P) re-runs it live, traced.', 0
p2x_s_thead: db 'step         64h   replies  ticks', 0
p2x_s_silent: db ', then deaf', 0
p2x_s_more:  db ' +', 0
p2x_s_stall: db ' STALL', 0
p2x_s_tmo:   db ' none', 0
p2x_s_cf:    db ' CF', 0
p2x_s_cb:    db 'Command byte ', 0
p2x_s_rest:  db ': restored', 0
p2x_s_gone:  db '??', 0
p2x_s_nrest: db ' NOT RESTORED', 0
p2x_s_nob:   db 'never read', 0
p2x_s_vh:    db 'Verdict: ', 0
p2x_v_kok:   db "kernel's road works now, ID ", 0
p2x_v_kst:   db "kernel stops at ", 0
p2x_s_boff:  db 'Probe again within 10 s: BIOS int 15h.', 0

p2x_v_late: db LP_WARN, 'So only the boot failed: report it.', 0, 0
p2x_v_en:   db LP_WARN, 'So the boot failed at the enable.', 0, 0
p2x_v_l2:   db LP_WARN, 'A plain reset finds it: report it.', 0, 0
p2x_v_l3:   db LP_WARN, 'Found only with kbd ON: report it.', 0, 0
p2x_v_aux:  db LP_WARN, 'Aux port, no device: check the mouse.', 0, 0
p2x_v_phx:  db LP_WARN, 'Deaf after 0xA8 (Phoenix): report it.', 0, 0
p2x_v_kbd:  db LP_WARN, 'A reset reached the KEYBOARD (no D4).', 0, 0

p2x_ltit:   dw p2x_t1, p2x_t2, p2x_t3, p2x_t4, p2x_t5, p2x_t6
p2x_t1:     db ' kernel: ', 0
p2x_t2:     db ' reset, kbd off: ', 0
p2x_t3:     db ' reset, kbd on: ', 0
p2x_t4:     db ' A8, reread: ', 0
p2x_t5:     db ' A9 test: ', 0
p2x_t6:     db ' int 15h, AH BH BL: ', 0

p2x_rwt:    dw p2x_q1, p2_w1, p2_w2, p2x_q4, p2_w3, p2_w4, p2_w5
            dw p2_w6, p2x_q9, p2x_q10, p2x_q11, p2x_q12, p2x_q13
            dw p2x_q14, p2x_q15, p2x_q16
p2x_q1:     db 'the 8042 stalled', 0
p2x_q4:     db 'deaf after A8', 0
p2x_q9:     db 'a device, ID ', 0
p2x_q10:    db 'skipped', 0
p2x_q11:    db 'bit 5 cleared: a port', 0
p2x_q12:    db 'bit 5 stayed: no port', 0
p2x_q13:    db '00, lines good', 0
p2x_q14:    db 'a stuck line', 0
p2x_q15:    db 'no answer', 0
p2x_q16:    db 'done', 0

p2x_onm:    dw p2x_o1, p2x_o2, p2x_o3, p2x_o4, p2x_o5, p2x_o6
            dw p2x_o7, p2x_o8, p2x_o9, p2x_o10, p2x_o11
p2x_o1:     db 'AD kbd off', 0
p2x_o2:     db '20 read', 0
p2x_o3:     db 'A8 aux on', 0
p2x_o4:     db 'D4 FF reset', 0
p2x_o5:     db 'D4 F2 ID', 0
p2x_o6:     db 'A9 test', 0
p2x_o7:     db 'AE kbd on', 0
p2x_o8:     db 'drain', 0
p2x_o9:     db '60 cmd ', 0
p2x_o10:    db 'restore ', 0
p2x_o11:    db 'int15 C2', 0

; --- loader-zeroed bss --------------------------------------------------------
; The per-probe state is ONE buffer named by offsets, because p2x_run zeroes it
; with one rep stosb and OS88_ABUF may put the 512 boundary between two items.
    OS88_ABUF  p2x_st, P2X_STN
    OS88_ABUF  p2x_tab, P2X_MAX * P2X_ESZ
    OS88_ABUF  p2x_scr, P2X_ESZ     ; where a full store writes
    OS88_AWORD p2x_cur              ; the open entry
    OS88_AWORD p2x_t0               ; [ticks] when the probe ended (0 never)
    OS88_ABUF  p2_lb, 16            ; a trace line's left part
p2x_n       equ p2x_st + 0          ; entries
p2x_wedge   equ p2x_st + 1          ; the 8042 stopped taking writes
p2x_hb      equ p2x_st + 2          ; the command byte is banked
p2x_bank    equ p2x_st + 3          ; ...as read, bit 4 cleared
p2x_c1      equ p2x_st + 4          ; ...as read (after 0xAD)
p2x_c2      equ p2x_st + 5          ; ...as read at the end
p2x_v       equ p2x_st + 6          ; p2x_c2 came
p2x_k       equ p2x_st + 7          ; the kernel's step leg 1 reached
p2x_kbd     equ p2x_st + 8          ; a reset answered from the keyboard side
p2x_lid     equ p2x_st + 9          ; the last ID read
p2x_m1      equ p2x_st + 10         ; the masks as found
p2x_m2      equ p2x_st + 11
p2x_bdone   equ p2x_st + 12         ; the BIOS leg has run
p2x_res     equ p2x_st + 13         ; 6 bytes: a RW_* per leg
