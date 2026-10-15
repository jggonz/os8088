; =============================================================================
; apps/redline/csknob.inc - every chipset KNOB that can be written, and the
; one routine that writes them (SPEC.md 103.6.2)
;
; Shared as source by REDLINE and RLAPPLY: cs_kput is the one chipset write
; either of them makes for a readable chipset (the FE3600's write-only R4 is
; csfe.inc's). A KNOB is a field of an indexed register, so one table row
; says all of it:
;
;   +0  db  protocol    CSKP_*: which ports, and how an access is made
;   +1  db  index       the register's index (byte protocols)
;   +2  dw  port        the register's own port (CSKP_WD: xx72h)
;   +4  db  shift       the field's lowest bit
;   +5  db  mask        the field's width, unshifted: 1 or 3
;   +6  db  base        the value a field of zero means...
;   +7  db  flags       ...counting DOWN from it with CSKF_INV (a wait state
;                       encoded 00 = 5); the low bits the largest FIELD that
;                       may be written, which is what keeps a bus clock off
;                       ATCLK (a hang on a board without the crystal) and off
;                       every reserved code
;
; A table is a count byte and then its rows, knob 1 first. Every write reads
; the register, replaces the one field and writes the rest back as the chip
; had them, interrupts off; reserved bits are never touched.
;
; THE PROTOCOLS, and what each one's safety rests on:
;   CSKP_22   index 22h, data 23h, a fresh index before every data access:
;             C&T NEAT [NEAT p29, p46] and CS8230 [CS8230 p15-16]. Written
;             only where the row's probe FOUND the chip (chipset.inc).
;   CSKP_EC   index ECh, data EDh: VLSI SCAMP [S1 p63]. The index is read
;             back before every data access - a LOCKED chip drops the index
;             write and the data would be the previous register's - and a
;             mismatch writes nothing. EEh/EFh/F9h/FBh are never touched.
;   CSKP_24   index 22h, data 24h, the index reset after every access: OPTi
;             82C283 [283 p11].
;   CSKP_WD   word registers at full 16-bit ports, unlocked by DAh to F073h:
;             WD76C10 [WD91 p12-15]. On a board that decodes only A9-A0 those
;             ports are 072h/073h, the CLOCK CHIP's, so every access first
;             points the RTC index at 0Dh - register D, read-only - and is
;             made only where the probe FOUND the part.
; =============================================================================

CSKP_22     equ 1
CSKP_EC     equ 2
CSKP_24     equ 3
CSKP_WD     equ 4
CSKF_INV    equ 0x80
CSK_ROW     equ 8                   ; bytes a knob

; --- C&T NEAT (82C211): 61h command delays, 62h wait states and clock -------
CS_NE_W8    equ 1                   ; 62h 3:2, 8-bit waits 2..5
CS_NE_C8    equ 2                   ; 61h 3:2, 8-bit memory command delay
CS_NE_CIO   equ 3                   ; 61h 1:0, I/O command delay
CS_NE_W16   equ 4                   ; 62h 5:4, 16-bit waits 0..3
CS_NE_BCLK  equ 5                   ; 62h 1:0, the bus clock as its divider:
                                    ;   2 = CLK2IN/2 (00), 1 = CLK2IN (01)
CS_NE_BOOK  equ 4
CS_NE_FLOOR equ 2
CS_NE_MAXW  equ 5
cs_kt_ne:   db 5
            db CSKP_22, 0x62, 0, 0, 2, 3, 2, 3
            db CSKP_22, 0x61, 0, 0, 2, 3, 0, 3
            db CSKP_22, 0x61, 0, 0, 0, 3, 0, 3
            db CSKP_22, 0x62, 0, 0, 4, 3, 0, 3
            db CSKP_22, 0x62, 0, 0, 0, 3, 2, CSKF_INV | 1

; --- C&T CS8230 / CS8231 (82C301), the NEAT's 386DX sibling [CS8230 p16-17]
; with its wait-state codes INVERTED (00 = the most) and its bus clock a
; divider of the processor clock: 3 = /3 (00, the default), 2 = /2 (01)
cs_kt_ca:   db 5
            db CSKP_22, 0x06, 0, 0, 2, 3, 5, CSKF_INV | 3   ; 8-bit 5..2
            db CSKP_22, 0x05, 0, 0, 2, 3, 0, 3              ; 8-bit mem delay
            db CSKP_22, 0x05, 0, 0, 0, 3, 0, 3              ; I/O delay
            db CSKP_22, 0x06, 0, 0, 4, 3, 3, CSKF_INV | 3   ; 16-bit 3..0
            db CSKP_22, 0x06, 0, 0, 0, 3, 3, CSKF_INV | 1   ; clock /3, /2

; --- VLSI SCAMP VL82C310/311 [S1 p49-54, p72-77]: BUSCTL 16h's four
; one-bit fields - PC/AT's timing is the ZERO side of each - and the slow
; SYSCLK divider in CLKCTL 07h, as the divider / 2: 1 = /2 ... 4 = /8
cs_kt_sm:   db 5
            db CSKP_EC, 0x16, 0, 0, 0, 1, 4, 1              ; 8WS: 4 or 5
            db CSKP_EC, 0x16, 0, 0, 3, 1, 0, 1              ; CMDLY2: 8-bit
                                    ; memory AND 8/16-bit I/O, one bit
            db CSKP_EC, 0x16, 0, 0, 2, 1, 0, 1              ; CMDLY1: 16-bit
            db CSKP_EC, 0x16, 0, 0, 1, 1, 0, 1              ; 16WS: 0 or 1
            db CSKP_EC, 0x07, 0, 0, 0, 3, 1, 3              ; SCLKDIV

; --- WD76C10 [WD91 p32-34, WD93 p39-41]: port 1872h, bus timing ------------
cs_kt_w7:   db 5
            db CSKP_WD, 0, 0x72, 0x18, 0, 3, 2, 3           ; WSM8 2..5
            db CSKP_WD, 0, 0x72, 0x18, 2, 3, 2, 3           ; WSI8: I/O 2..5
            db CSKP_WD, 0, 0x72, 0x18, 4, 1, 1, 1           ; WSM16 1..2
            db CSKP_WD, 0, 0x72, 0x18, 5, 1, 1, 1           ; WSI16 1..2
            db CSKP_WD, 0, 0x72, 0x18, 10, 1, 2, CSKF_INV | 1 ; BUS_MOD /2,/1

; --- OPTi 82C283 [283 p13]: 14h bit 0, the AT clock, CPUCLK2/6 (0) or /4
; (1), as the divider / 2: 3 or 2. Its only bus knob
cs_kt_o3:   db 1
            db CSKP_24, 0x14, 0, 0, 0, 1, 3, CSKF_INV | 1


; -----------------------------------------------------------------------------
; cs_rg_rd - BX = a knob row: AX = its register as it reads. CF = 1 it could
; not be read (a SCAMP whose index did not take: locked). Interrupts off is
; the CALLER's (cs_kput, cs_kget). Preserves every other register
; cs_rg_wr - BX = a knob row, AX = the register's new value
; -----------------------------------------------------------------------------
cs_rg_rd:
    push dx
    xor ah, ah
    mov dl, [bx]
    cmp dl, CSKP_WD
    je .wd
    mov al, [bx + 1]
    cmp dl, CSKP_EC
    jne .ix
    out 0xEC, al                    ; a SCAMP indexes at ECh, read back
    mov ah, al
    in al, 0xEC
    cmp al, ah
    jne .no
    in al, 0xED
    jmp short .ok
.ix:
    out 0x22, al                    ; CSKP_22 and CSKP_24 index at 22h
    cmp dl, CSKP_24
    je .d24
    in al, 0x23
    jmp short .ok
.d24:
    in al, 0x24
.ok:
    xor ah, ah
    clc
    pop dx
    ret
.no:
    stc
    pop dx
    ret
.wd:
    call cs_wd_open
    mov dx, [bx + 2]
    in ax, dx
    call cs_wd_close
    clc
    pop dx
    ret

cs_rg_wr:
    push ax
    push dx
    mov dl, [bx]
    cmp dl, CSKP_WD
    je .wd
    push ax
    mov al, [bx + 1]                ; a FRESH index before the data
    cmp dl, CSKP_EC
    jne .ix
    out 0xEC, al
    jmp short .ixd
.ix:
    out 0x22, al
.ixd:
    pop ax
    cmp dl, CSKP_EC
    je .ec
    cmp dl, CSKP_24
    je .d24
    out 0x23, al
    jmp short .out
.ec:
    out 0xED, al
    jmp short .out
.d24:
    out 0x24, al
    jmp short .out
.wd:
    call cs_wd_open
    mov dx, [bx + 2]
    out dx, ax
    call cs_wd_close
.out:
    pop dx
    pop ax
    ret

; cs_wd_open / cs_wd_close - the WD76C10's unlock bracket. First the RTC
; index to 0Dh, read-only register D, so that a board which aliases xx73h
; onto 071h writes nothing it can lose; then DAh to F073h. Close relocks
; unless the BIOS had left it unlocked. Every register preserved
cs_wd_open:
    push ax
    push dx
    mov al, 0x0D
    out 0x70, al
    mov dx, 0xF073
    mov al, 0xDA
    out dx, al
    pop dx
    pop ax
    ret
cs_wd_close:
    cmp byte [cs_wd_ul], 0
    jne .out
    push ax
    push dx
    mov dx, 0xF073
    xor al, al
    out dx, al
    pop dx
    pop ax
.out:
    ret

; cs_krow - SI = a table, AL = a knob: BX = its row; CF = 1 no such knob
cs_krow:
    or al, al
    jz .no
    cmp al, [si]
    ja .no
    push ax
    dec al
    mov bl, CSK_ROW
    mul bl
    mov bx, ax
    lea bx, [si + 1 + bx]
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cs_kput - SI = a table, AL = a knob, AH = its value: read, replace the one
; field, write back. CF = 1 no such knob, a value outside its field, or a
; register that could not be read - and nothing written. Every register kept
; -----------------------------------------------------------------------------
cs_kput:
    push ax
    push bx
    push cx
    push dx
    call cs_krow
    jc .out
    mov dl, ah                      ; DL = the value -> the field
    mov dh, [bx + 7]
    test dh, CSKF_INV
    jz .up
    mov al, [bx + 6]
    sub al, dl
    jc .no
    mov dl, al
    jmp short .lim
.up:
    sub dl, [bx + 6]
    jc .no
.lim:
    and dh, 0x7F
    cmp dl, dh
    ja .no
    pushf
    cli
    call cs_rg_rd
    jc .nord
    mov cl, [bx + 4]
    xor dh, dh                      ; DX = the field, in place
    shl dx, cl
    push ax
    mov al, [bx + 5]
    xor ah, ah
    shl ax, cl
    not ax                          ; AX = every other bit
    mov cx, ax
    pop ax
    and ax, cx
    or ax, dx
    call cs_rg_wr
    popf
    clc
    jmp short .out
.nord:
    popf
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifndef CS_APPLY_ONLY
; cs_kget - SI = a table, AL = a knob: AL = its value, read back (0FFh when
; the register could not be read). AH is lost; every other register kept
cs_kget:
    push bx
    push cx
    push dx
    call cs_krow
    jc .bad
    pushf
    cli
    call cs_rg_rd
    jc .nord
    popf
    mov cl, [bx + 4]
    shr ax, cl
    and al, [bx + 5]
    mov dl, [bx + 6]
    test byte [bx + 7], CSKF_INV
    jz .up
    sub dl, al
    mov al, dl
    jmp short .out
.up:
    add al, dl
    jmp short .out
.nord:
    popf
.bad:
    mov al, 0FFh
.out:
    pop dx
    pop cx
    pop bx
    ret
%endif

; --- every knob as the sweep and RLAPPLY call it: AL = value, regs kept ----
%macro CS_KSET 3                    ; name, table, knob
%1:
    push ax
    push si
    mov ah, al
    mov al, %3
    mov si, %2
    call cs_kput
    pop si
    pop ax
    ret
%endmacro
%macro CS_KGET 3                    ; name, table, knob: AL = value
%1:
    push si
    mov al, %3
    mov si, %2
    call cs_kget
    pop si
    ret
%endmacro

; the NEAT's, by the names csprobe and csweep know
cs_ne_put:                          ; AL = knob, AH = value: CF = refused
    push si
    mov si, cs_kt_ne
    call cs_kput
    pop si
    ret
    CS_KSET cs_ne_set, cs_kt_ne, CS_NE_W8
%ifndef CS_APPLY_ONLY
    CS_KSET cs_ne_setc8, cs_kt_ne, CS_NE_C8
    CS_KSET cs_ne_setcio, cs_kt_ne, CS_NE_CIO
    CS_KSET cs_ne_setw16, cs_kt_ne, CS_NE_W16
    CS_KSET cs_ne_setbclk, cs_kt_ne, CS_NE_BCLK
    CS_KGET cs_ne_getw8, cs_kt_ne, CS_NE_W8
    CS_KGET cs_ne_getc8, cs_kt_ne, CS_NE_C8
    CS_KGET cs_ne_getcio, cs_kt_ne, CS_NE_CIO
    CS_KGET cs_ne_getw16, cs_kt_ne, CS_NE_W16
    CS_KGET cs_ne_getbclk, cs_kt_ne, CS_NE_BCLK
cs_ne_kget:                         ; AL = knob: AL = its value
    push si
    mov si, cs_kt_ne
    call cs_kget
    pop si
    ret
cs_ne_get:                          ; AL = 8-bit waits, AH = 16-bit
    push bx
    mov al, CS_NE_W16
    call cs_ne_kget
    mov bh, al
    mov al, CS_NE_W8
    call cs_ne_kget
    mov ah, bh
    pop bx
    ret
; ...and the other four rows', for csweep.inc's descriptors
%assign cs_kn 1
%rep 5
    CS_KSET cs_ca_set%[cs_kn], cs_kt_ca, cs_kn
    CS_KGET cs_ca_get%[cs_kn], cs_kt_ca, cs_kn
    CS_KSET cs_sm_set%[cs_kn], cs_kt_sm, cs_kn
    CS_KGET cs_sm_get%[cs_kn], cs_kt_sm, cs_kn
    CS_KSET cs_w7_set%[cs_kn], cs_kt_w7, cs_kn
    CS_KGET cs_w7_get%[cs_kn], cs_kt_w7, cs_kn
%assign cs_kn cs_kn + 1
%endrep
    CS_KSET cs_o3_set1, cs_kt_o3, 1
    CS_KGET cs_o3_get1, cs_kt_o3, 1
%endif
