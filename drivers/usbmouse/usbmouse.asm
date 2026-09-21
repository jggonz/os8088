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

UI_GOOD     equ 1               ; [um_intok]: INT# reads in bit 7
UI_PROBE    equ 2               ; ...not yet known
UI_POLL     equ 0               ; ...does not: delay, then GET_STATUS

US_IDLE     equ 0               ; [um_state]: no device
US_ENUM     equ 1               ; a device to enumerate
US_RUN      equ 2               ; a boot mouse, being read
US_OTHER    equ 3               ; a device that is not a mouse: left alone

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). Nothing in it: the kernel never calls this
; driver's services, it only takes its reports. DSV_NAME names the row's sink
; the way every driver's does.
; -----------------------------------------------------------------------------
um_svc:
    dw 0, 0, 0, 0, 0            ; DSV_CAPS .. DSV_RELINST
    dw um_name                  ; DSV_NAME
    times DSV_SIZE - ($ - um_svc) db 0

um_name:    db 'USB Mouse', 0

; =============================================================================
; ENTRY
; =============================================================================
; BX, CX, DX AND DI ARE BANKED FOR EVERY VERB, and BX is the one that matters:
; drv_attach reads the ROW through BX the instruction after this returns, and
; drv_call does not save it. The descriptor walk spends BL, so an attach that
; refused a flash drive wrote its DRVE_BUSY through a garbage row pointer and
; left the real row looking loaded - which tests/usbmouse.py's busy leg caught.
um_entry:
    push bx
    push cx
    push dx
    push di
    call .verb
    pop di
    pop dx
    pop cx
    pop bx
    ret                         ; pops write no flags: CF is the verb's
.verb:
    cmp al, DRVV_ATTACH
    je  um_attach
    cmp al, DRVV_DETACH
    je  um_detach
    cmp al, DRVV_READY
    je  um_ready
    clc                         ; a verb we do not implement is not an error
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - is there a CH375, and is it free? (SPEC.md 9.12.2)
; out: CF = 0 and SI = the service table; CF = 1 and AL = DRVE_HW / DRVE_BUSY
;
; ALL-OR-NOTHING (SPEC.md 51.6): nothing is hooked, and on a refusal nothing
; has been written that changes what is on the bus. CHECK_EXIST is an echo;
; TEST_CONNECT and GET_DESCR read.
; -----------------------------------------------------------------------------
um_attach:
    mov ah, CMD_CHECK_EXIST
    mov al, 0x57
    call um_cwr
    cmp al, 0xA8                ; ~0x57. An undriven 0x260 floats to 0xFF
    jne .nohw

    ; --- CAN WE SEE INT#? No transaction is pending, so once any stale
    ; interrupt is consumed bit 7 must read HIGH. A bit that stays low is a bit
    ; that does not reach the bus, and poll mode is decided here rather than
    ; after a first transaction that would read garbage as a completion.
    mov byte [um_intok], UI_PROBE
    call ch_st
    test al, 0x80
    jnz .bitok
    mov ah, CMD_GET_STATUS
    call um_cr
    call ch_st
    test al, 0x80
    jnz .bitok
    mov byte [um_intok], UI_POLL
.bitok:

    ; --- WHOSE IS IT? A device the chip already calls READY was configured by
    ; somebody. Read its configuration at its CURRENT address - no reset - and
    ; take the bus only if it is a mouse.
    mov ah, CMD_TEST_CONNECT
    call um_cr
    cmp al, INT_USB_READY
    jne .ours
    mov ah, CMD_GET_DESCR
    mov al, 2
    call um_cw
    call um_spinwait            ; attach may run before the scheduler: no yield
    cmp al, INT_SUCCESS
    jne .ours                   ; nothing answered: nobody we can see is using it
    call um_rdata
    call um_parse
    jc .busy                    ; a flash drive, a keyboard: the BIOS's
.ours:
    mov byte [um_stop], 0
    mov byte [um_state], US_IDLE
    mov si, um_svc
    clc
    ret
.busy:
    mov al, DRVE_BUSY
    stc
    ret
.nohw:
    mov al, DRVE_HW
    stc
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
    jc .no
    mov byte [um_alive], 1      ; HERE and not in the worker: a detach between
    clc                         ; the spawn and its first instruction must know
    ret                         ; a worker exists and leave the chip to it
