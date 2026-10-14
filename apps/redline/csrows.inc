; =============================================================================
; apps/redline/csrows.inc - the CHIPSET ROWS (SPEC.md 103.6)
;
; Shared as SOURCE by REDLINE (the Tune tab) and RLAPPLY (the windowless
; applier, 103.6.5), the WEAVE/LOOM rule: one copy of what a chipset is and how
; it is found, never two. Nothing here draws.
;
; WHAT THIS FILE MAY TOUCH is the whole of its safety, so it is stated once:
;   * memory: the BIOS ROM at F000:0000-FFFF, read only;
;   * on an 8088/8086, NO PORT AT ALL - an XT clone's 22h can be a G2 GC100's
;     wait-state register itself, and an XT has no programmable bus timing to
;     find (103.6);
;   * on an AT-class CPU, in this order, each row reading only what it declares:
;       ECh/EDh       VLSI SCAMP and TOPCAT (never EEh/EFh: on those a READ
;                     of EFh resets the CPU)
;       1EDh/1EFh     Headland
;       22h/23h       C&T SCAT and NEAT - 286 CPUs only (an Intel 82335 keeps
;                     real registers at 22h on 386SX boards, and a Cyrix SLC
;                     uses the pair), and only after cs_alias says the pair
;                     does not mirror the 8259's 20h/21h.
;     The ONLY bytes a probe writes to 22h are 40h and 60h-67h: on a board
;     that aliases 22h onto the master 8259 those decode as OCW2 "no
;     operation" and "specific EOI on an idle level". 48h-4Fh/68h-6Fh (OCW3,
;     special mask mode) and anything with bit 4 set (ICW1, a PIC reset) are
;     never written (103.6.1).
; =============================================================================

CSR_CODE    equ 0                   ; dw: two ASCII, REDLINE.CFG's row code
CSR_NAME    equ 2                   ; dw: NUL name, as the drop-down shows it
CSR_IDENT   equ 4                   ; dw: read-only probe, or 0 (pick only)
CSR_STAT    equ 6                   ; db: CSS_*
CSR_PORTS   equ 7                   ; db: CSP_* - which ports the probe reads
CSR_APPLY   equ 8                   ; dw: AL = knob, AH = value -> set it,
                                    ;     CF = 1 not this row's knob; 0 = the
                                    ;     row cannot write (csapply.inc)
CSR_SIZE    equ 10

CSS_IDENTIFY  equ 0                 ; identifies and reports, writes nothing
CSS_DATASHEET equ 1                 ; the book's values, never run on iron
CSS_VERIFIED  equ 2                 ; run on real hardware

CSP_NONE    equ 0                   ; BIOS image only
CSP_ATPORT  equ 1                   ; a port pair nothing else on an AT owns
CSP_22H     equ 2                   ; 22h/23h: 286 only, after cs_alias

; Probe outcomes, one byte a row in cs_pres
CSO_NOTRUN  equ 0
CSO_SKIPCPU equ 1                   ; not on this CPU class
CSO_SKIPALI equ 2                   ; 22h/23h mirror the 8259
CSO_ABSENT  equ 3
CSO_FOUND   equ 4
CSO_PICK    equ 5                   ; no probe exists: the drop-down names it

CS_NONE     equ 0                   ; row 0 is "None / unknown"
CS_F3       equ 1                   ; WD FE3600 - row numbers are stable,
CS_CQ       equ 2                   ; RLAPPLY and the settings file use them
CS_SM       equ 3
CS_HL       equ 4
CS_SC       equ 5
CS_SX       equ 6
CS_NE       equ 7
CS_W7       equ 8
CS_O3       equ 9
CS_NROWS    equ 10

cs_rows:
    dw 'NO', cs_n_none,     0,             CSS_IDENTIFY  | (CSP_NONE << 8),   0
    dw 'F3', cs_n_fe3600,   cs_id_fe3600,  CSS_VERIFIED  | (CSP_NONE << 8),   cs_ap_fe
    dw 'CQ', cs_n_compaq,   cs_id_compaq,  CSS_IDENTIFY  | (CSP_NONE << 8),   0
    dw 'SM', cs_n_scamp,    cs_id_scamp,   CSS_IDENTIFY  | (CSP_ATPORT << 8), 0
    dw 'HL', cs_n_headland, cs_id_headland, CSS_IDENTIFY | (CSP_ATPORT << 8), 0
    dw 'SC', cs_n_scat,     cs_id_scat,    CSS_IDENTIFY  | (CSP_22H << 8),    0
    dw 'SX', cs_n_scatsx,   cs_id_scatsx,  CSS_IDENTIFY  | (CSP_22H << 8),    0
    dw 'NE', cs_n_neat,     cs_id_neat,    CSS_VERIFIED  | (CSP_22H << 8),    cs_ap_ne
    dw 'W7', cs_n_wd76,     0,             CSS_IDENTIFY  | (CSP_NONE << 8),   0
    dw 'O3', cs_n_opti283,  0,             CSS_IDENTIFY  | (CSP_NONE << 8),   0

