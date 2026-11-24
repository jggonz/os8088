; =============================================================================
; os8088 - tests/joygate/joygate.asm
;
; JOYGATE: the gate for JOYSTICK.DRV's package door (SPEC.md 137.4). A click
; on its window runs ONE probe of the game port as a PACKAGE sees it, through
; apps/os88joy.inc, and leaves every answer in `jg_res` for the harness to
; read out of the image (a screenshot would prove the numbers were drawn, not
; which bytes the driver returned):
;
;   joy_find, joy_read A twice (the second read of a held button must answer
;   no new press), joy_read B, joy_state, JOYV_RAW, and - when asked -
;   JOYV_PROBE first and OSAPI_DRV_USE (DRVU_UP) before everything.
;   The first joy_read and the joy_state are timed with pit_now (838ns).
;
; The harness chooses the probe by writing `jg_cmd` before it clicks:
;   bit 0  attach the driver first (OSAPI_DRV_USE, DRVC_JOY, DRVU_UP)
;   bit 1  JOYV_PROBE before the reads
; and waits for `jg_seq` to change. NEVER SHIPPED: its own scratch image,
;   make joygatetest && python3 tests/joystick.py
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'JOYGATE', jg_entry

JG_W        equ 220
JG_H        equ 40

jg_entry:
    push si
    mov si, jg_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; ---- store the registers a verb answered in, then its CF as a word ----------
%macro STORE_CF 1
    lahf
    and ah, 1
    mov [%1], ah
    mov byte [%1+1], 0
%endmacro

jg_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es

    ; --- bit 0: bring the driver up -----------------------------------------
    test byte [jg_cmd], 1
    jz .noatt
    mov al, DRVC_JOY
    mov ah, DRVU_UP
    call OSAPI_DRV_USE
    mov [jg_use_ax], ax
    STORE_CF jg_use_cf
.noatt:
    ; --- bit 1: look for sticks afresh ---------------------------------------
    test byte [jg_cmd], 2
    jz .noprobe
    mov bx, (DRVC_JOY << 8) | JOYV_PROBE
    call OSAPI_DRV_CALL
    mov [jg_pr_ax], ax
    STORE_CF jg_pr_cf
.noprobe:
    ; --- INFO ----------------------------------------------------------------
    call joy_find
    mov [jg_f_ax], ax
    mov [jg_f_cx], cx
    mov [jg_f_dx], dx
    STORE_CF jg_f_cf

    ; --- READ A, timed -------------------------------------------------------
    call pit_now
    mov [jg_t0], ax
    xor al, al
    call joy_read
    mov [jg_a1_ax], ax
    mov [jg_a1_bx], bx
    mov [jg_a1_cx], cx
    STORE_CF jg_a1_cf
    call pit_now
    sub ax, [jg_t0]
    mov [jg_a1_t], ax
    ; --- READ A again: a held button is not a second press -------------------
    xor al, al
    call joy_read
    mov [jg_a2_ax], ax
    mov [jg_a2_bx], bx
    mov [jg_a2_cx], cx
    STORE_CF jg_a2_cf
    ; --- READ B --------------------------------------------------------------
    mov al, 1
    call joy_read
    mov [jg_b_ax], ax
    mov [jg_b_bx], bx
    mov [jg_b_cx], cx
    STORE_CF jg_b_cf
    ; --- RAW ----------------------------------------------------------------
    mov bx, (DRVC_JOY << 8) | JOYV_RAW
    call OSAPI_DRV_CALL
    mov [jg_r_x1], ax
    mov [jg_r_y1], bx
    mov [jg_r_x2], cx
    mov [jg_r_y2], dx
    mov [jg_r_bt], si
    STORE_CF jg_r_cf
    ; --- STATE, timed ---------------------------------------------------------
    call pit_now
    mov [jg_t0], ax
    mov di, jg_st
    call joy_state
    STORE_CF jg_s_cf
    call pit_now
    sub ax, [jg_t0]
    mov [jg_s_t], ax

    inc word [jg_seq]               ; LAST: the harness waits on it
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

jg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, 8
    add dx, 10
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, jg_s_ttl
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

jg_onclick:
    call jg_probe
    ret

jg_onkey:
    ret

jg_tpl:
    dw 200, 200, JG_W, JG_H
    dw jg_ttl, jg_cls
    OS88_CLASS jg_cls, 0, PAINT, jg_paint, ONKEY, jg_onkey, \
               ONCLICK, jg_onclick

jg_ttl:     db 'Joy Gate', 0
jg_s_ttl:   db 'Click: probe the game port', 0

%include "os88joy.inc"
%include "os88pit.inc"

; ---- the result block: a magic the harness finds, then FIXED-OFFSET words ----
; (every field a word, in this order - tests/joystick.py's JG table)
jg_magic:   db 'JGATE:'
jg_cmd:     dw 0                    ; +0   the harness writes this
jg_seq:     dw 0                    ; +2
jg_use_cf:  dw 0                    ; +4
jg_use_ax:  dw 0                    ; +6
jg_pr_cf:   dw 0                    ; +8
jg_pr_ax:   dw 0                    ; +10
jg_f_cf:    dw 0                    ; +12
jg_f_ax:    dw 0                    ; +14
jg_f_cx:    dw 0                    ; +16
jg_f_dx:    dw 0                    ; +18
jg_a1_cf:   dw 0                    ; +20
jg_a1_ax:   dw 0                    ; +22
jg_a1_bx:   dw 0                    ; +24
jg_a1_cx:   dw 0                    ; +26
jg_a1_t:    dw 0                    ; +28
jg_a2_cf:   dw 0                    ; +30
jg_a2_ax:   dw 0                    ; +32
jg_a2_bx:   dw 0                    ; +34
jg_a2_cx:   dw 0                    ; +36
jg_b_cf:    dw 0                    ; +38
jg_b_ax:    dw 0                    ; +40
jg_b_bx:    dw 0                    ; +42
jg_b_cx:    dw 0                    ; +44
jg_r_cf:    dw 0                    ; +46
jg_r_x1:    dw 0                    ; +48
jg_r_y1:    dw 0                    ; +50
jg_r_x2:    dw 0                    ; +52
jg_r_y2:    dw 0                    ; +54
jg_r_bt:    dw 0                    ; +56
jg_s_cf:    dw 0                    ; +58
jg_s_t:     dw 0                    ; +60
jg_t0:      dw 0                    ; +62
jg_st:      times JOYS_SIZE db 0    ; +64  the JOYV_STATE record

    OS88_IMAGE_END