.no:
    mov al, DRVE_MEM
    stc
    ret

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6)
;
; The worker owns the chip while it is alive, so detach only raises the stop
; byte: drv_unload then waits on [drv_wcnt] (SPEC.md 51.7) while the worker
; releases any button, aborts and resets the chip, and exits. With no worker
; there is nobody to do that, so detach does it.
; -----------------------------------------------------------------------------
um_detach:
    mov byte [um_stop], 1
    cmp byte [um_alive], 0
    jne .out
    call um_reset
.out:
    clc
    ret

; =============================================================================
; THE WORKER (SPEC.md 9.12.3)
; =============================================================================
um_worker:
    mov ah, CMD_SET_USB_MODE    ; host mode, SOF on, connect detection live.
    mov al, 6                   ; A chip the BIOS left in host mode takes this
    call um_mode                ; as a no-op
.loop:
    call OSAPI_TASK_PARK        ; the heap compactor waits on this (SPEC.md
                                ; 66.5.5); nothing here holds a pointer across it
    cmp byte [um_stop], 0
    jne .die
    mov al, [um_state]
    cmp al, US_RUN
    jne .notrun
    call um_run
    jmp short .loop
.notrun:
    cmp al, US_ENUM
    jne .notenum
    call um_enum
    jmp short .loop
.notenum:
    call um_idle                ; US_IDLE and US_OTHER share a body: both are
    jmp short .loop             ; waiting for the bus to change

.die:
    call um_release             ; a button held down must not outlive us
    call um_reset
    mov byte [um_alive], 0
    xor ax, ax                  ; AX = 0: this worker is exiting (SPEC.md
    call OSAPI_DRV_TASK         ; 51.7). NEVER RETURNS

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
    mov ah, CMD_TEST_CONNECT
    call um_cr
    cmp al, INT_DISCONNECT
    jne .dev
    mov byte [um_state], US_IDLE
    mov byte [um_nenum], 0      ; a new device gets a fresh set of attempts
    jmp short .sleep
.dev:
    cmp byte [um_state], US_OTHER
    je .sleep
    cmp al, INT_CONNECT
    je .enum
    cmp al, INT_USB_READY
    jne .sleep
.enum:
    mov byte [um_state], US_ENUM
    ret
.sleep:
    mov ax, UM_IDLET
    call OSAPI_TASK_SLEEP       ; CALL, never jmp: the slot ends in retf
    ret

; -----------------------------------------------------------------------------
; um_enum - US_ENUM: reset, address, configure, boot protocol (SPEC.md 9.12.3)
; -----------------------------------------------------------------------------
um_enum:
    call um_drain
    mov ah, CMD_SET_USB_MODE
    mov al, 7                   ; host mode + bus RESET, held until the next mode
    call um_mode
    mov ax, 2
    call OSAPI_TASK_SLEEP       ; >= 55 ms of reset; USB wants 10
    mov ah, CMD_SET_USB_MODE
    mov al, 6
    call um_mode
    mov ax, 2
    call OSAPI_TASK_SLEEP

    mov cx, UM_CONNT            ; --- the device comes back ---
.back:
    cmp byte [um_stop], 0
    jne .out
    mov ah, CMD_TEST_CONNECT
    call um_cr
    cmp al, INT_CONNECT
    je .up
    cmp al, INT_USB_READY
    je .up
    push cx
    mov ax, 1
    call OSAPI_TASK_SLEEP
    pop cx
    loop .back
    mov byte [um_state], US_IDLE    ; it did not: unplugged during the reset
    ret
.up:
    call um_drain               ; the connect interrupt, consumed
    mov ax, UM_SETTLE
    call OSAPI_TASK_SLEEP

    test byte [um_nenum], 1     ; LOW SPEED ON EVEN ATTEMPTS, FULL ON ODD: most
    jnz .full                   ; mice are 1.5 Mbps and the datasheet documents
    mov ah, CMD_SET_RETRY       ; 12 Mbps only, so the proof of concept's
    mov al, 0x17                ; switch goes first - and a full-speed mouse,
    mov bl, 0xD8                ; which that switch breaks, gets the next try
    call um_c2
