; =============================================================================
; os8088 - drivers/joy/joy.asm
;
; JOYSTICK.DRV: THE GAME PORT, one driver for every game (SPEC.md 137).
;
; The IBM Game Control Adapter at 201h and every clone of it: four one-shots
; (X1, Y1, X2, Y2 - bits 0-3, 1 while timing) fired by any write, and four
; buttons (bits 4-7, 0 = pressed). A package never touches the port: it asks
; this driver through OSAPI_DRV_CALL (BH = DRVC_JOY, apps/os88joy.inc), and
; gets the stick the user calibrated ONCE on this driver's Control Panel page,
; with the dead zone, Invert and Swap applied and the presses that fell
; between two of its reads kept for it (SPEC.md 137.3, 137.4).
;
; NOTHING IS HOOKED. No vector, no worker, no tick, no claim: a sample is
; taken when a reader asks for one and never otherwise, so a machine with the
; row ticked and no game running pays the image and nothing else - and attach
; and detach have nothing to undo.
;
; ONE SAMPLE (SPEC.md 137.3, joy_sample): with interrupts off, PIT channel 0
; is latched, the port is fired, and it is read four times a block, the
; channel latched again after every block and at every fall - so a raw count
; is TIME, in 838 ns PIT units, and a turbo switch does not move a
; calibration. Only the axes the controller type uses AND that answered are
; waited for (Linux's `analog` mask); the window ends when they have all
; fallen or at JOY_TMO (~2.3 ms), under the 4.17 ms an 8250 byte takes at
; 2400 baud.
;
; THE CHANNEL MAY NOT BE THE SCHEDULER'S 65536 (SPEC.md 137.10). A package
; inside an FSXF_RATE bracket, or the speaker's PCM tier, runs channel 0 at a
; divisor of its own, and the count then reloads inside the window. A
; difference larger than any one block can take (JOY_TSTEP) is that reload,
; and is replaced by the previous block's length: exact at the scheduler's
; divisor, within one block (~40 us on the XT) at any other.
; =============================================================================

;   the page (SPEC.md 137.6) is on, and it lays itself out from the size it
;   is painted with - os88drv.inc reads OS88_DRV_CPWIDE as it is included
%define JOY_UI
%ifdef JOY_UI
  %define OS88_DRV_CPWIDE
%endif
%define OS88_DRV_HIGHOK         ; kern_mega may run this image above 1MB: no
                                ; paragraph arithmetic on its own segment, no
                                ; BIOS call or DMA engine handed it, port I/O
                                ; and PIT channel 0 only (KERN-MEGA-PLAN 82)
%include "os88drv.inc"
%include "joypkg.inc"

; THE PAGE (SPEC.md 137.6, the plan's J5) is drivers/joy/joyui.inc, switched
; on here: the service table below publishes its cells (joyui_name, _paint,
; _click, _close, _key, _up, _drag and the live tick, DSV_CPTICK - so the
; table is DSV_MAX long, SPEC.md 31.9.4), and OS88_DRV_CPWIDE says the page
; lays itself out from the size it is painted with, so the wide face hosts it
; over its whole page area (SPEC.md 135.3.1). Everything the page may read or
; call is listed in joypkg.inc's second half.

    OS88_DRIVER 'Joystick', DRVC_JOY, joy_entry, joy_svc_end - joy_svc

%if DRVV_ATTACH != 0 || DRVV_DETACH != 1 || DRVV_READY != 3
    %error "joy_entry assumes the published DRVV_* verb values"
%endif
%if JOYV_INFO != 0 || JOYV_READ != 1 || JOYV_STATE != 2 || JOYV_RAW != 3 || JOYV_PROBE != 4
    %error "joy_pkg's compare ladder assumes the JOYV_* numbers"
%endif
%if JOYA_X1 != 1 || JOYA_Y1 != 2 || JOYA_X2 != 4 || JOYA_Y2 != 8
    %error "joy_sample stores a fall by shifting the axis bits out in port order"
%endif
%if JOYS_Y1 != JOYS_X1 + 1 || JOYS_Y2 != JOYS_X2 + 1 || JOYS_X2 != JOYS_X1 + 2
    %error "joy_cook stores each stick's X and Y as one word"
%endif

; =============================================================================
; ENTRY. Every verb is named; anything else is refused (SPEC.md 51.2.2)
; =============================================================================
joy_entry:
    cmp al, DRVV_DETACH
    je .yes                     ; DETACH: nothing hooked, nothing claimed -
    cmp al, DRVV_READY          ; READY: nothing to arm. Both CF = 0. A
    je .yes                     ; READY refusal would be banked as the row's
                                ; reason, and "nothing plugged in" is not one
                                ; (SPEC.md 137.2)
    cmp al, DRVV_ATTACH
    jne .no
    jmp joy_attach
.yes:
    clc
    ret
.no:
    stc                         ; TIER, HWINFO, SLEEP, WAKE and any later
    ret                         ; verb: refused, no work done

; -----------------------------------------------------------------------------
; DRVV_ATTACH - read the settings, probe once, never refuse for want of a stick
; (SPEC.md 137.2): a missing game port and one with nothing plugged in read
; the same, and a stick is plugged in while the machine runs far more often
; than a card is. out CF = 0, SI = joy_svc. BX PRESERVED (drv_attach reads the
; row through it on return)
; -----------------------------------------------------------------------------
joy_attach:
    push ax
    push cx
    push es
    push ds
    pop es                      ; the blob is read into OUR segment
    mov si, joy_blob
    mov cx, JOYB_SZ
    xor al, al                  ; read. ATTACH may: the fence is the row,
    call OSAPI_DRV_CFG          ; armed before the image was read (51.9.2)
    pop es
    jc .def                     ; no row map (an older kernel): defaults
    cmp cx, JOYB_SZ
    jne .def
    call joy_valid
    jnc .ok
.def:
    call joy_defaults
.ok:
    call joy_apply
    call joy_probe              ; the one look at boot: joy_live, and the
    mov byte [joy_press], 0     ; first sample's centres. A button held at
    pop cx                      ; boot is not a press
    pop ax
    mov si, joy_svc
    clc
    ret

; joy_valid - is joy_blob one this driver wrote? CF = 1 no. Every register kept
joy_valid:
    cmp byte [joy_blob+JOYB_VER], JOYB_V
    jne .no
    cmp byte [joy_blob+JOYB_TYPE], JOYT_MAX
    ja .no
    cmp byte [joy_blob+JOYB_DZ], JOY_DZMAX
    ja .no
    test byte [joy_blob+JOYB_FLAGS], ~(JOYF_INVY | JOYF_SWAP) & 0xFF
    jnz .no
    clc
    ret
.no:
    stc
    ret

; joy_defaults - joy_blob = the defaults: version, one stick two buttons, no
; flags, JOY_DZDEF, nothing calibrated. Every register kept
joy_defaults:
    push bx
    mov bx, JOYB_SZ - 1
.z:
    mov byte [joy_blob+bx], 0
    dec bx
    jns .z
    mov byte [joy_blob+JOYB_VER], JOYB_V
    mov byte [joy_blob+JOYB_DZ], JOY_DZDEF
    pop bx
    ret

; -----------------------------------------------------------------------------
; joy_reset - forget the calibration (the page's Reset): every axis's centre
; 0 in the blob, then joy_apply - so the next sample that answers is each
; axis's centre again. The caller saves (joy_save) if it wants that kept.
; Every register kept
; -----------------------------------------------------------------------------
joy_reset:
    push bx
    mov bx, JOYB_CAL + 3 * JOYC_SZ
.c:
    mov word [joy_blob+bx+JOYC_CEN], 0
    sub bx, JOYC_SZ
    cmp bx, JOYB_CAL
    jae .c
    pop bx
    ; ...and into joy_apply

; -----------------------------------------------------------------------------
; joy_apply - joy_blob -> the type's facts (joy_tax, joy_tbm, joy_tnb,
; joy_tns) and the runtime calibration (joy_cal). Called at attach and by the
; page after it edits joy_blob. Every register kept
; -----------------------------------------------------------------------------
joy_apply:
    push ax
    push bx
    push cx
    push si
    push di
    mov bl, [joy_blob+JOYB_TYPE]
    mov bh, 0
    mov al, bl
    shl bx, 1
    add bl, al                  ; BX = the type * 3 (types are 0..5)
    mov al, [bx+joy_types]      ; the axes the type uses...
    test byte [joy_blob+JOYB_FLAGS], JOYF_SWAP
    jz .nsw
    call joy_swap4              ; ...on the other side of the port, swapped
.nsw:
    mov [joy_tax], al
    mov cl, [bx+joy_types+1]
    mov [joy_tnb], cl           ; buttons...
    mov al, 1
    shl al, cl
    dec al
    mov [joy_tbm], al           ; ...as a mask: 2 -> 3, 4 -> 0Fh
    mov al, [bx+joy_types+2]
    mov [joy_tns], al           ; sticks
    mov di, joy_blob + JOYB_CAL
    mov si, joy_cal
    mov cx, 4
.ax:
    mov ax, [di+JOYC_CEN]
    mov [si+JCAL_CEN], ax
    or ax, ax
    jz .nx                      ; not calibrated: the next sample that answers
    mov al, [di+JOYC_MIN]       ; is its centre (joy_calpos)
    call joy_x16
    mov [si+JCAL_MIN], ax
    mov al, [di+JOYC_MAX]
    call joy_x16
    mov [si+JCAL_MAX], ax
    call joy_prep
.nx:
    add di, JOYC_SZ
    add si, JCAL_SZ
    loop .ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; joy_x16 - AX = AL * 16 (a blob end back to PIT counts). Nothing else
joy_x16:
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    ret

; joy_swap4 - AL's low nibble with its two halves traded (stick A's axes or
; buttons for stick B's); the high nibble cleared. Nothing else
joy_swap4:
    push cx
    mov cl, al
    shr cl, 1
    shr cl, 1
    shl al, 1
    shl al, 1
    or al, cl
    and al, 0x0F
    pop cx
    ret

; -----------------------------------------------------------------------------
; joy_prep - one axis's dead zone, in counts each side of its centre: DZL =
; (CEN - MIN) * dz / 100, DZH = (MAX - CEN) * dz / 100. An end on the wrong
; side of the centre is pulled to it, so a span is never negative.
; in: SI -> the axis's joy_cal entry. Every register kept
; -----------------------------------------------------------------------------
joy_prep:
    push ax
    mov ax, [si+JCAL_CEN]
    cmp [si+JCAL_MIN], ax
    jbe .lo
    mov [si+JCAL_MIN], ax
.lo:
    cmp [si+JCAL_MAX], ax
    jae .hi
    mov [si+JCAL_MAX], ax
.hi:
    sub ax, [si+JCAL_MIN]
    call .dz
    mov [si+JCAL_DZL], ax
    mov ax, [si+JCAL_MAX]
    sub ax, [si+JCAL_CEN]
    call .dz
    mov [si+JCAL_DZH], ax
    pop ax
    ret
.dz:                            ; AX = a half -> AX = dz percent of it
    push bx
    push dx
    mov bl, [joy_blob+JOYB_DZ]
    mov bh, 0
    mul bx
    mov bx, 100
    div bx                      ; (half <= ~2800 and dz <= 30: no overflow)
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; joy_save - joy_cal -> joy_blob's calibration (the ends at /16, rounded
; outward; the centre whole), then the blob to the kernel through
; OSAPI_DRV_CFG. SYSTEM.CFG is written at the Control Panel's close, like
; every setting (SPEC.md 31.8). out CF = the kernel's answer. Every register
; but the flags kept
; -----------------------------------------------------------------------------
joy_save:
    push ax
    push cx
    push si
    push di
    mov si, joy_cal
    mov di, joy_blob + JOYB_CAL
    mov cx, 4
.ax:
    mov ax, [si+JCAL_CEN]
    mov [di+JOYC_CEN], ax
    mov ax, [si+JCAL_MIN]
    call .d16                   ; the minimum DOWN...
    mov [di+JOYC_MIN], al
    mov ax, [si+JCAL_MAX]
    add ax, 15                  ; ...and the maximum UP, so the stored span
    call .d16                   ; covers the travel and the ends clamp at 127
    mov [di+JOYC_MAX], al
    add si, JCAL_SZ
    add di, JOYC_SZ
    loop .ax
    push es
    push ds
    pop es
    mov si, joy_blob
    mov cx, JOYB_SZ
    mov al, 1                   ; write: SYSTEM.CFG is owed one
    call OSAPI_DRV_CFG
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
.d16:                           ; AL = AX / 16, held to 255
    push cx
    mov cl, 4
    shr ax, cl
    pop cx
    or ah, ah
    jz .fit
    mov al, 0xFF
.fit:
    ret

; =============================================================================
; THE SAMPLE (SPEC.md 137.3)
; =============================================================================

; -----------------------------------------------------------------------------
; joy_sample - fire the one-shots and time the fall of the axes asked for
; in:  AH = the axes to wait for (JOYA_* bits; 0 = none, the buttons only)
; out: joy_raw[0..3] = PIT counts from the fire to each fall, JOY_NONE for an
;      axis not asked for or that did not fall by JOY_TMO; joy_btn = buttons
;      held (bits 0-3, pressed = 1). AL = the axes that fell, AH = joy_btn
; Interrupts OFF for the whole window, restored on the way out. Every other
; register kept
; -----------------------------------------------------------------------------
joy_sample:
    push bx
    push cx
    push dx
    push si
    push bp
    mov bx, JOY_NONE
    mov [joy_raw], bx
    mov [joy_raw+2], bx
    mov [joy_raw+4], bx
    mov [joy_raw+6], bx
    mov bl, ah
    and bl, 0x0F                ; BL = still timing, of those asked for
    mov bh, bl                  ; BH = asked for
    mov dx, JOY_PORT
    mov word [joy_tlast], JOY_TBLK
    xor bp, bp                  ; BP = elapsed, PIT counts since the fire
    pushf
    cli
    call joy_pit
    mov [joy_tprev], ax
    out dx, al                  ; FIRE: any write starts all four one-shots
    test bl, bl
    jz .done                    ; nothing to wait for: the buttons alone
.blk:
%rep 4
    in al, dx                   ; ONE READ: 7 bytes, ~6 us on the XT - the
    and al, bl                  ; resolution of a fall, since the clock is
    cmp al, bl                  ; latched right after it is seen
    jne .fell
%endrep
    call joy_tick
    cmp bp, JOY_TMO
    jb .blk
    jmp short .done             ; timed out: what is still timing stays
                                ; JOY_NONE
.fell:
    mov ah, al                  ; AH = still timing
    call joy_tick               ; BP = when (joy_tick spends AL only)
    xchg ah, bl                 ; BL = still timing, AH = was timing...
    xor ah, bl                  ; ...AH = fell since the last look
    mov si, joy_raw
.st:
    shr ah, 1
    jnc .ns
    mov [si], bp                ; this axis's count
.ns:
    inc si
    inc si
    or ah, ah
    jnz .st
    or bl, bl
    jz .done                    ; every axis asked for has fallen
    cmp bp, JOY_TMO
    jb .blk
.done:
    in al, dx                   ; the buttons, from the last read
    popf
    not al
    mov cl, 4
    shr al, cl
    mov [joy_btn], al
    mov ah, al                  ; AH = the buttons...
    mov al, bl
    not al
    and al, bh                  ; ...AL = asked for and not still timing
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; joy_pit - AX = PIT channel 0's count, latched (it counts DOWN). The caller
; holds IF = 0: a latch interleaved with another reader's is undefined
joy_pit:
    mov al, 0
    out 0x43, al                ; latch counter 0: a READ command, so the
    jmp short $+2               ; system tick itself is undisturbed
    in al, 0x40
    mov ah, al
    jmp short $+2
    in al, 0x40
    xchg al, ah
    ret

; joy_tick - BP += the PIT counts since the last latch, which becomes this
; one. A difference past JOY_TSTEP is a RELOAD under a fast divisor (the
; header), never time, and counts as the previous block did. Spends AL only
joy_tick:
    push ax
    push cx
    call joy_pit
    mov cx, [joy_tprev]
    mov [joy_tprev], ax
    sub cx, ax                  ; down-counting: prev - now (mod 65536 is
    cmp cx, JOY_TSTEP           ; exact across the scheduler's own reload)
    jb .ok
    mov cx, [joy_tlast]
.ok:
    mov [joy_tlast], cx
    add bp, cx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; joy_probe - sample ALL FOUR axes with the full timeout: joy_live is the axes
; that answered (a stick plugged in since, or one taken out), and the sample
; is cooked. out AL = joy_live. Interrupts off for the window. Every other
; register kept
; -----------------------------------------------------------------------------
joy_probe:
    push ax
    mov ah, 0x0F
    call joy_sample
    mov [joy_live], al
    call OSAPI_GET_TICKS        ; when: joy_take's re-probe is timed off it
    mov [joy_ptick], ax
    call joy_cook
    pop ax
    mov al, [joy_live]
    ret

; -----------------------------------------------------------------------------
; joy_take - THE sample every verb takes: the expected axes (the type's, that
; answer), an axis that times out dropped from joy_live; or, while NONE of
; the type's axes answers, a full probe at most once per JOY_PROBEGAP ticks so
; a stick plugged in mid-game is found within a second - and between probes
; the buttons alone, no axis waited for. Cooked into joy_st. Interrupts off
; throughout, so a worker and a UI task asking at once cannot tear joy_st.
; Every register kept
; -----------------------------------------------------------------------------
joy_take:
    push ax
    push cx
    pushf
    cli
    mov al, [joy_live]
    and al, [joy_tax]
    jnz .some
    call OSAPI_GET_TICKS
    sub ax, [joy_ptick]
    cmp ax, JOY_PROBEGAP
    jb .bare
    call joy_probe
    jmp short .out
.bare:
    xor al, al                  ; buttons only
.some:
    mov ah, al
    mov ch, al                  ; CH = expected
    call joy_sample             ; AL = fell
    not al
    and al, ch                  ; expected and did not fall: DROPPED until
    not al                      ; the next probe, so it is never waited on
    and [joy_live], al          ; again (SPEC.md 137.3 step 2)
    call joy_cook
.out:
    popf
    pop cx
    pop ax
    ret

; =============================================================================
; COOKING: raw counts -> what a package reads (SPEC.md 137.3, 137.4)
; =============================================================================

; -----------------------------------------------------------------------------
; joy_cook - joy_raw / joy_btn / joy_live -> joy_st, and the presses. The
; calibration is per PORT axis; Swap and Invert are logical, after it.
; Every register kept
; -----------------------------------------------------------------------------
joy_cook:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- 1. each port axis through its calibration ---------------------------
    mov si, joy_cal
    mov di, joy_raw
    xor bx, bx
.p:
    mov ax, [di]
    call joy_calpos             ; AL = -127..127, 0 for JOY_NONE
    mov [joy_pos+bx], al
    add si, JCAL_SZ
    inc di
    inc di
    inc bx
    cmp bl, 4
    jb .p
    ; --- 2. Swap: stick B's side of the port becomes A's ---------------------
    mov ax, [joy_pos]           ; AL X1, AH Y1
    mov dx, [joy_pos+2]         ; DL X2, DH Y2
    mov bl, [joy_btn]
    mov bh, [joy_live]
    test byte [joy_blob+JOYB_FLAGS], JOYF_SWAP
    jz .nsw
    xchg ax, dx
    xchg al, bl
    call joy_swap4              ; the buttons in pairs...
    xchg al, bl
    xchg al, bh
    call joy_swap4              ; ...and which axes answer
    xchg al, bh
.nsw:
    mov [joy_llive], bh
    ; --- 3. Invert Y: stick A's, and stick B's on the two-stick type ---------
    mov cl, [joy_blob+JOYB_TYPE]
    test byte [joy_blob+JOYB_FLAGS], JOYF_INVY
    jz .ninv
    neg ah
    cmp cl, JOYT_2X2B
    jne .ninv
    neg dh
.ninv:
    mov [joy_st+JOYS_X1], ax
    mov [joy_st+JOYS_X2], dx
    ; --- 4. the buttons the type has, and its hat ----------------------------
    and bl, [joy_tbm]
    xor ch, ch                  ; CH = the hat, JOYD_* (0 = centred, none)
    cmp cl, JOYT_CHFS
    jne .nch
    mov al, bl                  ; CH FlightStick Pro: buttons 1 AND 2 held
    and al, 3                   ; together is the HAT, 3 and 4 saying which
    cmp al, 3                   ; way, and no button at all
    jne .hat
    mov al, bl
    shr al, 1
    shr al, 1
    mov bx, ax
    and bx, 3
    mov ch, [bx+joy_chhat]
    xor bl, bl
    jmp short .hat
.nch:
    cmp cl, JOYT_TMFCS
    jne .hat
    mov ax, [joy_raw+6]         ; ThrustMaster FCS: the hat is the LEVEL of
    cmp ax, JOY_NONE            ; Y2 - up lowest, then right, down, left,
    je .hat                     ; centred at the top, which a calibration (or
    mov si, [joy_cal+3*JCAL_SZ+JCAL_CEN] ; the first sample) holds as Y2's
    or si, si                   ; centre: q = (4 * raw + c/2) / c
    jz .hat
    push dx
    shl ax, 1
    shl ax, 1
    mov dx, si
    shr dx, 1
    add ax, dx
    xor dx, dx
    div si
    pop dx
    cmp ax, 4
    jae .hat                    ; 4 and past: centred
    push bx
    mov bx, ax
    mov ch, [bx+joy_fcshat]
    pop bx
.hat:
    mov [joy_st+JOYS_HAT], ch
    ; --- 5. presses: a level that went 0 -> 1 since the last sample ----------
    mov al, [joy_bprev]
    mov [joy_bprev], bl
    not al
    and al, bl
    or [joy_press], al
    mov [joy_st+JOYS_BTN], bl
    mov al, [joy_press]
    mov [joy_st+JOYS_PRESS], al
    ; --- 6. directions -------------------------------------------------------
    mov ax, [joy_st+JOYS_X1]
    call joy_dir
    mov [joy_st+JOYS_DIRA], al
    xor al, al
    cmp cl, JOYT_2X2B
    jne .nb
    mov ax, dx
    call joy_dir
.nb:
    mov [joy_st+JOYS_DIRB], al
    ; --- 7. throttle and rudder, 0..255, on the flight types -----------------
    xor ax, ax
    cmp cl, JOYT_FLIGHT
    je .fl
    cmp cl, JOYT_CHFS
    jne .nfl
.fl:
    mov bh, [joy_llive]
    test bh, JOYA_X2
    jz .nrud
    mov ah, dl
    xor ah, 0x80                ; -127..127 -> 1..255
.nrud:
    test bh, JOYA_Y2
    jz .nfl
    mov al, dh
    xor al, 0x80
.nfl:
    mov [joy_st+JOYS_THR], al
    mov [joy_st+JOYS_RUD], ah
    ; --- 8. the port as it is ------------------------------------------------
    mov al, [joy_live]
    mov [joy_st+JOYS_AXES], al
    mov ax, [joy_raw]
    mov [joy_st+JOYS_RAW], ax
    mov ax, [joy_raw+2]
    mov [joy_st+JOYS_RAW+2], ax
    mov ax, [joy_raw+4]
    mov [joy_st+JOYS_RAW+4], ax
    mov ax, [joy_raw+6]
    mov [joy_st+JOYS_RAW+6], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; joy_calpos - one raw count through its axis's calibration
; in:  SI -> the axis's joy_cal entry, AX = the raw count
; out: AL = the position, -127..127 (0 inside the dead zone, clamped at the
;      ends; 0 for JOY_NONE). An axis with no centre yet takes THIS count as
;      it, MIN 0 and MAX twice it, and answers 0 (SPEC.md 137.3)
; Every other register kept (AH spent)
; -----------------------------------------------------------------------------
joy_calpos:
    push bx
    push cx
    push dx
    cmp ax, JOY_NONE
    je .zero
    mov cx, [si+JCAL_CEN]
    jcxz .auto
    sub ax, cx
    jb .low
    mov bx, [si+JCAL_DZH]       ; right / down of the centre
    mov cx, [si+JCAL_MAX]
    sub cx, [si+JCAL_CEN]
    call .scale
    jmp short .out
.low:
    neg ax                      ; left / up
    mov bx, [si+JCAL_DZL]
    mov cx, [si+JCAL_CEN]
    sub cx, [si+JCAL_MIN]
    call .scale
    neg al
    jmp short .out
.auto:
    or ax, ax
    jz .zero
    mov [si+JCAL_CEN], ax
    mov word [si+JCAL_MIN], 0
    shl ax, 1
    mov [si+JCAL_MAX], ax
    call joy_prep
.zero:
    xor al, al
.out:
    pop dx
    pop cx
    pop bx
    ret
.scale:                         ; AX = distance, BX = dead zone, CX = half
    sub ax, bx                  ; -> AL = 0..127
    jbe .s0                     ; inside the dead zone
    sub cx, bx
    jbe .smax                   ; (a degenerate half: all or nothing)
    cmp ax, cx
    jae .smax
    mov dx, 127
    mul dx
    div cx                      ; < 127: AX < CX
    ret
.s0:
    xor al, al
    ret
.smax:
    mov al, 127
    ret

; joy_dir - AL = X, AH = Y (signed) -> AL = JOYD_*, past half of full travel.
; AH spent, nothing else
joy_dir:
    push cx
    xor cl, cl
    cmp al, -JOY_HALF
    jg .nl
    or cl, JOYD_LEFT
.nl:
    cmp al, JOY_HALF
    jl .nr
    or cl, JOYD_RIGHT
.nr:
    cmp ah, -JOY_HALF
    jg .nu
    or cl, JOYD_UP
.nu:
    cmp ah, JOY_HALF
    jl .nd
    or cl, JOYD_DOWN
.nd:
    mov al, cl
    pop cx
    ret

; =============================================================================
; THE PACKAGE DOOR (DSV_PKGCALL, SPEC.md 137.4)
; in:  BL = the verb, BH = DRVC_JOY, ES = the CALLER's segment, DS = ours
; out: per verb; CF = 1 a verb this driver does not have, AX = 0.
; DI IS PRESERVED (the package is promised it back), and every register a
; verb does not answer in. Every verb is worker-safe: no file, no lock, and
; each takes at most one sample
; =============================================================================
joy_pkg:
    cmp bl, JOYV_READ
    jb joy_v_info
    je joy_v_read
    cmp bl, JOYV_RAW
    jb joy_v_state
    je joy_v_raw
    cmp bl, JOYV_PROBE
    je joy_v_probe
    xor ax, ax
    stc
    ret

; INFO: no sample. AL = joy_live, AH = the type, CL = its buttons, CH = its
; sticks, DX = JOY_VER
joy_v_info:
    mov al, [joy_live]
    mov ah, [joy_blob+JOYB_TYPE]
    mov cl, [joy_tnb]
    mov ch, [joy_tns]
    mov dx, JOY_VER
    clc
    ret

; PROBE: all four axes, the full timeout. AL = joy_live
joy_v_probe:
    pushf
    cli
    call joy_probe
    popf
    clc
    ret

; RAW: one sample, uncooked to the caller (cooked here all the same, so a
; press that falls between two RAW reads still counts). AX BX CX DX = X1 Y1
; X2 Y2 raw, SI = the port's buttons held, bits 0-3
joy_v_raw:
    pushf
    cli
    call joy_take
    mov al, [joy_btn]
    cbw                         ; (bits 0-3: AH = 0)
    xchg si, ax
    mov ax, [joy_raw]
    mov bx, [joy_raw+2]
    mov cx, [joy_raw+4]
    mov dx, [joy_raw+6]
    popf
    clc
    ret

; STATE: one sample, the whole port into ES:DI (JOYS_SIZE bytes, the caller's
; own image). The presses go with it and are cleared. Every register kept
joy_v_state:
    push cx
    push si
    push di
    pushf
    cli
    call joy_take
    mov si, joy_st
    mov cx, JOYS_SIZE
    cld
    rep movsb                   ; ES = the caller's: the X stub put it there
    mov byte [joy_press], 0     ; (joy_st's PRESS was the live count)
    popf
    pop di
    pop si
    pop cx
    clc
    ret

; READ: AL = the stick. One sample. CF = 1 that stick is not answering - it is
; not one the type has, or neither of its axes answers. CF = 0: AL = X, AH = Y,
; BL = its buttons held, BH = its presses (cleared), CL = its JOYD_*. CH, DX,
; SI and DI kept
joy_v_read:
    push dx
    push si
    pushf
    cli
    mov dl, al                  ; DL = the stick
    call joy_take
    cmp dl, [joy_tns]
    jae .no                     ; stick B of a one-stick type
    mov al, [joy_llive]
    mov si, joy_st + JOYS_X1
    mov cl, [joy_st+JOYS_DIRA]
    mov dh, 0x0F                ; a one-stick type: every button is A's
    cmp byte [joy_tns], 2
    jb .a
    mov dh, 0x03                ; two sticks: two buttons each
    or dl, dl
    jz .a
    shr al, 1
    shr al, 1
    mov si, joy_st + JOYS_X2
    mov cl, [joy_st+JOYS_DIRB]
    mov dh, 0x0C
.a:
    test al, JOYA_X1 | JOYA_Y1  ; this stick's two axes, either answering
    jz .no                      ; (a paddle is one)
    mov bl, [joy_st+JOYS_BTN]
    and bl, dh
    mov bh, [joy_press]
    and bh, dh
    not dh
    and [joy_press], dh         ; read, so cleared - this stick's only
    cmp dh, ~0x0C & 0xFF
    jne .sh
    shr bl, 1                   ; stick B's buttons to bits 0-1
    shr bl, 1
    shr bh, 1
    shr bh, 1
.sh:
    mov ax, [si]
    popf
    pop si
    pop dx
    clc
    ret
.no:
    popf
    pop si
    pop dx
    stc
    ret

; --- tables --------------------------------------------------------------------
joy_types:                      ; per JOYT_*: the axes it uses, its buttons,
    db 0x03, 2, 1               ; its sticks. JOYT_2B
    db 0x0F, 4, 2               ; JOYT_2X2B: B on X2/Y2, buttons 3-4
    db 0x03, 4, 1               ; JOYT_4B
    db 0x0F, 4, 1               ; JOYT_FLIGHT: rudder X2, throttle Y2
    db 0x0F, 4, 1               ; JOYT_CHFS
    db 0x0B, 4, 1               ; JOYT_TMFCS: X1, Y1 and the hat's Y2
%if ($ - joy_types) != 3 * (JOYT_MAX + 1)
    %error "joy_types: one row per JOYT_*"
%endif
joy_chhat:                      ; CH FlightStick Pro, buttons 1+2 held: 3 and
    db JOYD_LEFT, JOYD_DOWN     ; 4 as a number. 0011 left, 0111 down,
    db JOYD_RIGHT, JOYD_UP      ; 1011 right, 1111 up
joy_fcshat:                     ; ThrustMaster FCS: Y2's level, lowest first
    db JOYD_UP, JOYD_RIGHT, JOYD_DOWN, JOYD_LEFT

%ifdef JOY_UI
%include "joyui.inc"            ; the Control Panel page (SPEC.md 137.6)
%endif

; =============================================================================
; THE SERVICE TABLE (SPEC.md 51.2): read IN PLACE by the kernel for as long as
; the class is published, so it stays here, whole, DSV_FLOOR bytes
; =============================================================================
joy_svc:
    times DSV_CPNAME db 0       ; no sound, no blocks
%ifdef JOY_UI
    dw joyui_name               ; DSV_CPNAME
    dw joyui_paint              ; DSV_CPPAINT
    dw joyui_click              ; DSV_CPCLICK
    dw joyui_close              ; DSV_CPCLOSE
    dw 0                        ; DSV_FS: not a file class
    dw joyui_key                ; DSV_CPKEY
    dw joyui_up                 ; DSV_CPUP
    dw joyui_drag               ; DSV_CPDRAG
%else
    times DSV_PKGCALL - DSV_CPNAME db 0 ; no page yet (J5)
%endif
    dw joy_pkg                  ; DSV_PKGCALL: the door
%ifdef JOY_UI
    dw joyui_tick               ; DSV_CPTICK: the live view (SPEC.md 31.9.4)
%endif
joy_svc_end:
%ifdef JOY_UI
 %if joy_svc_end - joy_svc != DSV_MAX
    %error "joy_svc must be DSV_MAX long: it carries the page's tick"
 %endif
%elif joy_svc_end - joy_svc != DSV_FLOOR
    %error "joy_svc must be exactly the floor"
%endif

; =============================================================================
; State. Zero-only below OS88_STATE, so it costs the file nothing
; =============================================================================
    OS88_STATE
joy_blob:   times JOYB_SZ db 0  ; the settings (joypkg.inc's JOYB_*)
joy_cal:    times 4 * JCAL_SZ db 0  ; the runtime calibration, per port axis
joy_raw:    times 4 dw 0        ; the last sample, PIT counts, port order
joy_pos:    times 4 db 0        ; ...through the calibration, port order
joy_st:     times JOYS_SIZE db 0    ; ...cooked: what JOYV_STATE hands over
joy_btn:    db 0                ; the last sample's buttons, port order
joy_live:   db 0                ; the axes answering (JOYA_*)
joy_llive:  db 0                ; ...after Swap: stick A's are bits 0-1
joy_tax:    db 0                ; the PORT axes the type waits for
joy_tbm:    db 0                ; the type's buttons as a mask
joy_tnb:    db 0                ; ...as a count (INFO's CL)
joy_tns:    db 0                ; the type's sticks (INFO's CH)
joy_bprev:  db 0                ; the last cooked buttons, for the presses
joy_press:  db 0                ; presses not yet read, logical bits 0-3
            db 0
joy_tprev:  dw 0                ; the last PIT latch (joy_tick)
joy_tlast:  dw 0                ; ...and the last block's length
joy_ptick:  dw 0                ; the tick of the last probe
%ifdef JOY_UI
    JOYUI_STATE                 ; the page's own (joyui.inc)
%endif

    OS88_DRV_END
