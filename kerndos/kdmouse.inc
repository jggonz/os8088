; =============================================================================
; kerndos/kdmouse.inc - INT 33h'S POINTER, ON A MACHINE WITH NO KERNEL
; (SPEC.md 96.45, docs/plans/KERN-DOS-PLAN.md 6.1 lever 2)
;
; **THE MOUSE WAS NOT MERELY ABSENT, IT WAS SWITCHED OFF ON PURPOSE.** The
; handoff's step 6 calls `mouse_unhook` (kernel/hiber.inc), and it has to: the
; kernel's `mou_isr` lives at a KERNEL_SEG offset that is `kern_dos`'s image
; one instruction later, so an IRQ4 arriving after the handover would vector
; into the middle of the disk layer with the program's registers. That is
; SPEC.md 9.6.5's hard freeze one segment along, and it is the same argument
; the step's own comment makes about `int 09h`.
;
; So what is owed is not "include mouse.inc" - lever 2's estimate - but a
; SECOND, much smaller driver, and the reason it is smaller is that os8088 has
; already done the hard part. `mouse_init` spends 596 ms at boot deciding
; which port has a mouse on it, at what line, against a modem that answers
; 'NO CARRIER' in bytes whose bit 5 is the left button (SPEC.md 9.5). That
; contest is SETTLED by the time the user picks the third arm, so `kern_dos`
; is TOLD - two fields in the launch block, exactly as it is told the boot
; unit and the diskette parameter table (kerndos/kdlaunch.inc) - and carries
; no probe, no hot-plug poller, no per-port arrays and no contest at all.
;
; WHAT IT COSTS THE DOS PROGRAM IS NOTHING. `KD_IMG_KB` is 35 and the image
; measured 34,969 of 35,840 before this, so this lands in slack that was
; already bought and `LOW_SEG` does not move.
;
; **SERIAL ONLY.** A PS/2 mouse ([mou_port] = MOU_P2ROW, SPEC.md 9.9) is a
; different transport on a different line through the 8042, and the box sends
; a zero base for it - which lands on exactly the still pointer this file
; replaces, so a PS/2 machine is no worse than it was and says so in one
; place. MartyPC is an 8088 and the field 5150 is serial, so the arm that
; ships is the arm that can be tested (docs/TESTING.md's list, entry 4).
; =============================================================================

; --- the UART, whose registers are kernel/mouse.inc's own table --------------
; +1 IER, +3 LCR, +4 MCR, +5 LSR - and the BAUD IS NOT SET HERE, deliberately:
; `mouse_unhook` writes IER = 0, masks the line and puts the vector back, and
; touches neither the divisor nor the LCR. So the port is still at the 1200-7N1
; `mouse_init` left it at and re-deriving that would be a second place for it
; to be wrong.
KDM_IER     equ 1
KDM_MCR     equ 4
KDM_LSR     equ 5
KDM_MCR_ON  equ 0x0B            ; DTR|RTS|OUT2 - powers the mouse AND gates the
                                ; UART's interrupt onto the bus. OUT2 is the
                                ; one that is invisible when it is wrong: the
                                ; bytes arrive, the LSR says so, and no IRQ is
                                ; ever raised
KDM_IER_ON  equ 0x01            ; RX data available, and nothing else

KDM_L4      equ 0x10            ; IRQ4's bit in the master 8259's OCW1, and
KDM_L3      equ 0x08            ; IRQ3's - kernel/mouse.inc's MOU_L4/MOU_L3,
                                ; which is what the launch block carries
KDM_V4      equ 0x30            ; ...and the IVT offsets those two lines use:
KDM_V3      equ 0x2C            ; int 0Ch and int 0Bh (mouse.inc's mou_ivecs)

KDM_XMAX    equ 639             ; INT 33h's virtual screen (SPEC.md 96.10), and
KDM_YMAX    equ 199             ; kdentry.inc sets [dos_vw]/[dos_vh] to match

; -----------------------------------------------------------------------------
; kd_mou_start - take the port the kernel settled on (SPEC.md 96.45)
; in:  SI -> the launch block; DS = ES = KD_SEG
; out: nothing; CF undefined. Every register preserved.
;
; **BEFORE `dos_save_machine`**, which is what makes this a TSR rather than a
; kernel service: the whole IVT is what `dos_restore_machine` puts back, so a
; vector taken after the bank is one the program's own exit silently undoes.
; Installed before it, the mouse is simply part of the machine the program is
; handed - which is what a mouse driver is under DOS.
;
; A zero base means "this machine has no serial mouse, or has a PS/2 one":
; nothing is hooked, `[kdm_base]` stays zero, and `kd_mou_read` answers the
; still pointer SPEC.md 96.10 already defines.
; -----------------------------------------------------------------------------
kd_mou_start:
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, [si+KDL_MOUBASE]
    or ax, ax
    jz .none
    mov [kdm_base], ax
    mov al, [si+KDL_MOULINE]
    mov [kdm_line], al

    ; --- the vector, which the line and not the base decides -----------------
    ; A card at 2F8 jumpered to IRQ4 is a machine this project has met
    ; (SPEC.md 9.5.2.1), so the line is carried beside the base rather than
    ; derived from it - the kernel learned which one actually fires by
    ; watching packets arrive, and that answer is not reconstructible here.
    mov bx, KDM_V3
    cmp al, KDM_L4
    jne .vec
    mov bx, KDM_V4
.vec:
    xor ax, ax
    mov es, ax
    pushf
    cli                         ; two words, and an interrupt between them
    mov word [es:bx], kd_mou_isr    ; hands the 8259 half an address
    mov [es:bx+2], cs
    popf

    ; --- the port: gate the IRQ on, then let it interrupt --------------------
    mov dx, [kdm_base]
    add dx, KDM_MCR
    mov al, KDM_MCR_ON
    out dx, al
    mov dx, [kdm_base]          ; ...and DRAIN before unmasking: a byte left
    add dx, KDM_LSR             ; in the receive register from the identify
    in al, dx                   ; burst asserts the line for ever, and the
    mov dx, [kdm_base]          ; UART then never makes another EDGE
    in al, dx
    mov dx, [kdm_base]
    add dx, KDM_IER
    mov al, KDM_IER_ON
    out dx, al

    ; --- ...and the 8259 last, so nothing arrives before there is an ISR -----
    mov cl, [kdm_line]
    not cl
    pushf
    cli
    in al, 0x21
    and al, cl
    out 0x21, al
    popf
.none:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_mou_stop - give the port back quiet (SPEC.md 96.45.1)
; in:  nothing; DS = KD_SEG
; out: nothing; every register preserved
;
; IER = 0 AND the line masked, which is `mouse_unhook`'s own pair and for its
; reason: the mask alone leaves a UART asserting its line, and IER alone
; leaves the 8259 free to deliver an edge that is already latched.
;
; **THE VECTOR IS DELIBERATELY NOT PUT BACK**, because there is nothing to put
; back that means anything: what was there is the kernel's `mou_isr` at an
; offset that is now this image. Quiet hardware is the whole of what the next
; owner needs - `int 19h` and a boot re-init it from scratch.
;
; **THE LIVE RESTORE DOES NOT, AND THIS COMMENT USED TO SAY IT DID** (SPEC.md
; 96.45.2). `mouse_init` is in `.ovlw`, handed back by `mem_unblob` at the end
; of `kmain`, and a live resume re-enters the kernel at `hbm_wake` rather than
; at a boot - so the one place in the tree that writes IER = 1 cannot run, and
; the IER = 0 below stood for the rest of the session. It presented as a dead
; pointer on an otherwise working machine, because the mask and the vector
; both come home and only the UART's own enable does not. `hbm_wake` step 3d
; puts it back; nothing here changes.
; -----------------------------------------------------------------------------
kd_mou_stop:
    push ax
    push cx
    push dx
    cmp word [kdm_base], 0
    je .none
    mov dx, [kdm_base]
    add dx, KDM_IER
    xor al, al
    out dx, al
    mov cl, [kdm_line]
    pushf
    cli
    in al, 0x21
    or al, cl
    out 0x21, al
    popf
    mov word [kdm_base], 0      ; and a second call is a no-op, which matters
.none:                          ; because kd_leave has two exits
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_mou_isr - one UART interrupt (SPEC.md 96.45)
;
; ON THE INTERRUPTED PROGRAM'S STACK, and that is the right answer here where
; SPEC.md 9.10 reached the other one: the kernel moved both mouse ISRs onto a
; private stack to take ~48 bytes off every one of SEVEN task slices, and
; `kern_dos` has no slices - there is one program and its stack is a .COM's
; whole segment or an .EXE's own declared one. Eleven words is what a real
; mode mouse driver costs under DOS too.
;
; IF STAYS 0 from the gate to the `iret`, so this cannot interrupt itself and
; needs no re-entrancy guard - kernel/mouse.inc's own property, for its reason.
; -----------------------------------------------------------------------------
kd_mou_isr:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov ax, KD_SEG              ; the PROGRAM's DS is in that register, and
    mov ds, ax                  ; every cell below is ours
    cld

    mov dx, [kdm_base]
    add dx, KDM_LSR
    in al, dx                   ; anything actually waiting?
    test al, 0x01
    jz .eoi
    mov dx, [kdm_base]
    in al, dx                   ; ...and THIS read is what drops the request

    ; --- the phase machine, which is this host's half (SPEC.md 96.45) --------
    ; ONE PORT, so it is three bytes of state where the kernel needs an array
    ; per port: there is no contest to lose and no modem to be confused by,
    ; because os8088 already decided (the header above).
    test al, 0x40               ; bit 6 set -> first byte of a packet
    jz .cont
    mov [kdm_b0], al
    mov byte [kdm_phase], 1
    jmp .eoi                    ; NEAR, and the decoder below is why: the two
.cont:                          ; header arms sit above forty lines of packet
    cmp byte [kdm_phase], 1     ; arithmetic and a short displacement does not
    jne .chk2                   ; reach past it
    mov [kdm_b1], al
    mov byte [kdm_phase], 2
    jmp .eoi
.chk2:
    cmp byte [kdm_phase], 2     ; anything else is a stray byte: drop it and
    jne .resync                 ; wait for the next header
    mov byte [kdm_phase], 0

    mov dh, [kdm_b0]
    mov dl, [kdm_b1]
    mov bl, al                  ; BL = byte 2, and the three are now in the
    MOU_DECODE_MS               ; shared decoder's contract: AX = dx, BX = dy,
                                ; CL = the buttons (kernel/mouproto.inc)
    mov [kdm_btn], cl

    ; --- and into INT 33h's virtual screen, clamped --------------------------
    ; 1:1 with the mickeys, which is what the kernel's own pointer does with
    ; them on the desktop - so a hand movement means the same distance in the
    ; box's window and out here, and a program that sets its own sensitivity
    ; (function 15) is doing arithmetic on the same input either way.
    add ax, [kdm_x]
    js .xlo                     ; a signed add is the clamp's whole test: the
    cmp ax, KDM_XMAX            ; accumulator is 0..639 so a negative result
    jbe .xok                    ; can only be a leftward delta off the edge
    mov ax, KDM_XMAX
    jmp short .xok
.xlo:
    xor ax, ax
.xok:
    mov [kdm_x], ax
    mov ax, bx
    add ax, [kdm_y]
    js .ylo
    cmp ax, KDM_YMAX
    jbe .yok
    mov ax, KDM_YMAX
    jmp short .yok
.ylo:
    xor ax, ax
.yok:
    mov [kdm_y], ax
    jmp short .eoi
.resync:
    mov byte [kdm_phase], 0
.eoi:
    mov al, 0x20                ; the master, and only the master: IRQ3 and
    out 0x20, al                ; IRQ4 are both its lines
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

; -----------------------------------------------------------------------------
; kd_mou_read - DHK_MOUSE (SPEC.md 96.44.3)
; in:  nothing; DS = KD_SEG
; out: BX = the button mask, CX = x, DX = y - all in INT 33h units
;
; The box's `dos_hk_mouse` scales the kernel's screen pixels into 640x200 here
; (SPEC.md 96.10); this accumulates in those units already, because there is
; no screen to scale from - `kern_dos` never sets a mode and the program owns
; every pixel.
;
; READ UNDER `cli`: the three cells are written by an ISR, and a caller that
; sampled x before an interrupt and y after it is handed a position the
; pointer never had. It is two instructions and it is the kind of tear that
; presents as a program's own cursor drawing occasionally in the wrong place.
; -----------------------------------------------------------------------------
kd_mou_read:
    pushf
    cli
    mov bl, [kdm_btn]
    xor bh, bh
    mov cx, [kdm_x]
    mov dx, [kdm_y]
    popf
    ret

; -----------------------------------------------------------------------------
; kd_mou_txt - DHK_TXT (SPEC.md 96.10.5.1): the text screen the cursor may
;              draw on
; in:  nothing; DS = KD_SEG
; out: ES = the segment, BX = columns, DX = rows, CF=1 = there is none
;      AX destroyed, everything else preserved
;
; **IT ASKS THE BDA RATHER THAN REMEMBERING**, so a program that changes mode
; mid-session gets its cursor in the right place with nothing told to us: the
; ROM maintains `0040:0049` (the mode) and `0040:004A` (the width) on every
; `int 10h AH=00h`, and a DOS program sets its mode through the ROM.
;
; Modes 0-3 are the colour text modes at `B800` and mode 7 is the mono one at
; `B000`. **ANYTHING ELSE IS REFUSED**, which is not a limitation to apologise
; for - a pointer in a graphics mode is function `09h`'s bitmap cursor and a
; different feature (96.10.2).
;
; Rows are 25 and are not read back: `0040:0084` is EGA and later, and on the
; 5150 this project is calibrated against it is somebody else's byte. The
; ceiling only has to be a bound the core clamps against, and 25 is the true
; one on every adapter that has these four modes.
; -----------------------------------------------------------------------------
kd_mou_txt:
    push si
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x49]               ; the mode the ROM last set
    cmp al, 7
    je .mono
    cmp al, 3
    ja .no                          ; 4, 5, 6 and everything above 7 are
    mov si, 0xB800                  ; GRAPHICS, and a graphics pointer is 09h
    jmp short .have
.mono:
    mov si, 0xB000
.have:
    mov bx, [es:0x4A]               ; the width, which is 40 in modes 0 and 1
    or bx, bx                       ; ...and zero on a BDA nothing has written,
    jz .no                          ; which would make the cell arithmetic a
    or bh, bh                       ; wild store. **AND IT MUST FIT A BYTE**:
    jnz .no                         ; the core's `row * columns` is `mul bl`,
                                    ; so a width above 255 would be multiplied
                                    ; by its low byte and clamped against the
                                    ; whole word - two different numbers, and
                                    ; a store past the end of the screen. No
                                    ; text mode is that wide; a BDA that says
                                    ; so has been scribbled on
    mov dx, 25
    mov es, si
    pop si
    clc
    ret
.no:
    pop si
    stc
    ret

section .bss
kdm_base:   resw 1              ; the UART, 0 = no serial mouse on this machine
kdm_line:   resb 1              ; ...and its bit in the master 8259's mask
kdm_phase:  resb 1
kdm_b0:     resb 1
kdm_b1:     resb 1
kdm_btn:    resb 1
kdm_x:      resw 1
kdm_y:      resw 1
section .text