.full:
    mov ah, CMD_GET_DESCR       ; --- device descriptor, at address 0 ---
    mov al, 1
    call um_cw
    call um_wait
    jc .out
    cmp al, INT_SUCCESS
    jne .fail
    call um_rdata               ; read and dropped: the chip wants it taken

    mov ah, CMD_SET_ADDRESS     ; --- address ---
    mov al, UM_ADDR
    call um_cw
    call um_wait
    jc .out
    cmp al, INT_SUCCESS
    jne .fail
    mov ah, CMD_SET_USB_ADDR
    mov al, UM_ADDR
    call um_cw

    mov ah, CMD_GET_DESCR       ; --- configuration, at most UM_BUFSZ ---
    mov al, 2
    call um_cw
    call um_wait
    jc .out
    cmp al, INT_SUCCESS
    jne .fail
    call um_rdata
    call um_parse
    jc .other

    mov ah, CMD_SET_CONFIG      ; --- configure ---
    mov al, [um_cfgv]
    call um_cw
    call um_wait
    jc .out
    cmp al, INT_SUCCESS
    jne .fail

    mov al, [um_if]             ; --- the two class requests, each allowed
    mov [um_sproto+4], al       ; to STALL: a boot-only mouse may refuse
    mov [um_sidle+4], al        ; SET_PROTOCOL and is still a boot mouse
    mov si, um_sproto
    call um_ctl
    jc .out
    mov si, um_sidle
    call um_ctl
    jc .out

    mov bl, 0x85                ; --- how a NAK is answered from here on ---
    cmp byte [um_intok], UI_POLL    ; INT# readable: the chip retries a NAK
    jne .retry                      ; forever and completes when the mouse
    mov bl, 0x05                    ; speaks. Poll mode: a NAK comes back at
.retry:                             ; once as INT_NAK. Both keep 5 timeout
    mov ah, CMD_SET_RETRY           ; retries
    mov al, 0x25
    call um_c2

    mov byte [um_tog], 0x80     ; DATA0 after SET_CONFIGURATION
    mov byte [um_pend], 0
    mov byte [um_err], 0
    mov byte [um_nenum], 0
    mov word [um_rx], 0
    mov word [um_ry], 0
    mov byte [um_state], US_RUN
.out:
    ret
.other:
    mov byte [um_state], US_OTHER
    ret
.fail:
    inc byte [um_nenum]         ; ...and the other speed next time
    cmp byte [um_nenum], UM_ENUMMAX
    jb .again
    mov byte [um_state], US_OTHER   ; enough: leave it alone until unplugged
    ret
.again:
    mov byte [um_state], US_IDLE    ; TEST_CONNECT sends us straight back, a
    ret                             ; UM_IDLET later

; -----------------------------------------------------------------------------
; um_run - US_RUN: one pass of issue-wait-read (SPEC.md 9.12.3)
; -----------------------------------------------------------------------------
um_run:
    cmp byte [um_pend], 0
    jne .wait
    mov ah, CMD_SET_ENDP6       ; --- issue: the toggle, then the IN token ---
    mov al, [um_tog]
    call um_cw
    mov al, [um_ep]
    mov cl, 4
    shl al, cl
    or al, PID_IN
    mov ah, CMD_ISSUE_TOKEN
    call um_cw
    mov byte [um_pend], 1
    call OSAPI_GET_TICKS
    mov [um_t0], ax

.wait:
    cmp byte [um_intok], UI_POLL
    je .poll
    call ch_st
    test al, 0x80
    jz .ready                   ; INT# low: the transaction finished
    jmp short .rest
.poll:
    call OSAPI_GET_TICKS
    sub ax, [um_t0]
    cmp ax, UM_WPOLL
    jae .ready
.rest:
    call OSAPI_GET_TICKS        ; nothing yet. HOT: yield, and be back as soon
    sub ax, [um_lrep]           ; as everybody else has had a turn. COLD: a
    cmp ax, UM_HOTT             ; whole tick
    jae .cold
    call OSAPI_TASK_YIELD
    ret
.cold:
    mov ax, 1
    call OSAPI_TASK_SLEEP
    ret

.ready:
    mov byte [um_pend], 0
    mov ah, CMD_GET_STATUS
    call um_cr
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
    mov ah, al                  ; a failure carrying a DATA PID is a toggle the
    and ah, 0x0F                ; device and we disagree about: take its word
    cmp ah, 0x03                ; (DATA0)
    je .flip
    cmp ah, 0x0B                ; (DATA1)
    je .flip
    inc byte [um_err]
    cmp byte [um_err], UM_ERRMAX
    jb .out
.reenum:
    call um_release
    mov byte [um_state], US_ENUM
.out:
    ret
.gone:
    call um_release
    mov byte [um_state], US_IDLE
    ret
.flip:
    xor byte [um_tog], 0x40
    ret
.stall:
    mov ah, CMD_CLR_STALL
    mov al, [um_ep]
    or al, 0x80
    call um_cw
    call um_wait
    mov byte [um_tog], 0x80     ; a cleared halt restarts at DATA0
    ret

