; =============================================================================
; apps/redline/csne.inc - the C&T NEAT row's knob (SPEC.md 103.6.2)
;
; Shared as source by REDLINE and RLAPPLY: cs_ne_set is the one write either
; of them makes. The NEAT (CS8221: 82C211 bus controller, 82C212 memory
; controller) keeps its registers behind 22h/23h, an index then the data,
; with "a fresh 22h write before every 23h access" [NEAT p29, p46] - and
; they READ BACK, which the FE3600's do not: the setting is a fact a row can
; check after every write rather than a measurement.
;
;   62h  RA2 "Wait States" [NEAT p31]
;        bits 1:0  BCLK source           (kept)
;        bits 3:2  8-bit AT cycle waits  00 = 2 ... 11 = 5   <- the knob
;        bits 5:4  16-bit AT cycle waits 00 = 0 ... 11 = 3   (kept)
;        bits 7:6  reserved - the PB286LP reads them 01, so they are KEPT
;                  rather than written as the book's zero
;
; Index 62h is OCW2 "specific EOI" on an idle level to an 8259 that aliases
; 22h, which is why the probes may touch it (103.6.1) - but a DATA write to
; 23h on such a board is an IMR write. That is never reached: a row writes
; only after its probe FOUND the NEAT, and the alias check has run first;
; RLAPPLY writes only on the BIOS fingerprint the setting was saved on.
;
; Mfg spec is 4 waits: the book's prose ("8 bit AT memory cycles will have 4
; wait states") against 5 on its register page [NEAT p25, p31] - and 4 is
; what the PB286LP's BIOS runs, so one board with this chipset runs it.
; =============================================================================

CS_NE_W8    equ 1                   ; the knob: 8-bit wait states
CS_NE_BOOK  equ 4
CS_NE_FLOOR equ 2
CS_NE_MAXW  equ 5

; cs_ne_set - AL = 8-bit wait states, 2..5: read 62h, replace bits 3:2,
; write it back, interrupts off throughout. Preserves every register.
cs_ne_set:
    push ax
    push bx
    pushf
    cli
    mov bl, al                      ; BL = the field, in bits 3:2
    sub bl, CS_NE_FLOOR
    and bl, 3
    shl bl, 1
    shl bl, 1
    mov al, 0x62
    out 0x22, al
    in al, 0x23
    and al, 0xF3                    ; every other bit as the chip has it
    or bl, al
    mov al, 0x62                    ; a fresh index before the data
    out 0x22, al
    mov al, bl
    out 0x23, al
    popf
    pop bx
    pop ax
    ret

%ifndef CS_APPLY_ONLY
; cs_ne_get - AL = the 8-bit wait states 62h says, AH = the 16-bit ones.
; Read-only; interrupts off around the index/data pair
cs_ne_get:
    pushf
    cli
    mov al, 0x62
    out 0x22, al
    in al, 0x23
    popf
    mov ah, al
    shr al, 1
    shr al, 1
    and al, 3
    add al, CS_NE_FLOOR
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    and ah, 3
    ret
%endif