cs_n_none:     db 'None / unknown', 0
cs_n_fe3600:   db 'WD FE3600', 0
cs_n_compaq:   db 'Compaq', 0
cs_n_scamp:    db 'VLSI SCAMP/TOPCAT', 0
cs_n_headland: db 'Headland HT/GC', 0
cs_n_scat:     db 'C&T SCAT', 0
cs_n_scatsx:   db 'C&T SCATsx', 0
cs_n_neat:     db 'C&T NEAT', 0
cs_n_wd76:     db 'WD76C10', 0
cs_n_opti283:  db 'OPTi 82C283', 0

; --- the state every caller reads ---------------------------------------------
cs_tier:    db 0                    ; OSAPI_CPU_INFO's AL
cs_found:   db CS_NONE              ; the first row a probe found
cs_crc:     dw 0                    ; the BIOS fingerprint (103.6.1)
cs_date:    times 8 db 0            ; F000:FFF5
cs_model:   db 0                    ; F000:FFFE
cs_compaqf: db 0                    ; 'COMPAQ' at F000:FFEA
cs_aliased: db 0                    ; 22h/23h mirror 20h/21h
cs_pres:    times CS_NROWS db 0     ; CSO_* per row
cs_pval:    times CS_NROWS dw 0     ; the raw value each probe read

; --- FE3600 allowlist: BIOS CRC and the date bytes of boards known to carry it
; (103.6.2). A board joins by its own field run printing its fingerprint.
cs_fe_allow:
    dw 2                            ; count of entries below
    ; dw crc, then 8 date bytes, per entry
    dw 0x2016                       ; Packard Bell Victory 286: Phoenix 80286
    db '01/15/88'                   ; ROM BIOS PLUS 3.10 04, "Packard Bell Ver
                                    ; 1.04", model FCh - RetroWeb's dump, its
                                    ; even/odd pair interleaved
    dw 0xBA37                       ; ...and the owner's own Victory, whose
    db '01/15/88'                   ; ROM is NOT that dump: same date and model,
                                    ; another fingerprint (its field run's
                                    ; CHIPSET1.TXT, 2026-10-13)

%ifdef CS_APPLY_ONLY
; RLAPPLY: the fingerprint and the table, and no probe at all - an Apply's
; safety is the fingerprint (csapply.inc), so it runs nothing that touches a
; port before it knows the setting is this machine's
cs_id_fe3600   equ 0
cs_id_compaq   equ 0
cs_id_scamp    equ 0
cs_id_headland equ 0
cs_id_scat     equ 0
cs_id_scatsx   equ 0
cs_id_neat     equ 0
%else
; =============================================================================
; cs_identify - the whole read-only pass. Fills every field above.
; clobbers: flags (every register preserved)
; =============================================================================
cs_identify:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call OSAPI_CPU_INFO
    mov [cs_tier], al
    mov byte [cs_found], CS_NONE
    push ds
    pop es
    mov di, cs_pres
    mov cx, CS_NROWS
    xor al, al
    cld
    rep stosb
    call cs_fingerprint
    call cs_alias
    mov si, cs_rows + CSR_SIZE      ; row 0 has no probe
    mov bx, 1
.row:
    cmp bx, CS_NROWS
    jae .done
    mov ax, [si + CSR_IDENT]
    or ax, ax
    jnz .has
    mov byte [cs_pres + bx], CSO_PICK
    jmp short .next
.has:
    mov al, [si + CSR_PORTS]
    cmp al, CSP_NONE
    je .run
    cmp byte [cs_tier], CPU_8086    ; no port of any kind on an XT
    je .skipcpu
    cmp al, CSP_22H
    jne .run
    cmp byte [cs_tier], CPU_286     ; 22h/23h: a 286 only...
    jne .skipcpu
    cmp byte [cs_aliased], 0        ; ...and only when they are not the PIC
    je .run
    mov byte [cs_pres + bx], CSO_SKIPALI
    jmp short .next
.skipcpu:
    mov byte [cs_pres + bx], CSO_SKIPCPU
    jmp short .next
.run:
    push bx
    push si
    call word [si + CSR_IDENT]      ; CF = 0 found; AX = the raw value
    pop si
    pop bx
    mov byte [cs_pres + bx], CSO_ABSENT ; the probe's CF, read before
    jc .val                         ; anything that moves a flag
    mov byte [cs_pres + bx], CSO_FOUND
    cmp byte [cs_found], CS_NONE
    jne .val
    mov [cs_found], bl
