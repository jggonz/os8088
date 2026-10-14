; =============================================================================
; apps/redline/csne.inc - the C&T NEAT row's knob (SPEC.md 103.6.2)
;
; Shared as source by REDLINE and RLAPPLY: cs_ne_put is the one write either
; of them makes. The NEAT (CS8221: 82C211 bus controller, 82C212 memory
; controller) keeps its registers behind 22h/23h, an index then the data,
; with "a fresh 22h write before every 23h access" [NEAT p29, p46] - and
; they READ BACK, which the FE3600's do not: the setting is a fact a row can
; check after every write rather than a measurement.
;
;   61h  RA1 "Command Delay" [NEAT p30], each field 0..3 BCLKs
;        bits 1:0  AT I/O command delay          <- knob 3, book 1
;        bits 3:2  8-bit AT memory command delay <- knob 2, book 1
;        bits 5:4  16-bit memory command delay   (kept: 0 already)
;        bit 6 quick mode off, bit 7 address hold (kept)
;   62h  RA2 "Wait States" [NEAT p31]
;        bits 1:0  BCLK source                   (kept - the bus CLOCK)
;        bits 3:2  8-bit AT cycle waits, 00 = 2  <- knob 1, book 4
;        bits 5:4  16-bit AT cycle waits, 00 = 0 <- knob 4, book 1
;        bits 7:6  reserved - the PB286LP reads them 01, so every write
;                  KEEPS the bits it does not own
;
; Index 62h is OCW2 "specific EOI" on an idle level to an 8259 that aliases
; 22h, which is why the probes may touch it (103.6.1) - but a DATA write to
; 23h on such a board is an IMR write. That is never reached: a row writes
; only after its probe FOUND the NEAT, and the alias check has run first;
; RLAPPLY writes only on the BIOS fingerprint the setting was saved on.
;
; Mfg spec is what the PB286LP's BIOS runs, which is also the book's prose
; for every field: 4 8-bit waits and 1 16-bit ("8 bit AT memory cycles will
; have 4 wait states, 16 bit AT memory cycles will have 1") where its
; register page says 5 and 3 [NEAT p25, p31], and one command delay each,
; the register page's default (61h = 45h, read on the PB286LP).
; =============================================================================

CS_NE_W8    equ 1                   ; knob 1: 62h 3:2, 8-bit waits 2..5
CS_NE_C8    equ 2                   ; knob 2: 61h 3:2, 8-bit memory command
                                    ;   delay, 0..3 BCLK [NEAT p30]
CS_NE_CIO   equ 3                   ; knob 3: 61h 1:0, I/O command delay
CS_NE_W16   equ 4                   ; knob 4: 62h 5:4, 16-bit waits 0..3
CS_NE_NK    equ 4
CS_NE_BOOK  equ 4
CS_NE_FLOOR equ 2
CS_NE_MAXW  equ 5

; The knobs' fields, three bytes each: index, shift, and the value a field
; of zero means (2 for the 8-bit waits, whose 00 is 2 waits; 0 elsewhere).
; Every field is two bits wide, and 61h and 62h are R/W in every field
; [NEAT p30, p31]
cs_ne_kt:   db 0x62, 2, 2
            db 0x61, 2, 0
            db 0x61, 0, 0
            db 0x62, 4, 0

; cs_ne_put - AL = a knob (CS_NE_*), AH = its value: read the register,
; replace that field, write the rest back as the chip had it, a fresh index
; before each access and interrupts off throughout. CF = 1 no such knob or
; a value outside its field, and nothing is written. Every register kept
cs_ne_put:
    push ax
    push bx
    push cx
    push dx
    cmp al, 1
    jb .no
    cmp al, CS_NE_NK
    ja .no
    dec al
    mov bl, al
    add bl, al
    add bl, al
    xor bh, bh                      ; BX = the knob's row of cs_ne_kt
    sub ah, [cs_ne_kt + bx + 2]
    jb .no
    cmp ah, 3
    ja .no
    mov cl, [cs_ne_kt + bx + 1]
    shl ah, cl                      ; AH = the field, in place
    mov ch, 3
    shl ch, cl
    not ch                          ; CH = every other bit
    mov dl, [cs_ne_kt + bx]         ; DL = the index
    pushf
    cli
    mov al, dl
    out 0x22, al
    in al, 0x23
    and al, ch
    or ah, al
    mov al, dl                      ; a fresh index before the data
    out 0x22, al
    mov al, ah
    out 0x23, al
    popf
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_ne_set - AL = 8-bit wait states, 2..5 (the sweep's and RLAPPLY's
; spelling of knob 1). Every register kept
cs_ne_set:
    push ax
    mov ah, al
    mov al, CS_NE_W8
    call cs_ne_put
    pop ax
    ret

%ifndef CS_APPLY_ONLY
%macro CS_NE_SETK 2                 ; name, knob: AL = value, regs kept
%1:
    push ax
    mov ah, al
    mov al, %2
    call cs_ne_put
    pop ax
    ret
%endmacro
    CS_NE_SETK cs_ne_setc8, CS_NE_C8
    CS_NE_SETK cs_ne_setcio, CS_NE_CIO
    CS_NE_SETK cs_ne_setw16, CS_NE_W16

; cs_ne_kget - AL = a knob (1..CS_NE_NK): AL = its value, read back.
; Interrupts off around the index/data pair. Every other register kept
cs_ne_kget:
    push bx
    push cx
    dec al
    mov bl, al
    add bl, al
    add bl, al
    xor bh, bh
    pushf
    cli
    mov al, [cs_ne_kt + bx]
    out 0x22, al
    in al, 0x23
    popf
    mov cl, [cs_ne_kt + bx + 1]
    shr al, cl
    and al, 3
    add al, [cs_ne_kt + bx + 2]
    pop cx
    pop bx
    ret
%macro CS_NE_GETK 2                 ; name, knob: AL = its value
%1:
    mov al, %2
    jmp cs_ne_kget
%endmacro
    CS_NE_GETK cs_ne_getc8, CS_NE_C8
    CS_NE_GETK cs_ne_getcio, CS_NE_CIO
    CS_NE_GETK cs_ne_getw16, CS_NE_W16

; cs_ne_get - AL = the 8-bit wait states 62h says, AH = the 16-bit ones
cs_ne_get:
    mov al, CS_NE_W16
    call cs_ne_kget
    mov ah, al
    mov al, CS_NE_W8
    jmp cs_ne_kget
%endif
