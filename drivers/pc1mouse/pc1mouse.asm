; =============================================================================
; os8088 - PC1MOUSE.DRV
;
; Olivetti Prodest PC1's built-in Yamaha V6335D mouse, based on the DOS
; reference `pc1mouse.asm` supplied with the feature request. Unlike that TSR,
; this driver leaves IRQ0/IRQ1/INT 33h and the PC1 hardware cursor alone. It
; polls the V6335 movement counters from a driver worker, observes the PC1's
; button scan codes through OSAPI_KEY_DOWN, and reports through the kernel's
; ordinary OSAPI_MOUSE_FEED path.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'PC1 Mouse', DRVC_POINT, p1_entry

P1_KBCMD    equ 0x64
P1_KBDATA   equ 0x60
P1_REG10    equ 0x10
P1_REG11    equ 0x11
P1_LEFT     equ 0x77
P1_RIGHT    equ 0x79

; The DOS driver's default speed rule (Max_speed_D2 = 2): double either
; signed axis when its magnitude exceeds two mickeys in one sample.
%macro P1_ACCEL 1
    mov dx, %1
    test dx, 0x8000
    jz %%magnitude
    neg dx
%%magnitude:
    cmp dx, 2
    jbe %%done
    shl %1, 1
%%done:
%endmacro

%if DRVV_ATTACH != 0 || DRVV_DETACH != 1 || DRVV_READY != 3
    %error "pc1mouse.asm assumes the published DRVV_* verb values"
%endif

p1_entry:
    cmp al, DRVV_DETACH
    je p1_detach
    cmp al, DRVV_READY
    je p1_ready
    cmp al, DRVV_ATTACH
    jne .ok
    call p1_probe
    jc .refuse
    call p1_keyinit
    jc .refuse
    mov al, P1_LEFT            ; arm the kernel's make/break map before PC1
    call OSAPI_KEY_DOWN        ; button scans can arrive (SPEC.md 9.7)
    mov byte [p1_stop], 0
    mov byte [p1_alive], 0
.ok:
    clc
    ret
.refuse:
    stc
    ret

p1_ready:
    mov ax, p1_worker
    xor dx, dx
    call OSAPI_DRV_TASK
    jc .ready_fail
    inc byte [p1_alive]
    clc
    ret
.ready_fail:
    mov al, DRVE_MEM
    stc
    ret

p1_detach:
    inc byte [p1_stop]
    cmp byte [p1_alive], 0
    jne .done
    ; No worker was created (READY refusal); still end with released buttons.
    xor ax, ax
    xor bx, bx
    xor cx, cx
    call OSAPI_MOUSE_FEED
.done:
    clc
    ret

; A positive BIOS identity is required before any PC1-specific keyboard or
; counter access. The DOS reference recognizes the final ROM word FE44h
; (floppy), FE49h (unknown disk configuration) and FE4Ah (hard disk).
p1_probe:
    push ds
    push bx
    mov ax, 0xF000
    mov ds, ax
    mov bx, 0xFFFD
    mov ax, [bx]
    cmp ax, 0xFE44
    je .yes
    cmp ax, 0xFE49
    je .yes
    cmp ax, 0xFE4A
    je .yes
    stc
    jmp short .out
.yes:
    clc
.out:
    pop bx
    pop ds
    ret

; Configure the PC1 keyboard interface to publish left and right button state
; as the make/break codes the reference driver documents. Bound every 8042 wait:
; a failed controller must refuse attach rather than hang boot. If the PC1
; reports that these codes are already enabled, leave its state alone.
p1_keyinit:
    push bx
    push cx
    ; Keep the keyboard ISR from consuming the controller's reply byte. The
    ; BIOS tick stays enabled; only IRQ1 is masked for this short transaction.
    pushf
    cli
    in al, 0x21
    mov bl, al
    and bl, 2                  ; remember only the bit we temporarily change
    or al, 2
    out 0x21, al
    mov [p1_imr], bl
    popf
    mov dx, P1_KBCMD
    mov al, 0x12
    call p1_kbout
    jc .bad
    call p1_kbin
    jc .bad
    test al, 1
    jnz .good
    mov bx, p1_keyseq
    mov cx, 5
.next:
    mov al, [bx]
    inc bx
    mov dx, P1_KBDATA
    call p1_kbout
    jc .bad
    loop .next
