; =============================================================================
; os8088 - USBMOUSE.DRV
;
; A USB mouse on a WCH CH375B host controller (SPEC.md 9.12) - the Book8088's
; one USB-A socket, which its BIOS uses to boot from a flash drive and nothing
; used for a mouse. Ports 0x260 (data) and 0x261 (command), from the Book8088
; itself and the public-domain proof of concept that first read a mouse on it
; (github.com/joshuashaffer/book8088-ch375mouse-poc at a81b753); the protocol
; is WCH's CH375 datasheet.
;
; --- WHAT IT HOOKS: NOTHING --------------------------------------------------
; No vector, no IRQ line. A WORKER TASK (SPEC.md 51.7) polls the chip and hands
; each report to the kernel through OSAPI_MOUSE_FEED, which applies it on the
; mouse's private stack. The kernel has no poll site for this driver, so a
; machine without a CH375 pays no compare on any task switch (SPEC.md 9.12.1).
;
; --- THE THREE THINGS THAT ARE NOT OBVIOUS FROM THE DATASHEET ----------------
; 1. EVERY COMMAND IS ONE IF=0 WINDOW. The chip allows 100 us between a
;    command byte and its data (TSC) and between data bytes (TSD). A tick and a
;    task switch in that gap is ~700 us on the target machine.
; 2. WAITING IS THE INT# BIT. Reading the command port answers INT# in bit 7,
;    low = pending, so SET_RETRY 25 85 lets the chip retry a NAK by itself and
;    the worker costs one `in` a tick while the mouse is still. A board where
;    that bit does not reach the bus is DETECTED, not assumed, and falls back
;    to the proof of concept's delay-then-status polling (um_wait).
; 3. ENUMERATION IS THE WORKER'S, NOT ATTACH'S. It needs a bus reset and then
;    up to half a second of the device's time, and attach runs before the first
;    paint. Attach only proves the chip is there and that nobody else - the
;    BIOS, serving a boot flash drive - has already configured what is on the
;    bus (SPEC.md 9.12.2).
;
; `make usbmousetest` assembles this with -DCH375SIM, which replaces the four
; port primitives with ch375sim.inc: a CH375 and a boot mouse as a model, so
; the whole driver runs under MartyPC (SPEC.md 9.12.6). The shipped image
; never contains the model.
;
; --- NO SERVICE TABLE (SPEC.md 9.12.5.4) -------------------------------------
; DRVC_POINT is the class drv_publish keeps NO copy of: drv_cls_svc_x refuses
; it, so attach's SI is never read and the 46 bytes of zeros and name that
; stood here were loaded and never looked at. The kernel takes this driver's
; reports through OSAPI_MOUSE_FEED and calls none of its services.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'USB Mouse', DRVC_POINT, um_entry

; --- the chip (SPEC.md 9.12) -------------------------------------------------
CH_PDAT     equ 0x260           ; data port (A0 = 0)
CH_PCMD     equ 0x261           ; command port (A0 = 1); READ, bit 7 = INT#

CMD_CHECK_EXIST  equ 0x06       ; data x -> answers ~x
CMD_RESET_ALL    equ 0x05       ; hardware reset, ~40 ms, back to device mode
CMD_SET_RETRY    equ 0x0B       ; 25h, retry byte (bit 7 = retry NAKs forever)
                                ; ...and 17h, D8h is the low-speed switch
CMD_SET_USB_ADDR equ 0x13       ; the address the chip's own tokens go to
CMD_SET_USB_MODE equ 0x15       ; 6 = host + SOF, 7 = host + bus reset
CMD_TEST_CONNECT equ 0x16       ; answers CONNECT / DISCONNECT / USB_READY
CMD_ABORT_NAK    equ 0x17       ; give up a NAK retry in progress
CMD_SET_ENDP6    equ 0x1C       ; the host's RECEIVE toggle: 80h DATA0, C0h DATA1
CMD_SET_ENDP7    equ 0x1D       ; ...and its TRANSMIT toggle
CMD_GET_STATUS   equ 0x22       ; the interrupt status, and clears INT#
CMD_RD_USB_DATA  equ 0x28       ; length, then that many bytes
CMD_WR_USB_DATA7 equ 0x2B       ; length, then that many bytes
CMD_CLR_STALL    equ 0x41       ; CLEAR_FEATURE(ENDPOINT_HALT), data = endpoint
CMD_SET_ADDRESS  equ 0x45       ; SET_ADDRESS to the device, data = address
CMD_GET_DESCR    equ 0x46       ; 1 = device, 2 = configuration
CMD_SET_CONFIG   equ 0x49       ; SET_CONFIGURATION, data = value
CMD_ISSUE_TOKEN  equ 0x4F       ; (endpoint << 4) | PID