.val:
    mov di, bx
    shl di, 1
    mov [cs_pval + di], ax
.next:
    add si, CSR_SIZE
    inc bx
    jmp short .row
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; cs_fingerprint - CRC-16/CCITT (init FFFFh) of F000:0000-FFFF, the date at
; F000:FFF5 and the model byte at F000:FFFE, and the Compaq signature. A
; nibble table, so 32 bytes of data and no table to build: ~0.35 s of an
; 8088 at 4.77 MHz, ~30 ms of a 16 MHz 286.
; -----------------------------------------------------------------------------
cs_fingerprint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, 0xF000
    mov es, ax
    mov dx, 0xFFFF                  ; the CRC
    mov cl, 4
    xor si, si
.byte:
    mov al, [es:si]
    mov ah, al
    shr al, cl                      ; the high nibble first
    mov bl, dh
    shr bl, cl
    xor bl, al
    xor bh, bh
    shl bx, 1
    shl dx, cl
    xor dx, [cs_crctab + bx]
    mov al, ah
    and al, 0x0F
    mov bl, dh
    shr bl, cl
    xor bl, al
    xor bh, bh
    shl bx, 1
    shl dx, cl
    xor dx, [cs_crctab + bx]
    inc si
    jnz .byte                       ; 65,536 bytes: SI wraps to 0
    mov [cs_crc], dx
    mov si, 0xFFF5
    mov di, cs_date
    mov cx, 8
.date:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .date
    mov al, [es:0xFFFE]
    mov [cs_model], al
    mov byte [cs_compaqf], 0
    mov si, 0xFFEA
    mov di, cs_s_compaq
    mov cx, 6
.cq:
    mov al, [es:si]
    cmp al, [di]
    jne .cqno
    inc si
    inc di
    loop .cq
    mov byte [cs_compaqf], 1
.cqno:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cs_crctab:  dw 0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7
            dw 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
cs_s_compaq: db 'COMPAQ'
%ifndef CS_APPLY_ONLY

; -----------------------------------------------------------------------------
; cs_alias - do 22h/23h read back as the master 8259's 20h/21h? Reads only.
; A pair that tracks the 8259 in BOTH registers twice over is the PIC itself,
; decoded coarsely, and no probe of ours may write there (103.6.1). A chipset
; whose data port happens to equal the IMR once is told apart by the second
; register: its index port does not read back the 8259's IRR.
; Only meaningful on a 286; harmless reads anywhere.
; -----------------------------------------------------------------------------
cs_alias:
    push ax
    push bx
    mov byte [cs_aliased], 0
    cmp byte [cs_tier], CPU_286
    jne .out
    pushf
    cli
    in al, 0x21
    mov bl, al
    in al, 0x23
    mov bh, al
    in al, 0x20
    mov ah, al
    in al, 0x22
    popf
    cmp bh, bl                      ; 23h = IMR...
    jne .out
    cmp al, ah                      ; ...and 22h = 20h's read
    jne .out
    mov byte [cs_aliased], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE PROBES. Each: read-only except for the index bytes 103.6.1 allows, CF = 0
; when found, AX = the raw value it read (the report prints it whether or not
; it matched, which is how a field run teaches a row about a board).
; =============================================================================

; FE3600: its registers are write-only and its 72h/73h alias the clock chip on
; other boards, so it is found by its BOARD - the allowlisted BIOS images.
cs_id_fe3600:
    push cx
    push si
    push di
    mov ax, [cs_crc]
    mov cx, [cs_fe_allow]
    mov si, cs_fe_allow + 2
    jcxz .no
.entry:
    cmp ax, [si]
    jne .skip
    push cx
    push si
    lea si, [si + 2]
    mov di, cs_date
    mov cx, 8
    push es
    push ds
    pop es
    repe cmpsb
    pop es
    pop si
    pop cx
    je .yes
.skip:
    add si, 10
    loop .entry
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop si
    pop cx
    ret

cs_id_compaq:
    xor ax, ax
    mov al, [cs_compaqf]
    cmp al, 1                       ; CF = 1 when the signature was not seen
    ret

