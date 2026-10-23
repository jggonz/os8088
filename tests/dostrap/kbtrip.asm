; =============================================================================
; os8088 - tests/dostrap/kbtrip.asm
;
; KBTRIP.COM - does a key reach a DOS program the kernel has just handed the
; machine to? (docs/plans/KERN-MEGA-PLAN.md 14.62, tests/megakbtrip.py)
;
;   nasm -f bin -w+error -o KBTRIP.COM tests/dostrap/kbtrip.asm
;
; The 8042 has ONE output buffer for the keyboard and the PS/2 mouse. A byte
; left in it with nobody to take it - an AUXILIARY byte with IRQ12 masked, or
; a byte whose interrupt edge an 8259 re-initialisation erased - keeps OBF set
; and the line high with no new edge, and the controller then sends NOTHING
; from either device: the keyboard is dead inside the program while the
; program runs on. This reads port 64h as it is handed the machine, says it is
; ready, waits up to WAIT_TICKS for a key through INT 16h, and reports each
; step on port E9h (Bochs's port_e9_hack; nothing at all on a machine without
; one) and on the screen:
;
;   KBTRIP entry 64=SS        the 8042's status at entry: bit 0 OBF, bit 5 AUX
;   KBTRIP ready              the harness sends its key now
;   KBTRIP key SSKK           a key arrived (SS its scan code, KK its ASCII)
;                             - exit code 1
;   KBTRIP nokey 64=SS        none in WAIT_TICKS, and the status - exit code 2
;
; It touches no port but 64h (read) and E9h (write), so it runs unchanged under
; a real DOS (docs/DOS-DEBUGGING.md's rule).
;
; NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the rest of the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

WAIT_TICKS  equ 546                 ; 30 s of BIOS ticks

start:
    in al, 0x64                     ; FIRST, before any DOS call can move it
    mov [kbt_st], al
    mov si, s_entry
    call say
    mov al, [kbt_st]
    call hex
    call eol
    mov si, s_ready
    call say
    call eol
    xor ax, ax
    mov es, ax
    mov bx, [es:0x046C]             ; the deadline, in ticks
    add bx, WAIT_TICKS
.wait:
    mov ah, 0x01
    int 0x16
    jnz .got
    mov ax, [es:0x046C]
    cmp ax, bx
    jne .wait
    mov si, s_nokey
    call say
    in al, 0x64
    call hex
    call eol
    mov ax, 0x4C02
    int 0x21
.got:
    xor ah, ah
    int 0x16
    push ax
    mov si, s_key
    call say
    pop ax
    push ax
    mov al, ah
    call hex
    pop ax
    call hex
    call eol
    mov ax, 0x4C01
    int 0x21

; say - the NUL-terminated string at SI, to E9h and the screen
say:
    lodsb
    or al, al
    jz .x
    call chr
    jmp short say
.x:
    ret

; chr - AL to E9h and the screen (DOS AH=02h). Every register kept
chr:
    push ax
    push dx
    out 0xE9, al
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

; hex - AL as two hex digits
hex:
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
    and al, 0x0F
.nib:
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 'A' - '0' - 10
.o:
    jmp short chr

eol:
    mov al, 13
    call chr
    mov al, 10
    jmp short chr

s_entry:    db 'KBTRIP entry 64=', 0
s_ready:    db 'KBTRIP ready', 0
s_key:      db 'KBTRIP key ', 0
s_nokey:    db 'KBTRIP nokey 64=', 0
kbt_st:        db 0
