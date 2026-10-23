; =============================================================================
; os8088 - apps/xtmp3/x3hook.inc - THE STUB BACKEND'S TEST HOOK (engine track)
;
; Assembled into an X3STUB=1 build only (`make xtmp3stub`, never shipped),
; where the stub backend (x3stub.asm) is the parts table's row 4 and answers
; for both backends. X3STUB.CFG, read from the launch folder at x3_e_init,
; says how long each rung's granule takes (docs/xtmp3/ENGINE.md section 7).
; The front end is the real one, x3pars.inc.
; =============================================================================

%include "x3stub.inc"

; -----------------------------------------------------------------------------
; THE TEST HOOK (X3STUB builds only): X3STUB.CFG, read from the folder the
; package was launched from at x3_e_init, is copied into the stub part's data
; each time it is fetched (x3_stubpoke): a speed per rung (x100, 0 = none: as
; fast as the machine), and a mid-track slowdown - from granule N on, rung R
; runs at speed S (docs/xtmp3/ENGINE.md, tests/xtmp3eng.py)
; -----------------------------------------------------------------------------
x3_stubcfg:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov si, x3h_cfgn
    mov bx, x3h_cfg
    mov cx, X3SB_CFGSZ
    xor dx, dx
    call OSAPI_FILE_READ
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_stubpoke - the cfg into the part (ES = its segment), and the rung the
; engine is opening it for (one stub part serves both backends)
x3_stubpoke:
    push cx
    push si
    push di
    mov si, x3h_cfg
    mov di, X3SB_CFG
    mov cx, X3SB_CFGSZ
    cld
    rep movsb
    mov al, [x3_rung]
    mov [es:X3SB_RUNG], al
    pop di
    pop si
    pop cx
    ret

x3h_cfgn:  db 'X3STUB.CFG', 0

%macro X3_HOOKBSS 0
    X3B x3h_cfg, X3SB_CFGSZ         ; X3STUB.CFG, as read
%endmacro