.rep:
    xor byte [um_tog], 0x40
    mov byte [um_err], 0
    call um_rdata
    cmp byte [um_blen], 3       ; boot layout: buttons, dx, dy (and maybe more)
    jb .out
    call OSAPI_GET_TICKS
    mov [um_lrep], ax
    ; FALL THROUGH into um_report

; -----------------------------------------------------------------------------
; um_report - [um_buf] as a boot report -> OSAPI_MOUSE_FEED (SPEC.md 9.12.3)
;
; HALVED WITH THE REMAINDER CARRIED: total = delta + carry, out = total sar 1,
; carry = total & 1 (two's complement makes that the remainder for either
; sign). A slow creep of 1s alternates 0 and 1, where a plain shift would
; drop it entirely.
; -----------------------------------------------------------------------------
um_report:
    mov al, [um_buf+1]
    cbw
    add ax, [um_rx]
    mov dx, ax
    and dx, 1
    mov [um_rx], dx
    sar ax, 1
    push ax                     ; dx, scaled
    mov al, [um_buf+2]
    cbw
    add ax, [um_ry]
    mov dx, ax
    and dx, 1
    mov [um_ry], dx
    sar ax, 1
    mov bx, ax                  ; dy, scaled - the HID convention is the
    pop ax                      ; screen's: positive is down
    mov cl, [um_buf]
    and cl, 0x03                ; left, right - mouse_btn's own two bits
    cmp cl, [um_btn]
    jne .feed
    or ax, ax
    jnz .feed
    or bx, bx
    jz .none                    ; moves nothing, changes nothing: not fed
.feed:
    mov [um_btn], cl
    call OSAPI_MOUSE_FEED
.none:
    ret

; -----------------------------------------------------------------------------
; um_release - feed a release if a button is down
;
; A button held across an unplug, a re-enumeration or a detach would otherwise
; be a drag that never ends: the kernel only learns of a release from a report.
; -----------------------------------------------------------------------------
um_release:
    cmp byte [um_btn], 0
    je .out
    xor ax, ax
    xor bx, bx
    xor cl, cl
    mov [um_btn], cl
    call OSAPI_MOUSE_FEED
.out:
    ret

; -----------------------------------------------------------------------------
; um_reset - the chip back as it powered up: device mode, bus released
;
; ABORT_NAK first, because RESET_ALL is a command like any other and a chip
; retrying a NAK is not listening for one.
; -----------------------------------------------------------------------------
um_reset:
    mov ah, CMD_ABORT_NAK
    call um_c0
    mov ah, CMD_RESET_ALL
    jmp um_c0

; =============================================================================
; TRANSACTIONS
; =============================================================================

; -----------------------------------------------------------------------------
; um_ctl - one class request with no data stage: SETUP, then the status IN
; in:  SI -> the 8-byte setup packet, in our segment
; out: CF = 1 the stop byte went up while waiting; else AL = the status stage's
;      status (a STALL is an ordinary answer)
; -----------------------------------------------------------------------------
um_ctl:
    mov ah, CMD_SET_ENDP7
    mov al, 0x80                ; SETUP is always DATA0
    call um_cw
    pushf
    cli
    mov al, CMD_WR_USB_DATA7
    call ch_cmd
    mov cx, 8
    mov al, cl
    call ch_wr
.wr:
    mov al, [si]
    inc si
    call ch_wr
    loop .wr
    popf
    mov ah, CMD_ISSUE_TOKEN
    mov al, PID_SETUP           ; endpoint 0
    call um_cw
    call um_wait
    jc .out
    cmp al, INT_SUCCESS
    jne .out
    mov ah, CMD_SET_ENDP6
    mov al, 0xC0                ; the status stage is DATA1
    call um_cw
    mov ah, CMD_ISSUE_TOKEN
    mov al, PID_IN              ; endpoint 0
    call um_cw
    call um_wait
.out:
    ret

; -----------------------------------------------------------------------------
; um_wait - wait for a transaction to finish, and take its status
; out: CF = 0 and AL = the interrupt status; CF = 1 the stop byte went up
;
; The three [um_intok] states are three ways of knowing a transaction is over:
;   UI_GOOD   INT# low, with UM_WGOOD ticks before an ABORT_NAK
;   UI_PROBE  INT# low within UM_WPROBE ticks proves the bit (-> UI_GOOD); a
;             GET_STATUS that answers SUCCESS after the bit never moved proves
;             it DEAD (-> UI_POLL), because the chip finished and did not say
;   UI_POLL   UM_WPOLL ticks, then GET_STATUS - the proof of concept's shape
; -----------------------------------------------------------------------------
um_wait:
    call OSAPI_GET_TICKS
    mov [um_t0], ax
