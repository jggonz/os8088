; =============================================================================
; os8088 - tests/dostrap/dwpoll.asm - DWPOLL.COM, a keystroke the way
; WordPerfect 4.1 answers one (SPEC.md 96.54.5.1)
;
; WordPerfect is TopView-aware: it asks int 10h AH=FEh once for the buffer
; to write, writes the screen there itself, and its main loop - idle or not -
; asks int 16h AH=01h "a key?", tells int 10h AH=FFh "all 2,000 cells
; changed" and asks the shift state. A typed letter is answered by MANY
; passes of that loop, not by one. This is that loop with nothing else in it:
;
;   - AH=FEh once, with ES = the mode's text segment and DI = 0;
;   - a key (AH=00h) starts a burst of PASSES passes, each an AH=01h poll, a
;     cell of the buffer written and an AH=FFh for the whole screen - the
;     FFh, as WordPerfect's, only when FEh moved the buffer;
;   - after the burst the key itself is written at the next column of row 0,
;     announced with AH=FFh, and the word at PSP:0103h counts it - which is
;     what a harness times the key against.
;
; `q` exits. It runs under a real IBM DOS 3.30 unchanged (AH=FEh and FFh
; leave ES:DI alone with no multitasker, and the ROM ignores FFh).
; =============================================================================
cpu 8086
org 0x100

PASSES  equ 2000

    jmp short start
    nop
done:   dw 0                        ; PSP:0103h - keys answered
start:
    mov ah, 0x0F
    int 0x10
    mov bx, 0xB800
    cmp al, 7
    jne .seg
    mov bx, 0xB000
.seg:
    mov es, bx
    xor di, di
    mov ah, 0xFE
    int 0x10                        ; ES:DI = where to write, from here on
    mov [buf], di
    mov [buf + 2], es
    mov ax, es                      ; REDIRECTED? WordPerfect's own test: it
    cmp ax, bx                      ; says AH=FFh only to a multitasker that
    jne .tv                         ; moved its buffer - a plain BIOS may not
    or di, di                       ; know the call at all
    jnz .tv
    mov byte [tv], 0
.tv:
.key:
    xor ah, ah
    int 0x16
    cmp al, 'q'
    je .quit
    push ax
    mov cx, PASSES
.pass:
    push cx
    mov ah, 0x01                    ; "a key?" - the answer is not used
    int 0x16
    les di, [buf]
    mov al, cl                      ; a cell written, as the loop's own work
    and al, 0x0F
    add al, '0'
    mov [es:di + 160 * 24 + 158], al
    call update                     ; "the whole screen changed"
    pop cx
    loop .pass
    pop ax
    les di, [buf]
    mov bx, [col]
    shl bx, 1
    mov [es:di + bx], al
    mov byte [es:di + bx + 1], 0x07
    inc byte [col]
    and byte [col], 63
    call update
    inc word [done]
    jmp .key
.quit:
    mov ah, 0x4C
    int 0x21

; update - int 10h AH=FFh for all 2,000 cells, when TopView moved the buffer
update:
    cmp byte [tv], 0
    je .r
    les di, [buf]
    mov cx, 2000
    mov ah, 0xFF
    int 0x10
.r:
    ret

tv:     db 1
buf:    dd 0
col:    dw 0