INT_SUCCESS      equ 0x14       ; GET_STATUS answers
INT_CONNECT      equ 0x15
INT_DISCONNECT   equ 0x16
INT_USB_READY    equ 0x18       ; TEST_CONNECT: connected AND addressed
INT_NAK          equ 0x2A       ; 20h failure | 0Ah the device's NAK
INT_STALL        equ 0x2E       ; 20h failure | 0Eh the device's STALL
INT_TIMEOUT      equ 0x20       ; 20h failure, no PID: the device never answered

PID_SETUP        equ 0x0D
PID_IN           equ 0x09

; --- the driver's own numbers ------------------------------------------------
UM_ADDR     equ 2               ; the one USB address this bus will ever have
UM_BUFSZ    equ 64              ; the chip's buffer, and the descriptor ceiling
UM_IDLET    equ 9               ; ticks between TEST_CONNECTs with no device
UM_CONNT    equ 18              ; ticks a reset device has to reappear
UM_SETTLE   equ 4               ; ticks after it does, before the first request
UM_HOTT     equ 9               ; ticks after a report that the worker YIELDS
                                ; rather than sleeps, so a moving mouse is read
                                ; as fast as it reports
UM_WGOOD    equ 18              ; ticks a transaction may take with INT# known
UM_WPROBE   equ 3               ; ...the first one, which is the bit's own test
UM_WPOLL    equ 2               ; ...and in poll mode, where time is the only
                                ; signal: two ticks is at least 55 ms
UM_ERRMAX   equ 8               ; failed reads in a row before re-enumerating
UM_ENUMMAX  equ 6               ; failed enumerations before giving up on the
                                ; device until it is unplugged

UI_POLL     equ 0               ; [um_intok]: delay, then GET_STATUS
UI_GOOD     equ 1               ; ...INT# reads in bit 7
UI_PROBE    equ 2               ; ...not yet known. um_wlim is indexed by these

US_IDLE     equ 0               ; [um_state]: no device
US_ENUM     equ 1               ; a device to enumerate
US_RUN      equ 2               ; a boot mouse, being read
US_OTHER    equ 3               ; a device that is not a mouse: left alone

%if DRVV_ATTACH != 0 || DRVV_DETACH != 1
    %error "um_entry's verb test assumes ATTACH = 0 and DETACH = 1"
%endif

; =============================================================================
; ENTRY
; =============================================================================
; BX MUST COME BACK FROM EVERY VERB: drv_attach reads the ROW through BX the
; instruction after this returns, and drv_call does not save it. The descriptor
; walk used to spend BL, so an attach that refused a flash drive wrote its
; DRVE_BUSY through a garbage row pointer and left the real row looking loaded
; - which tests/usbmouse.py's busy leg caught. So NOTHING on the attach, ready
; or detach paths writes BX (um_parse keeps its flag in BP, which drv_call
; spends anyway), and nothing is banked here: every other register belongs to
; drv_load and drv_unload, which bank them all (KENT) around the call, the
; way every other driver's entry relies on.
um_entry:
    cmp al, DRVV_DETACH
    jb um_attach                ; DRVV_ATTACH, which is 0
    je um_detach
    cmp al, DRVV_READY
    je um_ready
um_ok:
    CLC_OR_STC um_no            ; a verb we do not implement is not an error
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the earliest point OSAPI_DRV_TASK's fence answers (SPEC.md
;              51.2.2), so the worker is spawned here
; out: CF = 0; CF = 1 and AL = DRVE_MEM when no task slot is free
; -----------------------------------------------------------------------------
um_ready:
    mov ax, um_worker
    xor dx, dx
    call OSAPI_DRV_TASK
    mov al, DRVE_MEM
    jc um_no
    inc byte [um_alive]         ; 0 -> 1, HERE and not in the worker: a
    ret                         ; detach between the spawn and its first
                                ; instruction must know a worker exists and
                                ; leave the chip to it. CF = 0 from the spawn:
                                ; neither `mov` nor `inc` writes CF

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6)
;
; The worker owns the chip while it is alive, so detach only raises the stop
; byte: drv_unload then waits on [drv_wcnt] (SPEC.md 51.7) while the worker
; releases any button, aborts and resets the chip, and exits. With no worker
; there is nobody to do that, so detach does it - and returns CF = 0 either
; way: `cmp` with 0 never sets CF, and nothing in um_reset writes a flag.
; -----------------------------------------------------------------------------
um_detach:
    inc byte [um_stop]          ; 0 -> 1: a driver is detached once
    cmp byte [um_alive], 0
    jne um_ok
    ; FALL THROUGH into um_reset

; -----------------------------------------------------------------------------
; um_reset - the chip back as it powered up: device mode, bus released
;
; ABORT_NAK first, because RESET_ALL is a command like any other and a chip
; retrying a NAK is not listening for one.
; -----------------------------------------------------------------------------
um_reset:
    mov al, CMD_ABORT_NAK       ; a command with no data and no answer is one
    call ch_cmd                 ; `out`, so it needs no IF=0 window: there is
    mov al, CMD_RESET_ALL       ; no gap after it for a tick to land in
    jmp ch_cmd

