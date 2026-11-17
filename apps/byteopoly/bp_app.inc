; Byteopoly - the app lane's assembly bridge (SPEC.md 131.9.6): the os88ui
; alert the close decision and the discard question need, called from C
; (cdecl, near, SPEC.md 73.3). Modelled on BREADXT's _bb_ask (apps/breadxt/
; draw.inc), whose tail BX_CORE_ONLY leaves out. Lane: app.
section .text
; int bp_ask(const char *message,void *window,int buttons)
; buttons = OS88UI_A* set (1 Yes/No, 2 Save/Discard/Cancel, 3 Cancel/Proceed);
; 0 = the alert is up, -1 = refused. The answer arrives in C's bp_alertdone.
_bp_ask:
    push bp
    mov bp,sp
    push es
    mov si,[bp+4]
    mov bx,[bp+6]
    mov ax,[bp+8]
    mov di,bp_ask_done
    call os88ui_ask
    mov ax,0
    jnc .ok
    dec ax
.ok:
    pop es
    pop bp
    ret
; the alert's completion: AL = the button, OS88UI_ACANCEL = dismissed
bp_ask_done:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    xor ah,ah
    cmp al,OS88UI_ACANCEL
    jne .have
    mov ax,-1
.have:
    push ax
    call _bp_alertdone
    add sp,2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