.w:
    cmp byte [um_stop], 0
    jne .stop
    cmp byte [um_intok], UI_POLL
    je .late
    call ch_st
    test al, 0x80
    jz .ready
.late:
    call OSAPI_GET_TICKS
    sub ax, [um_t0]
    mov bx, UM_WGOOD
    cmp byte [um_intok], UI_GOOD
    je .lim
    mov bl, UM_WPROBE
    cmp byte [um_intok], UI_PROBE
    je .lim
    mov bl, UM_WPOLL
.lim:
    cmp ax, bx
    jae .timeout
    cmp byte [um_intok], UI_POLL
    je .nap
    call OSAPI_TASK_YIELD       ; a transaction is milliseconds: stay close
    jmp short .w
.nap:
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .w
.ready:
    cmp byte [um_intok], UI_PROBE
    jne .status
    mov byte [um_intok], UI_GOOD    ; the bit moved: it is real
.status:
    mov ah, CMD_GET_STATUS
    call um_cr
    clc
    ret
.timeout:
    mov ah, CMD_GET_STATUS
    call um_cr
    cmp byte [um_intok], UI_POLL
    je .done                    ; poll mode: this IS the answer
    cmp al, INT_SUCCESS
    jne .stuck
    cmp byte [um_intok], UI_PROBE
    jne .done
    mov byte [um_intok], UI_POLL    ; finished, and INT# never said so
.done:
    clc
    ret
.stuck:
    mov ah, CMD_ABORT_NAK       ; a device NAKing forever: stop the chip
    call um_c0                  ; retrying, and report a timeout
    mov al, INT_TIMEOUT
    clc
    ret
.stop:
    stc
    ret

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
    mov cx, 0xFFFF
.w:
    call ch_st
    cmp byte [um_intok], UI_POLL
    je .next
    test al, 0x80
    jz .ready
.next:
    loop .w
    cmp byte [um_intok], UI_POLL
    je .ready
    mov ah, CMD_ABORT_NAK
    call um_c0
    mov ah, CMD_GET_STATUS
    call um_cr
    mov al, INT_TIMEOUT
    ret
.ready:
    mov ah, CMD_GET_STATUS
    jmp um_cr

; -----------------------------------------------------------------------------
; um_drain - consume an interrupt nobody asked for (a plug, an unplug)
; -----------------------------------------------------------------------------
um_drain:
    cmp byte [um_intok], UI_POLL
    je .take                    ; blind: GET_STATUS with nothing pending is
    call ch_st                  ; harmless
    test al, 0x80
    jnz .out
.take:
    mov ah, CMD_GET_STATUS
    call um_cr
.out:
    ret

; -----------------------------------------------------------------------------
; um_rdata - RD_USB_DATA into [um_buf], at most UM_BUFSZ kept; [um_blen] = kept
; -----------------------------------------------------------------------------
um_rdata:
    pushf
    cli
    mov al, CMD_RD_USB_DATA
    call ch_cmd
    call ch_rd
    xor ah, ah
    mov cx, ax                  ; what the chip will send, all of which must
    cmp al, UM_BUFSZ            ; be read whatever we keep
    jbe .fits
    mov al, UM_BUFSZ
.fits:
    mov [um_blen], al
    mov di, um_buf
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
    popf
    ret

; -----------------------------------------------------------------------------
; um_parse - find a boot mouse in the configuration descriptor in [um_buf]
; out: CF = 0 and [um_cfgv], [um_if], [um_ep] set; CF = 1 no boot mouse here
;
; A descriptor CHAIN, walked by bLength, and not the proof of concept's fixed
; offsets: a HID descriptor sits between the interface and its endpoint, and a
; composite device puts other interfaces first. The mouse is interface class
; 3 protocol 2; its endpoint is the first interrupt IN after it. Every field
; is read only once its descriptor is proven to lie inside what was kept.
; -----------------------------------------------------------------------------
um_parse:
    mov byte [um_cfgv], 1
    mov si, um_buf
    mov dx, si
    mov al, [um_blen]
    xor ah, ah
    add dx, ax                  ; DX = one past the last byte kept
    xor bl, bl                  ; BL = 1 inside a mouse's interface
