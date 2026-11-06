; =============================================================================
; os8088 - apps/mousechk/other.inc - VMware absolute, USB, the keyboard mouse
;                                    (SPEC.md 126.9)
;
; The pointers the kernel does not drive from a UART or the 8042, and the one
; it falls back on. Everything here is READ-ONLY and does NO port I/O: whether
; a driver is loaded comes from OSAPI_DRV_CLASSK, and whether a pointer is in
; use from the 'MO' block's [mou_port] (6 = VMware absolute, 8 = a driver's
; OSAPI_MOUSE_FEED, SPEC.md 9.4, 9.11.3, 9.12), which sum_snap banked into the
; sum_* statics (summary.inc) earlier in the same rebuild. What no slot and no
; published block answers - whether a driver is WANTED (SYSTEM.CFG's DW bits 5
; and 6, which only the FILE holds), which chip USBMOUSE.DRV attached to,
; kern_emu against another real-mode build - the rows SAY so rather than guess
; (SPEC.md 47). With [mc_kok] = 0 nothing is decoded and every "used" row
; says unknown.
;
; ES = KERNEL_SEG, DS = the package, on entry to both procs.
; =============================================================================
oth_init:
    ret

oth_rows:
    ; --- VMware absolute pointer -----------------------------------------
    mov si, oth_s_vh
    mov al, LP_NA
    call mc_head
    mov si, oth_s_used
    call mc_row
    mov al, 6
    call oth_used                   ; CF = 1: it is the pointer in use
    pushf
    mov si, oth_s_cpu
    call mc_row
    mov si, oth_s_cpuok
    cmp byte [sum_cpu], 2
    jae .vc
    mov si, oth_s_cpubad
.vc:
    call mc_put
    popf
    jc .usb
    mov si, oth_s_vl1
    call mc_line
    mov si, oth_s_vnot              ; kern_mega, or the build this package was
    mov al, [sum_pm]                ; made against (kern_big): a fact, so say it
    or al, [mc_kok]
    jnz .vn
    mov si, oth_s_nask              ; another real-mode build: nothing says
.vn:
    call mc_line
    ; --- USB mouse -------------------------------------------------------
.usb:
    mov al, DRVC_POINT
    call OSAPI_DRV_CLASSK           ; CF = 1: nothing of that class is loaded
    sbb al, al
    mov [oth_ld], al
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, oth_s_uh
    mov al, LP_NA
    call mc_head
    mov si, oth_s_drv
    call mc_row
    mov si, oth_s_loaded
    cmp byte [oth_ld], 0
    je .ul
    mov si, oth_s_noload
.ul:
    call mc_put
    cmp byte [oth_ld], 0
    jne .uu
    mov al, LP_INFO
    call mc_lamp
.uu:
    mov si, oth_s_used
    call mc_row
    mov al, 8
    call oth_used
    jc .kbm
    mov si, oth_s_ul1
    call mc_line
    mov si, oth_s_ul2
    call mc_line
    ; --- the keyboard mouse ----------------------------------------------
.kbm:
    mov si, oth_s_kh
    mov al, LP_NA
    call mc_head
    mov si, oth_s_state
    call mc_row
    mov si, oth_s_unknown
    cmp byte [sum_ok], 0
    je .kp
    mov si, oth_s_koff
    cmp byte [sum_seen], 0
    jne .kp
    mov si, oth_s_kon
.kp:
    call mc_put
    cmp byte [sum_ok], 0
    je .hint
    cmp byte [sum_seen], 0
    jne .koff
    mov al, LP_WARN
    call mc_lamp
    mov si, oth_s_k1
    call mc_line
    mov si, oth_s_k2
    call mc_line
    jmp short .hint
.koff:
    mov si, oth_s_k3
    call mc_line
    ; --- the checklist, only while nothing has worked --------------------
.hint:
    cmp byte [sum_ok], 0
    je .h
    cmp byte [sum_seen], 0
    jne .done
.h:
    mov si, oth_s_hh
    mov al, LP_INFO
    call mc_head
    mov si, oth_s_h1
    call mc_line
    mov si, oth_s_h2
    call mc_line
    mov si, oth_s_h3
    call mc_line
    mov si, oth_s_h4
    call mc_line
    mov si, oth_s_h5
    call mc_line
.done:
    ret

; -----------------------------------------------------------------------------
; oth_used - append "yes", "no" or "unknown" for whether [mou_port] = AL is the
; pointer in use; raises the section's lamp to OK on a yes
; out: CF = 1 yes
; -----------------------------------------------------------------------------
oth_used:
    mov si, oth_s_unknown
    cmp byte [sum_ok], 0
    je .no
    mov si, oth_s_no
    cmp byte [sum_seen], 0
    je .no
    cmp al, [sum_port]
    jne .no
    mov si, oth_s_yes
    call mc_put
    mov al, LP_OK
    call mc_lamp
    stc
    ret
.no:
    call mc_put
    clc
    ret

oth_s_vh:     db 'VMware absolute pointer', 0
oth_s_uh:     db 'USB mouse', 0
oth_s_kh:     db 'Keyboard mouse', 0
oth_s_hh:     db 'If your mouse is not detected', 0
oth_s_used:   db 'In use', 0
oth_s_cpu:    db 'CPU', 0
oth_s_drv:    db 'Driver', 0
oth_s_state:  db 'State', 0
oth_s_yes:    db 'yes', 0
oth_s_no:     db 'no', 0
oth_s_unknown: db 'unknown (record not decoded)', 0
oth_s_cpuok:  db 'a 386 or later: can run it', 0
oth_s_cpubad: db 'too old: it needs a 386 or later', 0
oth_s_loaded: db 'USBMOUSE.DRV is loaded', 0
oth_s_noload: db 'USBMOUSE.DRV is not loaded', 0
oth_s_koff:   db 'off: a pointer is in use', 0
oth_s_kon:    db 'ON: no pointer has been found', 0
oth_s_vl1:    db 'kern_emu only: Control Panel > Drivers > Absolute mouse.', 0
oth_s_vnot:   db 'This kernel is not kern_emu.', 0
oth_s_nask:   db 'Nothing this package can read names this kernel.', 0
oth_s_ul1:    db 'Control Panel > Drivers > USB Mouse (SYSTEM.CFG bit 6).', 0
oth_s_ul2:    db 'It needs a CH375 at 260h or a PicoMEM; neither is asked.', 0
oth_s_k1:     db 'Arrows move; keypad 5, Space or Ins click; Del right-', 0
oth_s_k2:     db 'clicks. ScrollLock on gives the keys back to a program.', 0
oth_s_k3:     db 'It retires for the session once any pointer works.', 0
oth_s_h1:     db 'PS/2: use the mouse socket, not the keyboard socket.', 0
oth_s_h2:     db 'One mini-DIN for both? It needs a Y-splitter cable.', 0
oth_s_h3:     db 'BIOS Setup: enable PS/2 mouse; IRQ12 not set to ISA.', 0
oth_s_h4:     db 'Serial mice need a COM port, PS/2 mice a PS/2 port.', 0
oth_s_h5:     db 'Then press Probe PS/2 (P) and read where it stops.', 0

oth_ld:       db 0              ; 0 = USBMOUSE.DRV loaded, FF = not
