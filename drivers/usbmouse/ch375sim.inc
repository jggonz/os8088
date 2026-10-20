; =============================================================================
; ch375sim.inc - a CH375B and one USB device, as a model (SPEC.md 9.12.6)
;
; `%include`d by usbmouse.asm IN PLACE OF ITS FOUR PORT PRIMITIVES when the
; gate build passes -DCH375SIM, and by nothing else: no emulator in this tree
; carries a CH375, so this is how the driver's enumeration, its state machine
; and the kernel's OSAPI_MOUSE_FEED run on an 8088 at all. The shipped
; USBMOUSE.DRV never assembles a byte of it.
;
; WHAT IT IS NOT: evidence about the chip. It is written from the same
; datasheet reading as the driver, so a misreading shared by both passes here
; and fails on a Book8088. What it does catch is everything else - a toggle
; that never flips, a descriptor walk that reads past its buffer, a state that
; never leaves, a button left down across an unplug, a feed the kernel
; refuses, a worker that does not die on detach.
;
; THE MAILBOX. tests/usbmouse.py finds the driver's segment through its
; drv_tab row and scans it for 'CH375SIM'; everything after the magic is at a
; fixed offset from it (SIM_* below). The test WRITES plug, nostat, the report
; and stalln, and READS the counters and the three driver-state offsets.
;
; The model is passive: it runs only when the driver touches a port, so every
; primitive first brings it up to date with the mailbox (sim_sync) - a plug
; or unplug the test posted, and a pending IN that a newly posted report can
; now complete.
;
; SIMBOOT picks the power-on state, because an ATTACH happens at boot before
; the test can write anything: 0 (default) nothing plugged, chip in device
; mode; 2 a full-speed flash drive the BIOS has already addressed and
; configured, chip in host mode - SPEC.md 9.12.2's refusal case.
; =============================================================================

%ifndef SIMBOOT
%define SIMBOOT 0
%endif

SIMK_MOUSE  equ 1               ; plug kinds: a low-speed boot mouse on
SIMK_DISK   equ 2               ; interface 0 endpoint 81h, and a full-speed
                                ; mass-storage device

; --- primitives (each preserves every register but AL) -----------------------
ch_cmd:
    push es                     ; the copies below point ES at our segment
    push ax
    push bx
    push cx
    push si
    push di
    mov [s_cmd], al
    mov byte [s_ph], 0
    mov byte [s_olen], 0
    mov byte [s_opos], 0
    call sim_sync
    cmp al, CMD_GET_STATUS
    jne .n1
    mov al, [s_int]
    mov byte [s_int], 0
    call sim_out1
    jmp .done
.n1:
    cmp al, CMD_TEST_CONNECT
    jne .n2
    call sim_conn
    call sim_out1
    jmp .done
.n2:
    cmp al, CMD_RD_USB_DATA
    jne .n3
    mov si, s_dlen              ; the length byte, then the bytes
    mov di, s_out
    mov cl, [s_dlen]
    xor ch, ch
    inc cx
    mov [s_olen], cl
    push ds
    pop es                      ; ES = ours for the copy; the caller's is
    cld                         ; banked at entry
    rep movsb
    jmp .done
.n3:
    cmp al, CMD_RESET_ALL
    jne .n4
    xor al, al
    mov [s_mode], al
    mov [s_addr], al
    mov [s_tadr], al
    mov [s_cfg], al
    mov [s_int], al
    mov [s_pin], al
    mov [s_lspd], al
    mov byte [s_nakr], 1
    inc byte [sim_resets]
    jmp .done
.n4:
    cmp al, CMD_ABORT_NAK
    jne .done
    mov byte [s_pin], 0
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    pop es
    ret

ch_st:
    push es                     ; sim_sync may deliver a report, which copies
    push bx
    mov bh, ah
    call sim_sync
    mov al, 0xFF                ; INT# high: nothing pending
    cmp byte [sim_nostat], 0
    jne .out                    ; ...or a board where the bit is not wired
    cmp byte [s_int], 0
    je .out
    mov al, 0x7F                ; bit 7 low: pending
.out:
    mov ah, bh
    pop bx
    pop es
    ret

ch_rd:
    push bx
    mov bl, [s_opos]
    cmp bl, [s_olen]
    jae .none
    inc byte [s_opos]
    xor bh, bh
    mov al, [bx+s_out]
    pop bx
    ret
.none:
    mov al, 0xFF                ; nothing to read: the bus floats
    pop bx
    ret

ch_wr:
    push es
    push ax
    push bx
    push cx
    push si
    push di
    mov bl, [s_ph]
    inc byte [s_ph]
    mov ah, [s_cmd]
    cmp ah, CMD_CHECK_EXIST
    jne .w1
    not al
    call sim_out1
    jmp .done
.w1:
    cmp ah, CMD_SET_USB_MODE
    jne .w2
    call sim_mode
    jmp .done
.w2:
    cmp ah, CMD_SET_RETRY
    jne .w3
    or bl, bl
    jnz .w2b
    mov [s_arg], al             ; the first byte says which sub-function
    jmp .done