; um_gst - GET_STATUS: out AL = the interrupt status, and INT# cleared
um_gst:
    mov ah, CMD_GET_STATUS
    jmp um_cr

; -----------------------------------------------------------------------------
; um_drain - consume an interrupt nobody asked for (a plug, an unplug)
; -----------------------------------------------------------------------------
um_drain:
    cmp byte [um_intok], UI_POLL
    je um_gst                   ; blind: GET_STATUS with nothing pending is
    call ch_st                  ; harmless
    test al, 0x80
    jz um_gst
    ret

; =============================================================================
; ATTACH, AND THE MEMORY IT LEAVES BEHIND
;
; The code from here to um_attach.live runs ONCE PER IMAGE: drv_load reads the
; image off the disk and attaches it, nothing sends DRVV_ATTACH to an image
; twice (a reload, hibernate's included, is a fresh read), and um_entry is the
; only way in. So once attach has returned, those bytes are free, and they are
; where the driver keeps
;   um_buf      the 64-byte descriptor and report buffer, and after it
;   um_scratch  the state that is always WRITTEN BEFORE IT IS READ - so it
;               needs no assembled value and can start out as code
; (SPEC.md 9.12.5.4). The one write into either that happens WHILE attach runs
; is attach's own descriptor read and walk, and those return to .live, past
; the end of both, which the assembly checks below. Nothing may be added to
; um_scratch that is read before something writes it: a byte that needs to
; start at zero belongs at the end of the image, with um_stop and um_btn.
; =============================================================================
um_buf:
; -----------------------------------------------------------------------------
; um_spinwait - um_wait for ATTACH: no scheduler, no clock
; out: AL = the interrupt status (INT_TIMEOUT if none came)
;
; Attach can run inside kmain's drv_boot, where neither a yield nor [ticks] can
; be relied on, so the bound is an ITERATION COUNT: 65,535 reads of the status
; port is ~0.5 s on a 4.77 MHz 8088 and less on anything faster, against a
; GET_DESCR that takes milliseconds. In poll mode the spin is the delay.
; -----------------------------------------------------------------------------
um_spinwait:
    mov ah, [um_intok]          ; AH = UI_POLL or not: ch_st keeps AH, and
    mov cx, 0xFFFF              ; attach may not spend BX (um_entry)
.w:
    call ch_st
    or ah, ah                   ; UI_POLL: the spin is the delay
    jz .next
    test al, 0x80
    jz um_gst
.next:
    loop .w
    or ah, ah
    jz um_gst
    call um_gst                 ; ...and a timeout as um_wait takes one:
    jmp um_wait.stuck           ; GET_STATUS, ABORT_NAK, INT_TIMEOUT

; -----------------------------------------------------------------------------
; DRVV_ATTACH - is there a CH375, and is it free? (SPEC.md 9.12.2)
; out: CF = 0; CF = 1 and AL = DRVE_HW / DRVE_BUSY. SI is not set: see the
;      file header - this class has no service table to point at
;
; ALL-OR-NOTHING (SPEC.md 51.6): nothing is hooked, and on a refusal nothing
; has been written that changes what is on the bus. CHECK_EXIST is an echo;
; TEST_CONNECT and GET_DESCR read.
;
; The image was read off the disk by the drv_load that called this, so every
; byte of state below is still its assembled value: there is nothing to reset.
; -----------------------------------------------------------------------------
um_attach:
    mov ax, CMD_CHECK_EXIST * 256 + 0x57
    pushf                       ; the echo is read inside the command's own
    cli                         ; IF=0 window
    call um_cwi
    call ch_rd
    popf
    cmp al, 0xA8                ; ~0x57. An undriven 0x260 floats to 0xFF
    mov al, DRVE_HW
    jne um_no

    ; --- CAN WE SEE INT#? No transaction is pending, so once any stale
    ; interrupt is consumed (um_drain, [um_intok] being UI_PROBE) bit 7 must
    ; read HIGH. A bit that stays low is a bit that does not reach the bus,
    ; and poll mode is decided here rather than after a first transaction
    ; that would read garbage as a completion.
    call um_drain
    call ch_st
    test al, 0x80
    jnz .bitok
    mov byte [um_intok], UI_POLL
.bitok:

    ; --- WHOSE IS IT? A device the chip already calls READY was configured by
    ; somebody. Read its configuration at its CURRENT address - no reset - and
    ; take the bus only if it is a mouse.
    call um_conn
    cmp al, INT_USB_READY       ; READY alone: a plain CONNECT is nobody's yet
    jne .ok
    mov ax, CMD_GET_DESCR * 256 + 2
    call um_cw
    call um_spinwait            ; attach may run before the scheduler: no yield
    cmp al, INT_SUCCESS
    jne .ok                     ; nothing answered: nobody we can see is using it
    call um_rdata               ; ...WHICH WRITES OVER ALL OF THE ABOVE
.live:                          ; (um_buf): from here on it must be intact
    call um_parse               ; CF is the walk's: a mouse is ours, and
    mov al, DRVE_BUSY           ; anything else - a flash drive, a keyboard -
    ret                         ; is the BIOS's
.ok:
    clc
    ret

um_scratch  equ um_buf + UM_BUFSZ
um_t0       equ um_scratch      ; dw: when the IN token went out (um_run)
um_pend     equ um_scratch + 2  ; db: an IN token is out    } ONE WORD:
um_err      equ um_scratch + 3  ; db: failed reads in a row } um_enum clears both
um_rx       equ um_scratch + 4  ; dw: the halving's carried remainders,
um_ry       equ um_scratch + 6  ; dw: ...um_enum clears both
um_epa      equ um_scratch + 8  ; db: the endpoint address, 81h-8Fh (um_parse)
um_cfgw     equ um_scratch + 9  ; dw: SET_CONFIG and its value, as one word
um_cfgv     equ um_cfgw         ; ...whose low byte um_parse finds
UM_SCRATCH  equ 11

