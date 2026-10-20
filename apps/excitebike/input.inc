; The keyboard (SPEC.md 102.3, plan 10): one OSAPI_KEY_DOWN level read per key per
; frame - six far calls at 46.7 us each, ~1.7k clocks a frame - into the input byte
; the steps of that frame all use.  A frame is 18 ms, a key tap shorter than that is
; caught only if it spans a poll: OSAPI_KEY_DOWN is advice, not an oracle
; (SPEC.md 67.11.1).  The arrows steer (Up/Down the lane, Left lifts the nose, Right
; drops it), Z is the throttle and X the turbo.
;
; xb_input - [xb_inp] = the input bits, unless the harness is feeding a script
xb_input:
    cmp byte [xb_tmode], 0
    jne .out
    push ax
    push bx
    xor bl, bl
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jnc .d
    or bl, INP_U
.d:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jnc .l
    or bl, INP_D
.l:
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .r
    or bl, INP_L
.r:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .a
    or bl, INP_R
.a:
    mov al, 2ch                     ; Z
    call OSAPI_KEY_DOWN
    jnc .b
    or bl, INP_A
.b:
    mov al, 2dh                     ; X
    call OSAPI_KEY_DOWN
    jnc .st
    or bl, INP_B
.st:
    mov [xb_inp], bl
    pop bx
    pop ax
.out:
    ret