.w2b:
    cmp byte [s_arg], 0x25
    jne .w2c
    mov cl, 7
    shr al, cl
    mov [s_nakr], al            ; bit 7: retry NAKs forever
    jmp .done
.w2c:
    cmp byte [s_arg], 0x17
    jne .done
    cmp al, 0xD8
    jne .done
    mov byte [s_lspd], 1        ; the low-speed switch
    jmp .done
.w3:
    cmp ah, CMD_SET_USB_ADDR
    jne .w4
    mov [s_tadr], al
    jmp .done
.w4:
    cmp ah, CMD_WR_USB_DATA7
    jne .w5
    or bl, bl
    jz .done                    ; the length
    dec bl
    cmp bl, 8
    jae .done
    xor bh, bh
    mov [bx+s_setup], al
    jmp .done
.w5:
    cmp ah, CMD_CLR_STALL
    jne .w6
    mov byte [s_int], INT_SUCCESS
    jmp .done
.w6:
    call sim_devok              ; everything below talks to the device
    jc .fail
    cmp ah, CMD_SET_ADDRESS
    jne .w7
    mov [s_addr], al
    mov byte [s_int], INT_SUCCESS
    jmp .done
.w7:
    cmp ah, CMD_GET_DESCR
    jne .w8
    call sim_descr
    jmp .done
.w8:
    cmp ah, CMD_SET_CONFIG
    jne .w9
    mov [s_cfg], al
    inc byte [sim_configs]
    mov byte [s_int], INT_SUCCESS
    jmp .done
.w9:
    cmp ah, CMD_ISSUE_TOKEN
    jne .done
    call sim_token
    jmp .done
.fail:
    mov byte [s_int], INT_TIMEOUT
    cmp byte [s_dev], 0
    jne .done
    mov byte [s_int], INT_DISCONNECT
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    pop es
    ret

; --- the model ---------------------------------------------------------------
; Everything below may spend AX BX CX SI DI: the primitives above bank them.

sim_out1:                       ; AL -> a one-byte answer
    mov [s_out], al
    mov byte [s_olen], 1
    ret

sim_conn:                       ; out AL = what TEST_CONNECT answers
    xor al, al                  ; device mode: "not finished"
    cmp byte [s_mode], 5
    jb .out
    mov al, INT_DISCONNECT
    cmp byte [s_dev], 0
    je .out
    mov al, INT_USB_READY
    cmp byte [s_addr], 0
    jne .out
    mov al, INT_CONNECT
.out:
    ret

sim_mode:                       ; AL = the new mode
    mov [s_mode], al
    mov byte [s_lspd], 0        ; every mode change is full speed again
    cmp al, 7
    jne .m6
    xor al, al                  ; BUS RESET: the device forgets everything
    mov [s_addr], al
    mov [s_tadr], al
    mov [s_cfg], al
    mov [s_pin], al
    mov [s_int], al
    inc byte [sim_busrst]
    jmp short .ok
.m6:
    cmp al, 6
    jne .ok
    cmp byte [s_dev], 0
    je .ok
    cmp byte [s_addr], 0
    jne .ok                     ; already initialised: no fresh connect
    mov byte [s_int], INT_CONNECT
.ok:
    mov al, 0x51                ; CMD_RET_SUCCESS
    jmp sim_out1

sim_devok:                      ; CF = 0 the device would answer a transaction
    cmp byte [s_mode], 6
    jne .no
    mov bl, [s_dev]
    cmp bl, SIMK_MOUSE
    jne .disk
    cmp byte [s_lspd], 1        ; a low-speed device needs the switch...
    jne .no
    jmp short .addr
.disk:
    cmp bl, SIMK_DISK
    jne .no
    cmp byte [s_lspd], 0        ; ...and a full-speed one must not have it
    jne .no
.addr:
    mov bl, [s_tadr]
    cmp bl, [s_addr]
    jne .no
    clc
    ret
.no:
    stc
    ret

sim_descr:                      ; AL = 1 device / 2 configuration
    mov si, sim_mdev
    cmp byte [s_dev], SIMK_MOUSE
    je .k
    mov si, sim_ddev
.k:
    cmp al, 1
    je .copy
    mov si, sim_mcfg
    cmp byte [s_dev], SIMK_MOUSE
    je .copy
    mov si, sim_dcfg
.copy:
    mov cl, [si]                ; each table: a length byte, then the bytes
    xor ch, ch
    inc cx
    mov di, s_dlen
    push ds
    pop es
    cld
    rep movsb
    mov byte [s_int], INT_SUCCESS
    ret

sim_token:                      ; AL = (endpoint << 4) | PID
    mov byte [s_dlen], 0
    mov bl, al
    and bl, 0x0F                ; PID
    mov cl, 4
    shr al, cl                  ; endpoint
    cmp bl, PID_SETUP
    jne .notsetup
    cmp byte [s_setup+1], 0x0B  ; SET_PROTOCOL: remember the value
    jne .ok
    mov bl, [s_setup+2]
    mov [sim_proto], bl