; VLSI SCAMP, SCAMP II and TOPCAT: index ECh, data EDh, and register 00h the
; version byte - SCAMP VL82C310 D0h-D3h and 311/311L D4h-D7h [VL82C310/311
; book p75-77], SCAMP II 68h-6Fh [VL82C315A p112-113], TOPCAT VL82C320
; E0h-E3h [TOPCAT p63] and VL82C330 F0h-F3h [p114]. One index write, 00h, and
; it is never put back: nothing reads it between two of the BIOS's own
; accesses. EEh and EFh are never touched - a READ of EFh resets the CPU on
; all three families, and EEh moves A20.
cs_id_scamp:
    push bx
    pushf
    cli
    xor al, al
    out 0xEC, al
    in al, 0xED
    popf
    xor ah, ah
    mov bl, al
    and bl, 0xFC
    cmp bl, 0xD0
    je .yes
    cmp bl, 0xD4
    je .yes
    cmp bl, 0xE0
    je .yes
    cmp bl, 0xF0
    je .yes
    and bl, 0xF8
    cmp bl, 0x68
    je .yes
    pop bx
    stc
    ret
.yes:
    pop bx
    clc
    ret

; Headland HT/GC: index 1EDh (it reads back on parts that have it), data
; 1EFh; CR4's high nibble is the revision (0, 1, 2 or 8 documented). The index
; it found is put back.
cs_id_headland:
    push bx
    push cx
    push dx
    pushf
    cli
    mov dx, 0x1ED
    in al, dx
    mov bl, al                      ; the index as it stood
    mov al, 4
    out dx, al
    in al, dx
    mov bh, al                      ; does the index read back?
    mov dx, 0x1EF
    in al, dx
    mov ah, al                      ; CR4
    mov dx, 0x1ED
    mov al, bl
    out dx, al                      ; put the index back
    popf
    mov al, ah
    mov ah, bh                      ; AX = index readback : CR4, the report's
    cmp ah, 4
    jne .no
    mov dl, al                      ; the revision nibble: 0, 1, 2 or 8
    mov cl, 4
    shr dl, cl
    cmp dl, 0
    je .yes
    cmp dl, 1
    je .yes
    cmp dl, 2
    je .yes
    cmp dl, 8
    je .yes
.no:
    pop dx
    pop cx
    pop bx
    stc
    ret
.yes:
    pop dx
    pop cx
    pop bx
    clc
    ret

; C&T SCAT / SCATsx: index 22h, data 23h; register 40h reads 01h or 04h on
; SCAT and 13h on SCATsx. EMULATOR-DERIVED (86Box scat.c): the SCAT books are
; short-form and carry no register detail (docs/reports/CHIPSET-REGISTERS-
; 2026-10-13.md), so this row identifies and reports and never writes. 40h is
; OCW2 "no operation" to an aliased 8259.
cs_scatver:
    pushf
    cli
    mov al, 0x40
    out 0x22, al
    in al, 0x23
    popf
    xor ah, ah
    ret
cs_id_scat:
    call cs_scatver
    cmp al, 0x01
    je .yes
    cmp al, 0x04
    je .yes
    stc
    ret
.yes:
    clc
    ret
cs_id_scatsx:
    call cs_scatver
    cmp al, 0x13
    je .yes
    stc
    ret
.yes:
    clc
    ret

; C&T NEAT (82C211/82C212): index 22h, data 23h, a fresh 22h write before
; every 23h access [NEAT p29, p46]; registers 60h-62h and 64h-6Fh. Register
; 64h bit 7 = 0 says 82C212, bits 6:5 its revision; 60h bits 7:6 the 82C211's
; [NEAT p29-31]. Port 22h is not documented as readable, so the tell is the
; data: an empty bus floats FFh (bit 7 set), and an 8259 aliased at 22h/23h
; returns its IMR for EVERY index, so 60h, 62h and 64h reading alike is the
; PIC and not a NEAT. 60h/62h/64h are OCW2 "specific EOI" on idle levels 0, 2
; and 4 to an aliased 8259: harmless outside an ISR. 60h is never WRITTEN as
; data - its bit 5 resets the CPU on a 0->1 edge [NEAT p30].
; AX = 64h's value : 62h's value; cs_neat60 = 60h's.
cs_neat60:  db 0
cs_neatrd:                          ; AL = index -> AL = its value
    out 0x22, al
    in al, 0x23
    ret
cs_id_neat:
    push bx
    pushf
    cli
    mov al, 0x60
    call cs_neatrd
    mov [cs_neat60], al
    mov al, 0x62
    call cs_neatrd
    mov bh, al
    mov al, 0x64
    call cs_neatrd
    mov bl, al
    popf
    mov ax, bx                      ; AH = 62h, AL = 64h
    test al, 0x80
    jnz .no                         ; not an 82C212 (or a floating bus)
    cmp al, ah
    jne .yes
    cmp al, [cs_neat60]
    jne .yes
.no:
    pop bx
    stc
    ret
.yes:
    pop bx
    clc
    ret
%endif