%if um_scratch + UM_SCRATCH > um_attach.live
    %error "um_buf and um_scratch run past um_attach.live: attach's own read would overwrite the code it returns to"
%endif

; =============================================================================
; THE WORKER (SPEC.md 9.12.3)
; =============================================================================
um_worker:
    mov al, 6                   ; host mode, SOF on, connect detection live.
    call um_mode                ; A chip the BIOS left in host mode takes this
                                ; as a no-op
.loop:
    call OSAPI_TASK_PARK        ; the heap compactor waits on this (SPEC.md
                                ; 66.5.5); nothing here holds a pointer across it
    cmp byte [um_stop], 0
    jne .die
    call um_pass
    jmp short .loop

.die:                           ; [um_alive] stays 1: only detach reads it, and
    call um_release             ; detach is what raised the stop byte. A button
    call um_reset               ; held down must not outlive us
    xor ax, ax                  ; AX = 0: this worker is exiting (SPEC.md
    call OSAPI_DRV_TASK         ; 51.7). NEVER RETURNS

; um_pass - one pass of whichever state the worker is in: a tail jump into
; that state's body, whose `ret` is this routine's
um_pass:
    mov al, [um_state]
    cmp al, US_RUN
    je um_run
    cmp al, US_ENUM
    jne um_idle                 ; US_IDLE and US_OTHER share a body: both are
    jmp um_enum                 ; waiting for the bus to change

; -----------------------------------------------------------------------------
; um_idle - US_IDLE / US_OTHER: watch the bus, cheaply
;
; TEST_CONNECT answers directly and needs no INT#, so it serves both modes.
; Any interrupt left pending (the plug or unplug that got us here) is consumed
; so INT# is clean for the enumeration. In US_OTHER a DISCONNECT is the only
; answer that moves us: whatever is there is not ours to reset again.
; -----------------------------------------------------------------------------
um_idle:
    call um_drain
    call um_conn
    je .dev
    cmp al, INT_DISCONNECT
    jne .sleep
    mov word [um_state], US_IDLE    ; ...and [um_nenum] = 0: a new device gets
    jmp short .sleep                ; a fresh set of attempts
.dev:
    cmp byte [um_state], US_OTHER
    je .sleep
    inc byte [um_state]         ; US_IDLE -> US_ENUM, the only other state
    ret                         ; that comes here
.sleep:
    mov ax, UM_IDLET
    jmp short um_sleep

; um_conn - TEST_CONNECT; out AL = its answer, ZF = 1 a device is there
; (CONNECT, or USB_READY: connected and already addressed)
um_conn:
    mov ah, CMD_TEST_CONNECT
    call um_cr
    cmp al, INT_CONNECT
    je .out
    cmp al, INT_USB_READY
.out:
    ret

