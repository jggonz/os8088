; =============================================================================
; os8088 - tests/dostrap/dwtick.asm - DWTICK.COM, the live window's probe
; (SPEC.md 96.54, docs/plans/DOS-WINDOWED-PLAN.md wave 1)
;
; A program that NEVER YIELDS ON ITS OWN: it waits by spinning on the BIOS
; tick at 0040:006C, so the only way anything else on the machine runs while
; it does is pre-emption - which is the property under test. It prints one
; line every half second, alternately through INT 21h AH=09h and through the
; video BIOS's teletype (int 10h AH=0Eh), so both output paths reach the
; window, and counts them in the word at PSP:0103h, which the row reads
; rather than the band (a band scrolls). Then it asks for a key through INT 21h AH=08h, prints what it got and
; asks AGAIN, and exits with the second key as its exit code - which is how
; the row tells its keystrokes arrived.
;
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================
cpu 8086
org 0x100

DW_LINES equ 12

    jmp short start
    nop
count: dw 0                         ; PSP:0103h - lines printed, for the row
start:
    mov dx, s_hello
    mov ah, 0x09
    int 0x21
    mov cx, DW_LINES
    mov byte [n], '0'
.line:
    push cx
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
    add bx, 9                       ; ~half a second of 18.2 Hz ticks
.spin:
    mov ax, [es:0x6C]
    sub ax, bx
    js .spin
    inc byte [n]
    cmp byte [n], '9'
    jbe .dig
    mov byte [n], 'A'
.dig:
    test byte [n], 1
    jz .bios
    mov dx, s_tick                  ; odd lines: DOS
    mov ah, 0x09
    int 0x21
    jmp short .next
.bios:
    mov si, s_btick                 ; even lines: the ROM's teletype
.bch:
    lodsb
    or al, al
    jz .next
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .bch
.next:
    inc word [count]
    pop cx
    loop .line
    mov dx, s_key
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    mov [s_got + 11], al            ; ...say what came, and ASK AGAIN: the
    mov dx, s_got                   ; report this answers was a question that
    mov ah, 0x09                    ; reached the glass only after a drag,
    int 0x21                        ; printed by a program then waiting on
    mov ah, 0x08                    ; its next key
    int 0x21
    mov ah, 0x4C
    int 0x21

s_hello: db 'DWTICK START', 13, 10, '$'
s_tick:  db 'DWTICK DOS '
n:       db '0', 13, 10, '$'
s_btick: db 'DWTICK BIOS', 13, 10, 0
s_key:   db 'DWTICK PRESS A KEY', 13, 10, '$'
s_got:   db 'DWTICK GOT ?', 13, 10, 'DWTICK AGAIN', 13, 10, '$'