.next:
    mov al, [si]                ; bLength
    cmp al, 2
    jb .none
    xor ah, ah
    add ax, si
    cmp ax, dx
    ja .none                    ; runs past what was kept
    mov ah, [si+1]              ; bDescriptorType
    cmp ah, 2
    jne .notcfg
    mov al, [si+5]              ; bConfigurationValue
    mov [um_cfgv], al
    jmp short .adv
.notcfg:
    cmp ah, 4
    jne .notif
    xor bl, bl
    cmp byte [si+5], 3          ; bInterfaceClass HID
    jne .adv
    cmp byte [si+7], 2          ; bInterfaceProtocol mouse
    jne .adv
    mov al, [si+2]
    mov [um_if], al
    mov bl, 1
    jmp short .adv
.notif:
    cmp ah, 5
    jne .adv
    or bl, bl
    jz .adv
    mov al, [si+2]              ; bEndpointAddress
    test al, 0x80
    jz .adv
    mov ah, [si+3]
    and ah, 3
    cmp ah, 3                   ; interrupt
    jne .adv
    and al, 0x0F
    mov [um_ep], al
    clc
    ret
.adv:
    mov al, [si]
    xor ah, ah
    add si, ax
    cmp si, dx
    jb .next
.none:
    stc
    ret

; =============================================================================
; COMMANDS - each one IF=0 window, for the datasheet's TSC/TSD
; =============================================================================

; um_c0 - AH = a command with no data and no answer
um_c0:
    pushf
    cli
    mov al, ah
    call ch_cmd
    popf
    ret

; um_cw - AH = a command, AL = its one data byte
um_cw:
    pushf
    cli
    push ax
    mov al, ah
    call ch_cmd
    pop ax
    call ch_wr
    popf
    ret

; um_c2 - AH = a command, AL then BL = its two data bytes
um_c2:
    call um_cw
    pushf
    cli
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

; um_cwr - AH = a command, AL = its data byte; out AL = its one answer
um_cwr:
    pushf
    cli
    push ax
    mov al, ah
    call ch_cmd
    pop ax
    call ch_wr
    call ch_rd
    popf
    ret

; um_mode - SET_USB_MODE, AL = the mode. The answer comes up to 20 us after
; the data byte (TE2), which a fast machine can beat, so the read waits a
; moment - OUTSIDE the IF=0 window, which bounds only command-to-data.
um_mode:
    call um_cw
    mov cx, 64
.d:
    loop .d
    jmp ch_rd                   ; CMD_RET_SUCCESS (51h); nothing to do with it

; =============================================================================
; THE FOUR PORT PRIMITIVES. Everything above reaches the chip through these
; and nothing else, which is what lets the gate build put a model under them.
; Each preserves every register but AL.
; =============================================================================
%ifdef CH375SIM
%include "ch375sim.inc"
%else
ch_cmd:
    push dx
    mov dx, CH_PCMD
    out dx, al
    pop dx
    ret
ch_wr:
    push dx
    mov dx, CH_PDAT
    out dx, al
    pop dx
    ret
ch_rd:
    push dx
    mov dx, CH_PDAT
    in al, dx
    pop dx
    ret
ch_st:
    push dx
    mov dx, CH_PCMD
    in al, dx
    pop dx
    ret
%endif

; =============================================================================
; State. A driver's zeroed data is stripped from the file and zeroed at load
; (drivers/os88drv.inc), so the buffer goes last.
; =============================================================================
um_sproto:  db 0x21, 0x0B, 0, 0, 0, 0, 0, 0 ; SET_PROTOCOL(boot), interface +4
um_sidle:   db 0x21, 0x0A, 0, 0, 0, 0, 0, 0 ; SET_IDLE(0), interface +4

um_stop     db 0                ; detach raised it: the worker exits
um_alive    db 0                ; a worker exists (set at spawn)
um_state    db US_IDLE
um_intok    db UI_PROBE
um_nenum    db 0                ; failed enumerations of this device
um_pend     db 0                ; an IN token is out
um_err      db 0                ; failed reads in a row
um_tog      db 0x80             ; SET_ENDP6's byte: 80h DATA0, C0h DATA1
um_ep       db 1
um_if       db 0
um_cfgv     db 1
um_btn      db 0                ; the buttons the kernel was last told
um_blen     db 0
um_t0       dw 0
um_lrep     dw 0                ; the tick of the last report
um_rx       dw 0                ; the halving's carried remainders
um_ry       dw 0
um_buf:     times UM_BUFSZ db 0

    OS88_DRV_END