; um_nap / um_sleep - OSAPI_TASK_SLEEP for one tick / AX ticks. CALL the slot,
; never jmp: it ends in retf. It keeps every register but the flags (SPEC.md
; 1's rule for a public routine; task_sleep banks AX and BX and names no other)
um_nap:
    mov ax, 1
um_sleep:
    call OSAPI_TASK_SLEEP
    ret

; -----------------------------------------------------------------------------
; um_run - US_RUN: one pass of issue-wait-read (SPEC.md 9.12.3)
;
; THE HOT PATH: a moving mouse comes round here as fast as the scheduler
; allows. The issue is two whole words (command and data) out of [um_togw] and
; [um_tokw], the token having been composed once by um_parse.
; -----------------------------------------------------------------------------
um_run:
    cmp byte [um_pend], 0
    jne .wait
    mov ax, [um_togw]           ; --- issue: the toggle, then the IN token ---
    call um_cw
    mov ax, [um_tokw]
    call um_cw
    inc byte [um_pend]          ; 0 -> 1: this pass found it 0
    call OSAPI_GET_TICKS
    mov [um_t0], ax

.wait:
    cmp byte [um_intok], UI_POLL
    je .poll
    call ch_st
    test al, 0x80
    jz .ready                   ; INT# low: the transaction finished
.rest:
    call OSAPI_GET_TICKS        ; nothing yet. HOT: yield, and be back as soon
    sub ax, [um_lrep]           ; as everybody else has had a turn. COLD: a
    cmp ax, UM_HOTT             ; whole tick
    jae um_nap
    call OSAPI_TASK_YIELD
    ret
.poll:
    call OSAPI_GET_TICKS
    sub ax, [um_t0]
    cmp ax, UM_WPOLL
    jb .rest

.ready:
    dec byte [um_pend]          ; 1 -> 0: only an issued token gets here
    call um_gst
    cmp al, INT_SUCCESS
    je .rep
    cmp al, INT_NAK             ; poll mode's "nothing to say": issue again
    je .out
    cmp al, INT_DISCONNECT
    je .gone
    cmp al, INT_CONNECT
    je .reenum
    cmp al, INT_STALL
    je .stall
    and al, 0x07                ; a failure carrying a DATA PID - 03h DATA0 or
    cmp al, 0x03                ; 0Bh DATA1, the two PIDs whose low three bits
    je .flip                    ; are 011 - is a toggle the device and we
                                ; disagree about: take its word
    inc byte [um_err]
    cmp byte [um_err], UM_ERRMAX
    jb .out
.reenum:
    mov al, US_ENUM
    jmp short .drop
.gone:
    mov al, US_IDLE
.drop:
    mov [um_state], al
    jmp um_release
.flip:
    xor byte [um_tog], 0x40
.out:
    ret
.stall:
    mov ah, CMD_CLR_STALL
    mov al, [um_epa]            ; the endpoint ADDRESS, direction bit and all
    call um_req
    mov byte [um_tog], 0x80     ; a cleared halt restarts at DATA0
    ret

.rep:
    xor byte [um_tog], 0x40
    mov byte [um_err], 0
    call um_rdata
    cmp al, 3                   ; boot layout: buttons, dx, dy (and maybe more)
    jb .out
    call OSAPI_GET_TICKS
    mov [um_lrep], ax
    ; FALL THROUGH into um_report

; -----------------------------------------------------------------------------
; um_report - [um_buf] as a boot report -> OSAPI_MOUSE_FEED (SPEC.md 9.12.3)
;
; HALVED WITH THE REMAINDER CARRIED: total = delta + carry, out = total sar 1,
; carry = the bit `sar` shifts out (two's complement makes that the remainder
; for either sign). A slow creep of 1s alternates 0 and 1, where a plain shift
; would drop it entirely. dy first, so it can be parked in BX with one xchg.
; -----------------------------------------------------------------------------
um_report:
    mov al, [um_buf+2]
    mov si, um_ry
    call um_half
    xchg ax, bx                 ; dy, scaled - the HID convention is the
    mov al, [um_buf+1]          ; screen's: positive is down
    dec si
    dec si                      ; um_rx, the word before
    call um_half                ; AX = dx, scaled
    mov cl, [um_buf]
    and cl, 0x03                ; left, right - mouse_btn's own two bits
.cmp:
    cmp cl, [um_btn]
    jne .feed
    mov dx, ax
    or dx, bx
    jz .none                    ; moves nothing, changes nothing: not fed
.feed:
    mov [um_btn], cl
    call OSAPI_MOUSE_FEED
.none:
    ret

; um_half - AL = a signed count, SI -> its carried remainder (0 or 1); out
; AX = the count plus the remainder, halved, and [SI] = the bit that fell off
um_half:
    cbw
    add ax, [si]
    xor dx, dx
    sar ax, 1
    adc dx, dx
    mov [si], dx
    ret

; -----------------------------------------------------------------------------
; um_release - feed a release if a button is down
;
; A button held across an unplug, a re-enumeration or a detach would otherwise
; be a drag that never ends: the kernel only learns of a release from a report.
; It IS a report - no movement, no buttons - so um_report's own test decides:
; fed when a button was down, not fed when none was.
; -----------------------------------------------------------------------------
um_release:
    xor ax, ax
    xor bx, bx
    xor cx, cx
    jmp short um_report.cmp

; -----------------------------------------------------------------------------
; um_enum - US_ENUM: reset, address, configure, boot protocol (SPEC.md 9.12.3)
;
; A raised stop byte makes every um_req answer a timeout, so it ends in .fail
; like any other refusal; the worker's loop then sees the byte and exits, and
; its RESET_ALL is what the chip is left with whatever this had done.
; -----------------------------------------------------------------------------
um_enum:
    call um_drain
    mov al, 7                   ; host mode + bus RESET, held until the next
    call um_mode2               ; mode: >= 55 ms of it, where USB wants 10
    mov al, 6
    call um_mode2

    mov cx, UM_CONNT            ; --- the device comes back ---
.back:
    cmp byte [um_stop], 0
    jne .out
    call um_conn
    je .up
    call um_nap                 ; keeps CX
    loop .back
    dec byte [um_state]         ; US_ENUM -> US_IDLE: it did not come back -
                                ; unplugged during the reset
.out:
    ret
.up:
    call um_drain               ; the connect interrupt, consumed
    mov ax, UM_SETTLE
    call um_sleep

    test byte [um_nenum], 1     ; LOW SPEED ON EVEN ATTEMPTS, FULL ON ODD: most
    jnz .full                   ; mice are 1.5 Mbps and the datasheet documents
    mov ax, CMD_SET_RETRY * 256 + 0x17  ; 12 Mbps only, so the proof of
    mov bl, 0xD8                ; concept's switch goes first - and a
    call um_c2                  ; full-speed mouse, which that switch breaks,
.full:                          ; gets the next try
    mov ax, CMD_GET_DESCR * 256 + 1     ; --- device descriptor, at address 0
    call um_req
    jne .fail
    call um_rdata               ; read and dropped: the chip wants it taken

    mov ax, CMD_SET_ADDRESS * 256 + UM_ADDR ; --- address ---
    call um_req
    jne .fail
    mov ax, CMD_SET_USB_ADDR * 256 + UM_ADDR
    call um_cw

    mov ax, CMD_GET_DESCR * 256 + 2     ; --- configuration, at most UM_BUFSZ
    call um_req
    jne .fail
    call um_rdata
    call um_parse
    mov al, US_OTHER            ; not a mouse: left alone until unplugged
    jc .set

    mov ax, [um_cfgw]           ; --- configure ---
    call um_req
    jne .fail

    mov al, 0x0B                ; --- the two class requests, each allowed to
    call um_ctl                 ; STALL: a boot-only mouse may refuse
                                ; SET_PROTOCOL(boot) and is still a boot mouse
    mov al, 0x0A                ; SET_IDLE(0)
    call um_ctl

    mov bl, 0x85                ; --- how a NAK is answered from here on ---
    cmp byte [um_intok], UI_POLL    ; INT# readable: the chip retries a NAK
    jne .retry                      ; forever and completes when the mouse
    mov bl, 0x05                    ; speaks. Poll mode: a NAK comes back at
.retry:                             ; once as INT_NAK. Both keep 5 timeout
    mov ax, CMD_SET_RETRY * 256 + 0x25  ; retries
    call um_c2

    mov word [um_state], US_RUN ; ...and [um_nenum] = 0
    mov byte [um_tog], 0x80     ; DATA0 after SET_CONFIGURATION
    xor ax, ax
    mov [um_pend], ax           ; ...and [um_err]
    mov [um_rx], ax
    mov [um_ry], ax
    ret
.fail:
    inc byte [um_nenum]         ; ...and the other speed next time
    cmp byte [um_nenum], UM_ENUMMAX
    mov al, US_IDLE             ; TEST_CONNECT sends us straight back, a
    jb .set                     ; UM_IDLET later
    mov al, US_OTHER            ; enough: leave it alone until unplugged
.set:
    mov [um_state], al
um_ret:                         ; ...a `ret` um_ctl borrows
    ret

; =============================================================================
; TRANSACTIONS
; =============================================================================

; -----------------------------------------------------------------------------
; um_ctl - one class request with no data stage: SETUP, then the status IN
; in:  AL = bRequest; [um_pkt+4] = the interface, which um_parse put there
; out: ZF = 1 the status stage succeeded (a STALL is an ordinary answer, and
;      both callers ignore it)
; -----------------------------------------------------------------------------
um_ctl:
    mov [um_pkt+1], al
    mov ax, CMD_SET_ENDP7 * 256 + 0x80  ; SETUP is always DATA0
    call um_cw
    pushf
    cli
    mov ax, CMD_WR_USB_DATA7 * 256 + 8
    call um_cwi
    mov si, um_pkt
    mov cx, 8
    cld
.wr:
    lodsb
    call ch_wr
    loop .wr
    popf
    mov ax, CMD_ISSUE_TOKEN * 256 + PID_SETUP   ; endpoint 0
    call um_req
    jne um_ret
    mov ax, CMD_SET_ENDP6 * 256 + 0xC0  ; the status stage is DATA1
    call um_cw
    mov ax, CMD_ISSUE_TOKEN * 256 + PID_IN      ; endpoint 0
    ; FALL THROUGH into um_req

; -----------------------------------------------------------------------------
; um_req - AH = a command, AL = its data byte: send it, then um_wait
; um_wait - wait for a transaction to finish, and take its status
; out: AL = the interrupt status; ZF = 1 it was INT_SUCCESS. Clobbers BX, SI.
;      The stop byte going up answers INT_TIMEOUT, after an ABORT_NAK
;
; The three [um_intok] states are three ways of knowing a transaction is over:
;   UI_GOOD   INT# low, with UM_WGOOD ticks before an ABORT_NAK
;   UI_PROBE  INT# low within UM_WPROBE ticks proves the bit (-> UI_GOOD); a
;             GET_STATUS that answers SUCCESS after the bit never moved proves
;             it DEAD (-> UI_POLL), because the chip finished and did not say
;   UI_POLL   UM_WPOLL ticks, then GET_STATUS - the proof of concept's shape
; -----------------------------------------------------------------------------
um_req:
    call um_cw
um_wait:
    call OSAPI_GET_TICKS
    xchg ax, si                 ; SI = the tick it started: nothing in the
.w:                             ; loop spends SI, and the slots keep it
    cmp byte [um_stop], 0
    jne .stuck
    mov bl, [um_intok]
    or bl, bl                   ; UI_POLL
    jz .late
    call ch_st
    test al, 0x80
    jz .ready
.late:
    call OSAPI_GET_TICKS
    sub ax, si
    or ah, ah
    jnz .timeout                ; 256 ticks and more: past every limit
    xor bh, bh
    cmp al, [bx+um_wlim]        ; this state's limit, in ticks
    jae .timeout
    or bl, bl                   ; UI_POLL
    jz .nap
    call OSAPI_TASK_YIELD       ; a transaction is milliseconds: stay close
    jmp short .w
.nap:
    call um_nap
    jmp short .w
.ready:
    cmp bl, UI_PROBE
    jne .status
    dec byte [um_intok]         ; UI_PROBE -> UI_GOOD: the bit moved, so it
                                ; is real
.status:
    call um_gst
    jmp short .done
.timeout:
    call um_gst
    or bl, bl                   ; UI_POLL
    jz .done                    ; poll mode: this IS the answer
    cmp al, INT_SUCCESS
    jne .stuck
    cmp bl, UI_PROBE
    jne .done
    mov byte [um_intok], UI_POLL    ; finished, and INT# never said so
    jmp short .done
.stuck:
    mov al, CMD_ABORT_NAK       ; a device NAKing forever: stop the chip
    call ch_cmd                 ; retrying, and report a timeout
    mov al, INT_TIMEOUT
.done:
    cmp al, INT_SUCCESS
    ret


; -----------------------------------------------------------------------------
; um_rdata - RD_USB_DATA into [um_buf], at most UM_BUFSZ kept
; out: AL = how many were kept, DI = one past the last of them
; -----------------------------------------------------------------------------
um_rdata:
    pushf
    cli
    mov al, CMD_RD_USB_DATA
    call ch_cmd
    call ch_rd
    xor ah, ah
    mov cx, ax                  ; what the chip will send, all of which must
    mov di, um_buf              ; be read whatever we keep
    jcxz .done
.rd:
    call ch_rd
    cmp di, um_buf + UM_BUFSZ
    jae .skip
    mov [di], al
    inc di
.skip:
    loop .rd
.done:
    mov ax, di                  ; DI = one past the last byte kept, which
    sub ax, um_buf              ; um_parse takes as its end
    popf                        ; (popf writes no register: AL is the count)
    ret

; -----------------------------------------------------------------------------
; um_parse - find a boot mouse in the configuration descriptor in [um_buf]
; in:  DI = one past its last byte, as um_rdata leaves it: both callers call
;      um_parse the instruction after um_rdata
; out: CF = 0 and [um_cfgw], [um_pkt+4], [um_epa], [um_tok] set; CF = 1 no
;      boot mouse here
;
; A descriptor CHAIN, walked by bLength, and not the proof of concept's fixed
; offsets: a HID descriptor sits between the interface and its endpoint, and a
; composite device puts other interfaces first. The mouse is interface class
; 3 protocol 2; its endpoint is the first interrupt IN after it. No descriptor
; is followed past what was kept.
; -----------------------------------------------------------------------------
um_parse:
    mov word [um_cfgw], CMD_SET_CONFIG * 256 + 1
    mov si, um_buf
    mov dx, di                  ; DX = one past the last byte kept
    xor bp, bp                  ; BP = 1 inside a mouse's interface. NOT BX:
                                ; attach calls this (see um_entry)
.next:
    mov al, [si]                ; bLength
    cmp al, 2
    jb .none
    xor ah, ah
    add ax, si
    cmp ax, dx
    ja .none                    ; runs past what was kept
    xchg ax, di                 ; DI = the descriptor after this one
    mov ax, [si+1]              ; AL = bDescriptorType, AH = the byte after it
    cmp al, 2
    jne .notcfg
    mov al, [si+5]              ; bConfigurationValue
    mov [um_cfgv], al
    jmp short .adv
.notcfg:
    cmp al, 4
    jne .notif
    xor bp, bp
    cmp byte [si+5], 3          ; bInterfaceClass HID
    jne .adv
    cmp byte [si+7], 2          ; bInterfaceProtocol mouse
    jne .adv
    mov [um_pkt+4], ah          ; bInterfaceNumber: wIndex of both requests
    inc bp
    jmp short .adv
.notif:
    cmp al, 5
    jne .adv
    or bp, bp
    jz .adv
    test ah, 0x80               ; bEndpointAddress: IN
    jz .adv
    mov al, [si+3]
    and al, 3
    cmp al, 3                   ; interrupt
    jne .adv
    mov [um_epa], ah            ; CLR_STALL's operand, as it stands
    mov cl, 4                   ; ...and um_run's IN token, (ep << 4) | IN:
    shl ah, cl                  ; the direction bit shifts out of the byte
    or ah, PID_IN               ; CF = 0: `or` clears it
    mov [um_tok], ah
    ret
.adv:
    mov si, di                  ; ...and no `cmp si, dx` here: at the end, SI
    jmp short .next             ; = DX, and .next refuses any bLength >= 2
.none:                          ; that would run past it (reading one byte
    stc                         ; there, which is ours)
    ret

; =============================================================================
; COMMANDS - each one IF=0 window, for the datasheet's TSC/TSD
; =============================================================================

; um_cw - AH = a command, AL = its one data byte
um_cw:
    pushf
    cli
    call um_cwi
    popf
    ret

; um_c2 - AH = a command, AL then BL = its two data bytes
um_c2:
    pushf
    cli
    call um_cwi
    mov al, bl
    call ch_wr
    popf
    ret

; um_cr - AH = a command with no data; out AL = its one answer
um_cr:
    pushf
    cli
    mov al, ah
    call ch_cmd
    call ch_rd
    popf
    ret

; um_cwi - um_cw's body, for a caller already inside its IF=0 window
um_cwi:
    push ax
    mov al, ah
    call ch_cmd
    pop ax
    jmp ch_wr

; um_mode2 - um_mode, then two ticks for the bus to act on it
um_mode2:
    call um_mode
    mov ax, 2
    jmp um_sleep

; um_mode - SET_USB_MODE, AL = the mode. The answer comes up to 20 us after
; the data byte (TE2), which a fast machine can beat, so the read waits a
; moment - OUTSIDE the IF=0 window, which bounds only command-to-data.
um_mode:
    mov ah, CMD_SET_USB_MODE
    call um_cw
    mov cx, 64
.d:
    loop .d
    jmp ch_rd                   ; CMD_RET_SUCCESS (51h); nothing to do with it

; =============================================================================
; THE FOUR PORT PRIMITIVES. Everything above reaches the chip through these
; and nothing else, which is what lets the gate build put a model under them.
; Each preserves every register but AL and DX - and nothing above holds a
; value in DX across one (the model, which banks DX too, is the stricter).
; =============================================================================
%ifdef CH375SIM
%include "ch375sim.inc"
%else
ch_cmd:
    mov dx, CH_PCMD
    out dx, al
    ret
ch_wr:
    mov dx, CH_PDAT
    out dx, al
    ret
ch_rd:
    mov dx, CH_PDAT
    in al, dx
    ret
ch_st:
    mov dx, CH_PCMD
    in al, dx
    ret
%endif

; =============================================================================
; State. A driver's zeroed data is stripped from the file and zeroed at load
; (drivers/os88drv.inc), so what starts at zero goes last. The buffer is not
; here: it is um_buf, over attach.
; =============================================================================
um_pkt:     db 0x21, 0, 0, 0, 0, 0, 0, 0 ; class, interface: bRequest +1,
                                         ; wIndex = the interface +4
um_wlim:    db UM_WPOLL, UM_WGOOD, UM_WPROBE ; um_wait's limits, by [um_intok]
um_intok    db UI_PROBE
um_togw:                        ; um_run's first command as one word:
um_tog      db 0x80             ; SET_ENDP6's byte: 80h DATA0, C0h DATA1
            db CMD_SET_ENDP6
um_tokw:                        ; ...and its second
um_tok      db 0                ; (ep << 4) | PID_IN, composed by um_parse
            db CMD_ISSUE_TOKEN

um_state    db US_IDLE          ; THESE TWO ARE ONE WORD: um_idle and um_enum
um_nenum    db 0                ; write both. Failed enumerations of this device
um_stop     db 0                ; detach raised it: the worker exits
um_alive    db 0                ; a worker exists (set at spawn)
um_btn      db 0                ; the buttons the kernel was last told
um_lrep     dw 0                ; the tick of the last report
                                ; (the rest of the state is um_scratch, above)

%if um_nenum != um_state + 1
    %error "um_state and um_nenum are written as one word"
%endif
%if UI_POLL != 0 || UI_GOOD != 1 || UI_PROBE != 2
    %error "um_wlim is indexed by [um_intok], and um_wait decrements PROBE to GOOD"
%endif
%if US_IDLE != 0 || US_ENUM != 1
    %error "um_idle increments US_IDLE to US_ENUM and um_enum decrements it back"
%endif

    OS88_DRV_END
