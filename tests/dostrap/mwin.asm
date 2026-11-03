; =============================================================================
; os8088 - tests/dostrap/mwin.asm - MWIN.COM, INT 33h in a window's probe
; (SPEC.md 96.54.4, docs/plans/DOS-WINDOWED-PLAN.md wave 5)
;
; A mouse program and nothing else: it resets the driver, SHOWS the text
; cursor (which a window must not draw), installs an event handler for motion
; and the left press, and then polls INT 33h 03h and 05h as fast as it can
; until a key arrives. Everything the row needs is a word at PSP:0103h, read
; off the program rather than off a band that scrolls:
;
;   0103 polls      0105 03h's BX   0107 03h's CX   0109 03h's DX
;   010B 05h's left presses, summed  010D/010F where the last one was
;   0111 handler calls  0113 its CX  0115 its DX  0117 its BX
;
; It runs under a real DOS with a mouse driver unchanged.
; =============================================================================
cpu 8086
org 0x100

    jmp short start
    nop
polls:  dw 0                        ; 0103
mbtn:   dw 0                        ; 0105
mx:     dw 0                        ; 0107
my:     dw 0                        ; 0109
npress: dw 0                        ; 010B
px:     dw 0                        ; 010D
py:     dw 0                        ; 010F
nev:    dw 0                        ; 0111
ex:     dw 0                        ; 0113
ey:     dw 0                        ; 0115
ebtn:   dw 0                        ; 0117
start:
    mov dx, s_hello
    mov ah, 0x09
    int 0x21
    xor ax, ax                      ; reset: the driver's power-up state
    int 0x33
    mov ax, 1                       ; show the cursor
    int 0x33
    push cs
    pop es
    mov dx, handler
    mov cx, 3                       ; motion and the left press
    mov ax, 0x0C
    int 0x33
.loop:
    mov ax, 3
    int 0x33
    mov [mbtn], bx
    mov [mx], cx
    mov [my], dx
    mov ax, 5
    xor bx, bx
    int 0x33
    or bx, bx
    jz .nopress
    add [npress], bx
    mov [px], cx
    mov [py], dx
.nopress:
    inc word [polls]
    mov ah, 1
    int 0x16
    jz .loop
    xor ah, ah
    int 0x16
    xor ax, ax                      ; reset: the handler comes out with it
    int 0x33
    mov dx, s_bye
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C00
    int 0x21

handler:                            ; far: AX = events, BX/CX/DX = the state
    inc word [cs:nev]
    mov [cs:ex], cx
    mov [cs:ey], dx
    mov [cs:ebtn], bx
    retf

s_hello: db 'MWIN READY - press a key to end', 13, 10, '$'
s_bye:   db 'MWIN DONE', 13, 10, '$'
