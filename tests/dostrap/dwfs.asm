; =============================================================================
; os8088 - tests/dostrap/dwfs.asm - DWFS.COM, the live window's FULL SCREEN
; probe (SPEC.md 96.54.1, docs/plans/DOS-WINDOWED-PLAN.md wave 3)
;
; Prints `DWFS n` every half second, waiting by SPINNING on the BIOS tick, and
; counts its lines in the word at PSP:0103h so a row can tell running from
; frozen without reading any screen. Between lines it takes a key with INT 16h
; AH=01h/00h:
;   q  exit, code 'q'
;   g  set CGA mode 4 through int 10h AH=00h - the one thing a window can see
;      coming - mark PSP:0105h, and write the top of the screen ITSELF, then
;      wait for any key, set mode 3, print `DWFS BACK`, and exit with that key
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================
cpu 8086
org 0x100

    jmp short start
    nop
count: dw 0                         ; PSP:0103h - lines printed
gflag: dw 0                         ; PSP:0105h - 1 once mode 4 was set
start:
    mov dx, s_hello
    mov ah, 0x09
    int 0x21
.line:
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
    add bx, 9
.spin:
    mov ax, [es:0x6C]
    sub ax, bx
    js .spin
    inc word [count]
    mov ax, [count]                 ; the count's low digit, so a captured
    and al, 7                       ; screen shows which line it froze on
    add al, '0'
    mov [s_n + 5], al
    mov dx, s_n
    mov ah, 0x09
    int 0x21
    mov ah, 0x01
    int 0x16
    jz .line
    xor ah, ah
    int 0x16
    cmp al, 'q'
    je .quit
    cmp al, 'g'
    jne .line
    mov ax, 0x0004                  ; CGA 320x200x4
    int 0x10
    mov word [gflag], 1
    mov ax, 0xB800                  ; ...and drawn on BY HAND, as a game does
    mov es, ax
    xor di, di
    mov cx, 400
    mov ax, 0xFFFF
    cld
    rep stosw
    xor ah, ah
    int 0x16
    push ax
    mov ax, 0x0003
    int 0x10
    mov dx, s_back
    mov ah, 0x09
    int 0x21
    pop ax
.quit:
    mov ah, 0x4C
    int 0x21

s_hello: db 'DWFS START', 13, 10, '$'
s_n:     db 'DWFS 0', 13, 10, '$'
s_back:  db 'DWFS BACK', 13, 10, '$'