.ok:
    mov byte [s_int], INT_SUCCESS
    ret
.notsetup:
    cmp bl, PID_IN
    jne .stall
    or al, al
    jz .ok                      ; endpoint 0 IN: a status stage
    cmp al, 1
    jne .stall
    cmp byte [s_dev], SIMK_MOUSE
    jne .stall
    cmp byte [s_cfg], 0
    je .stall
    cmp byte [sim_stalln], 0
    je .nostall
    dec byte [sim_stalln]
.stall:
    mov byte [s_int], INT_STALL
    ret
.nostall:
    cmp byte [sim_repn], 0
    jne sim_deliver
    cmp byte [s_nakr], 0
    je .nak
    mov byte [s_pin], 1         ; the chip retries: no interrupt until a report
    ret
.nak:
    mov byte [s_int], INT_NAK
    ret

sim_deliver:                    ; the posted report completes the IN
    mov si, sim_rep
    mov di, s_data
    push ds
    pop es
    cld
    movsw
    movsw
    mov byte [s_dlen], 4
    mov byte [sim_repn], 0
    mov byte [s_pin], 0
    mov byte [s_int], INT_SUCCESS
    ret

sim_sync:                       ; the mailbox's plug and report, applied
    push ax
    push bx
    mov al, [sim_plug]
    cmp al, [s_dev]
    je .same
    mov bl, [s_mode]
    cmp byte [s_dev], 0
    je .in
    xor ah, ah                  ; out: forgets everything, says so in host mode
    mov [s_dev], ah
    mov [s_addr], ah
    mov [s_tadr], ah
    mov [s_cfg], ah
    mov [s_pin], ah
    cmp bl, 5
    jb .in
    mov byte [s_int], INT_DISCONNECT
.in:
    mov [s_dev], al
    or al, al
    jz .same
    cmp bl, 5
    jb .same
    mov byte [s_int], INT_CONNECT
.same:
    cmp byte [s_pin], 0
    je .out
    cmp byte [sim_repn], 0
    je .out
    call sim_deliver
.out:
    pop bx
    pop ax
    ret

; --- descriptors: a length byte, then the descriptor as the device sends it ---
sim_mdev:   db 18
            db 18, 1, 0x10, 0x01, 0, 0, 0, 8, 0x6D, 0x04, 0x16, 0xC0
            db 0x40, 0x03, 1, 2, 0, 1
sim_mcfg:   db 34               ; config, interface (HID boot mouse), HID, EP 81h
            db 9, 2, 34, 0, 1, 1, 0, 0xA0, 50
            db 9, 4, 0, 0, 1, 3, 1, 2, 0
            db 9, 0x21, 0x11, 0x01, 0, 1, 0x22, 52, 0
            db 7, 5, 0x81, 3, 4, 0, 10
sim_ddev:   db 18
            db 18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x81, 0x07, 0x51, 0x55
            db 0x00, 0x01, 1, 2, 3, 1
sim_dcfg:   db 32               ; config, interface (mass storage), 2 x bulk
            db 9, 2, 32, 0, 1, 1, 0, 0x80, 100
            db 9, 4, 0, 0, 2, 8, 6, 0x50, 0
            db 7, 5, 0x81, 2, 64, 0, 0
            db 7, 5, 0x02, 2, 64, 0, 0

; --- the mailbox (tests/usbmouse.py) ------------------------------------------
sim_box:    db 'CH375SIM'       ; +0
%if SIMBOOT == 2
sim_plug    db SIMK_DISK        ; +8   the test writes: 0 / 1 mouse / 2 disk
%else
sim_plug    db 0
%endif
sim_nostat  db 0                ; +9   1: INT# does not reach bit 7
sim_repn    db 0                ; +10  1: sim_rep waits for the next IN
sim_rep     db 0, 0, 0, 0       ; +11  buttons, dx, dy, wheel
sim_stalln  db 0                ; +15  IN tokens to answer with a STALL
sim_configs db 0                ; +16  SET_CONFIGURATIONs accepted
sim_proto   db 0xFF             ; +17  the last SET_PROTOCOL value
sim_resets  db 0                ; +18  RESET_ALLs
sim_busrst  db 0                ; +19  bus resets (SET_USB_MODE 7)
            dw um_state         ; +20  where the driver keeps its state...
            dw um_intok         ; +22  ...how it waits...
            dw um_btn           ; +24  ...and what it last fed

; --- the chip's own state ------------------------------------------------------
%if SIMBOOT == 2
s_mode      db 6
s_dev       db SIMK_DISK
s_addr      db 1
s_tadr      db 1
s_cfg       db 1
%else
s_mode      db 0
s_dev       db 0
s_addr      db 0
s_tadr      db 0
s_cfg       db 0
%endif
s_lspd      db 0
s_nakr      db 1                ; the chip's default: retry a NAK forever
s_int       db 0
s_pin       db 0
s_cmd       db 0
s_ph        db 0
s_arg       db 0
s_setup     times 8 db 0
s_olen      db 0
s_opos      db 0
s_out       times 70 db 0
s_dlen      db 0
s_data      times 64 db 0
