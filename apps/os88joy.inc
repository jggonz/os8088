; =============================================================================
; os88joy.inc - the joystick, for a package (SPEC.md 137.4)
;
; **A PACKAGE REACHES THE GAME PORT THROUGH A DRIVER, NOT THROUGH PORT 201h.**
; JOYSTICK.DRV owns the port, the calibration the user made once on its
; Control Panel page, the dead zone, Invert and Swap, and the button presses
; that fell between two of your reads (SPEC.md 137.3). This file is its door,
; the os88sock.inc of DRVC_JOY: the verbs, the record and the constants are
; in apps/os88api.inc beside the class, so a package needs no driver header
; and no extra -I to use it.
;
;       %include "os88joy.inc"       ; at the END, beside os88ui.inc
;       ...
;       call joy_find                ; ONCE, at start
;       jc   .keys_only              ; no driver: kern_small, kern_emu, unticked
;       ...
;       xor  al, al                  ; stick A
;       call joy_read                ; where you read your keys
;       jc   .no_stick               ; nothing answering on that stick
;       ...                          ; AL = X, AH = Y, BL held, BH presses,
;                                    ; CL = JOYD_*
;
; **A JOYSTICK ADDS A CONTROL AND NEVER REPLACES ONE** (SPEC.md 137.8). Keep
; your keys working whatever joy_find says; joy_dirkeys below ORs the arrows
; into a JOYD_* byte, so "stick or arrows" is one question.
;
; WHAT IT COSTS. joy_find is no sample at all - one OSAPI_DRV_CALL - so it is
; cheap enough to ask at start and forget. joy_read and joy_state each take
; ONE sample: the port's one-shots timed by the PIT with interrupts off for
; as long as the expected axes take to fall, bounded by JOY_TMO (~2.3 ms,
; SPEC.md 137.3); a whole verb is 1.5-2.9 ms on the XT (SPEC.md 137.12). Once a frame is the intended rate; a sample is never taken
; behind your back, so an idle machine with the driver ticked pays nothing.
;
; A worker may call every verb here: none touches a file or takes a lock.
;
; This file is code only - no state - so it may be included anywhere a
; package's code goes.
; =============================================================================

; -----------------------------------------------------------------------------
; joy_find - is a joystick driver attached?
; out: CF=1 no (OSAPI_DRV_CALL's refusal: no DRVC_JOY driver published -
;      every kern_small and kern_emu (SPEC.md 137.1), and a machine whose row is
;      unticked)
;      CF=0 yes, and JOYV_INFO's answer: AL = the axes answering (JOYA_*),
;      AH = the type (JOYT_*), CL = buttons the type has, CH = its sticks,
;      DX = the driver's version. "Yes" with AL = 0 is a driver with nothing
;      plugged in YET - read anyway; a stick plugged in later is found
; clobbers: AX, CX, DX, flags
; -----------------------------------------------------------------------------
joy_find:
    push bx
    mov bx, (DRVC_JOY << 8) | JOYV_INFO
    call OSAPI_DRV_CALL
    pop bx
    ret

; -----------------------------------------------------------------------------
; joy_read - one sample of one stick
; in:  AL = the stick: 0 A, 1 B
; out: CF=1 that stick is not answering (or no driver)
;      CF=0 AL = X, AH = Y (signed -127..+127, + = right / down), BL = its
;      buttons held (bit 0 = its first), BH = its presses since its last read
;      (this read clears them), CL = JOYD_* direction
; clobbers: AX, BX, CL, flags (the driver keeps CH, DX, SI and DI)
; -----------------------------------------------------------------------------
joy_read:
    mov bx, (DRVC_JOY << 8) | JOYV_READ
    call OSAPI_DRV_CALL         ; (a FAR call: it cannot be a tail jump)
    ret


; -----------------------------------------------------------------------------
; joy_state - one sample of the whole port
; in:  DI = a JOYS_SIZE record in YOUR image (OSAPI_DRV_CALL hands the driver
;      your segment in ES - a heap claim's segment would never reach it)
; out: CF=0 the record filled (apps/os88api.inc's JOYS_*); CF=1 no driver
; clobbers: flags
; -----------------------------------------------------------------------------
joy_state:
    push ax
    push bx
    mov bx, (DRVC_JOY << 8) | JOYV_STATE
    call OSAPI_DRV_CALL
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; joy_dirkeys - "the stick, or the arrow keys"
; in:  AL = a JOYD_* byte (a read's CL, or 0)
; out: AL = it ORed with the arrows held now (OSAPI_KEY_DOWN, SPEC.md 9.7)
; clobbers: flags
;
; OSAPI_KEY_DOWN's map is ARMED by the first ask, so the very first call
; reports every arrow up; call it once at start if that frame matters. It is
; a LEVEL, advice rather than an oracle (os88api.inc says why) - which is all
; a direction needs.
; -----------------------------------------------------------------------------
joy_dirkeys:
    push bx
    push si
    mov bl, al
    mov si, .keys
.k:
    mov al, [cs:si]             ; the scancode... (CS = DS in a package; the
    call OSAPI_KEY_DOWN         ; override keeps this right from any include
    jnc .up                     ; site)
    or bl, [cs:si+1]            ; ...and its JOYD_* bit
.up:
    inc si
    inc si
    cmp si, .keys + 8
    jb .k
    mov al, bl
    pop si
    pop bx
    ret
.keys:
    db KSC_LEFT, JOYD_LEFT, KSC_RIGHT, JOYD_RIGHT
    db KSC_UP, JOYD_UP, KSC_DOWN, JOYD_DOWN