.good:
    pushf
    cli
    in al, 0x21                ; preserve other IRQ mask changes made while
    and al, 0xFD                ; the bounded controller wait ran
    or al, [p1_imr]
    out 0x21, al
    popf
    pop cx
    pop bx
    clc
    ret
.bad:
    pushf
    cli
    in al, 0x21
    and al, 0xFD
    or al, [p1_imr]
    out 0x21, al
    popf
    pop cx
    pop bx
    stc
    ret

; p1_kbout - bounded wait for input buffer empty, then write AL to DX.
p1_kbout:
    push ax
    push cx
    mov cx, 0xFFFF
.wait:
    in al, P1_KBCMD
    test al, 2
    jz .write
    loop .wait
    pop cx
    pop ax
    stc
    ret
.write:
    pop cx
    pop ax
    out dx, al
    clc
    ret

; p1_kbin - bounded wait for output buffer full, then consume port 60h.
p1_kbin:
    push cx
    mov cx, 0xFFFF
.wait:
    in al, P1_KBCMD
    test al, 1
    jnz .read
    loop .wait
    pop cx
    stc
    ret
.read:
    in al, P1_KBDATA
    pop cx
    clc
    ret

p1_worker:
.loop:
    call OSAPI_TASK_PARK
    cmp byte [p1_stop], 0
    jne .exit
    call p1_sample
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .loop
.exit:
    cmp byte [p1_buttons], 0
    je .no_release
    xor ax, ax
    xor bx, bx
    xor cx, cx
    call OSAPI_MOUSE_FEED
    mov byte [p1_buttons], 0
.no_release:
    xor ax, ax
    call OSAPI_DRV_TASK         ; worker exit; never returns

; Read/clear the two signed 8-bit hardware counters, and combine them with the
; PC1's button make/break states. On the PC1 field run the DOS reference's
; apparent X/Y assignment was transposed: register 11h drives horizontal motion,
; while register 10h drives vertical motion and is reversed. Keep both at one
; count per pixel until the hardware speed is measured on the PC1.
p1_sample:
    mov dx, 0xD4
    mov al, P1_REG10
    out dx, al
    inc dx
    in al, dx
    cbw
    neg ax
                                ; this counter is vertical on the field unit
    push ax

    mov dx, 0xD4
    mov al, P1_REG11
    out dx, al
    inc dx
    in al, dx
    cbw
    mov bx, ax                  ; this counter is horizontal on the field unit
    pop ax                      ; negated vertical delta
%ifndef PC1_AXES_DOS
    xchg ax, bx                  ; OSAPI_MOUSE_FEED wants X in AX, Y in BX
%endif                          ; -DPC1_AXES_DOS is the controlled reference-map
                                ; A/B: register 10h -> X, register 11h -> Y
%ifndef PC1_SCALE8
    P1_ACCEL ax
    P1_ACCEL bx
%endif
%ifdef PC1_SCALE8
    ; Diagnostic sensitivity arm: one OS pixel per eight PC1 counts, with a
    ; signed remainder on each axis so slow movement is not lost.
    add ax, [p1_remx]
    cwd
    mov si, 8
    idiv si
    mov [p1_remx], dx
    push ax
    mov ax, bx
    add ax, [p1_remy]
    cwd
    mov si, 8
    idiv si
    mov [p1_remy], dx
    mov bx, ax
    pop ax
%endif

    push ax                     ; the key-query scan codes use AL; preserve dx
    xor cx, cx
    mov al, P1_LEFT
    call OSAPI_KEY_DOWN
    jnc .right
    or cl, 1
.right:
    mov al, P1_RIGHT
    call OSAPI_KEY_DOWN
    jnc .compare
    or cl, 2
.compare:
    pop ax                      ; restore the sampled/scaled horizontal delta
    cmp cl, [p1_buttons]
    jne .feed
    mov dx, ax
    or dx, bx
    jz .out
.feed:
    mov [p1_buttons], cl
    call OSAPI_MOUSE_FEED
.out:
    ret

p1_keyseq: db 0x12, 0x77, 0x78, 0x79, 0x01
p1_stop:   db 0
p1_alive:  db 0
p1_buttons: db 0
p1_imr:    db 0
%ifdef PC1_SCALE8
p1_remx:   dw 0
p1_remy:   dw 0
%endif

    OS88_DRV_END
