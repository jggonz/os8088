; =============================================================================
; apps/redline/csweep.inc - THE VERIFIED SWEEP, for every row that can write
; (SPEC.md 103.6.3)
;
; REDLINE's alone (RLAPPLY never sweeps). A row is a DESCRIPTOR - its book
; value, its floor, its step, its knob, a proc that sets a value and one that
; reads it back (0 for a write-only chipset) - so the FE3600 and the NEAT run
; the same code, and a third row is a descriptor rather than a sweep.
;
; cs_sw_try sets a candidate only after reading what it can reach back right
; at it, with interrupts off from the references to the verdict: an
; interrupt could run a card's own BIOS code out of its ROM at whatever speed
; is set. Taken at HOME - the setting the board was running when Tune was
; pressed, which it demonstrably runs - then compared at the candidate. Not
; at the book's value: the book's 10 is FASTER than the Victory's BIOS's 14,
; and references read at a setting the board cannot run are wrong in the
; same way the candidate is, so they agree with it and nothing is caught:
;   - 512 bytes of an option ROM on the 8-bit bus and of the screen, four
;     times over (cs_sw_refs names them);
;   - a PicoMEM's disk buffer, a pattern written at the book's value and read
;     at the candidate, another written at the candidate and read at the
;     book's, and its 2A3h counter ramp;
;   - the floppy controller's main status (3F4h), when it read the same
;     sixteen times at the book's value: the device on the board most likely
;     to fail first, and its read has no side effect;
;   - an I/O WRITE, read back: DMA page register 85h, which no channel uses
;     on an AT, takes four patterns at the candidate and is put back as it
;     was. A kept setting applies to every 8-bit WRITE after it, and writes
;     are where a too-short cycle does harm, so the sweep tries one;
;   - the knob itself, read back, on a chipset that can (the NEAT).
; Any wrong byte puts HOME back before interrupts return. A
; candidate that passed is TIMED where it stands (cs_sw_measure, the
; includer's), and every step is LOGGED with what failed, for the report:
; one field run is meant to carry everything the next design needs.
; =============================================================================

CSD_BOOK    equ 0                   ; db the data book's value (Mfg spec)
CSD_FLOOR   equ 1                   ; db never below
CSD_STEP    equ 2                   ; db one bus clock, in the knob's units
CSD_KNOB    equ 3                   ; db REDLINE.CFG's knob number
CSD_SET     equ 4                   ; dw proc: AL = value, every reg kept
CSD_GET     equ 6                   ; dw proc: AL = value read, or 0
CSD_PRE     equ 8                   ; dw the value's words, before it ('R4 ')
CSD_SUF     equ 10                  ; dw ...and after it (' waits')
CSD_VER     equ 12                  ; db CSS_VERIFIED, or CSS_DATASHEET: this
                                    ; KNOB's own status, which a row's newer
                                    ; knobs do not share with its first

CS_SWMAX    equ 12                  ; steps logged, every knob's
CSL_DESC    equ 0                   ; dw the knob's descriptor
CSL_VAL     equ 2                   ; db the candidate
CSL_WHY     equ 3                   ; db what went wrong (CSW_*), 0 = passed
CSL_BAD     equ 4                   ; dw wrong reads
CSL_HOME    equ 6                   ; db where that knob's sweep started
CSL_NS      equ 8                   ; 4 dw: screen, PicoMEM, ROM, I/O (ns)
CSL_SIZE    equ 16
CSW_SCR     equ 1                   ; the screen read wrong
CSW_FDC     equ 2                   ; the floppy status changed
CSW_PAGE    equ 4                   ; the I/O write did not read back
CSW_RB      equ 8                   ; the knob did not read back
CSW_ROM     equ 16                  ; the option ROM read wrong
CSW_PM      equ 32                  ; a PicoMEM pattern read wrong
CSW_RAMP    equ 64                  ; the PicoMEM's 2A3h count skipped
                                    ; (one flag a reference: the Victory's
                                    ; first report said "memory" and could
                                    ; not say which of three it was)
CS_SW_PBUF  equ 0x5000              ; a PicoMEM's disk transfer buffer
CS_SW_PAGE  equ 0x85                ; an unused DMA page register

; A row's knobs, swept in this order: each starts from where the knobs
; before it were left
cs_klist_fe: dw cs_desc_fe, 0
cs_klist_ne: dw cs_desc_ne, cs_desc_nec8, cs_desc_neio, cs_desc_ne16, 0

cs_desc_fe: db CS_FE_BOOK, CS_FE_FLOOR, 2, CS_FE_R4
            dw cs_fe_set, 0, cs_k_r4, cs_k_none
            db CSS_VERIFIED
cs_desc_ne: db CS_NE_BOOK, CS_NE_FLOOR, 1, CS_NE_W8
            dw cs_ne_set, cs_ne_getw8, cs_k_8b, cs_k_waits
            db CSS_VERIFIED         ; the PB286LP, 2026-10-14
cs_desc_nec8: db 1, 0, 1, CS_NE_C8
            dw cs_ne_setc8, cs_ne_getc8, cs_k_8bd, cs_k_none
            db CSS_DATASHEET
cs_desc_neio: db 1, 0, 1, CS_NE_CIO
            dw cs_ne_setcio, cs_ne_getcio, cs_k_iod, cs_k_none
            db CSS_DATASHEET
cs_desc_ne16: db 1, 0, 1, CS_NE_W16
            dw cs_ne_setw16, cs_ne_getw16, cs_k_16b, cs_k_waits
            db CSS_DATASHEET
cs_ne_getw8:
    mov al, CS_NE_W8
    jmp cs_ne_kget
cs_k_r4:    db 'R4 '
cs_k_none:  db 0
cs_k_waits: db ' waits', 0
cs_k_8b:    db '8-bit ', 0
cs_k_16b:   db '16-bit ', 0
cs_k_8bd:   db '8-bit delay ', 0
cs_k_iod:   db 'I/O delay ', 0

cs_sw_desc: dw 0
cs_sw_home: db 0                    ; the setting the sweep started from
cs_sw_rseg: dw 0
cs_sw_sseg: dw 0xA000
cs_sw_pseg: dw 0
cs_sw_cand: db 0
cs_sw_why:  db 0
cs_sw_bad:  dw 0
cs_sw_fail: db 0                    ; the first candidate that failed, or 0
cs_sw_fdc:  db 0                    ; 3F4h at the book's value; 0FFh unusable
cs_sw_pg:   db 0                    ; 1: 85h reads back at the book's value
cs_sw_pg0:  db 0                    ; ...and what it held
cs_sw_n:    db 0                    ; steps logged
cs_sw_ns:   times 4 dw 0            ; cs_sw_measure's answer
cs_sw_log:  times CS_SWMAX * CSL_SIZE db 0

; cs_sw_refs - AX = the screen's segment, BX = a PicoMEM's or 0, CX = an
; option ROM's on the 8-bit bus or 0 (THE CALLER CHOOSES: a shadowed ROM is
; RAM and tests nothing - chipset.inc, cs_romref)
cs_sw_refs:
    mov [cs_sw_sseg], ax
    mov [cs_sw_pseg], bx
    mov [cs_sw_rseg], cx
    ret

; cs_sw_set - AL = a value, through the descriptor. Every register kept
cs_sw_set:
    push bx
    mov bx, [cs_sw_desc]
    call [bx + CSD_SET]
    pop bx
    ret

; cs_sw_grab - AX = a segment: its first 512 bytes into ES:DI (DS's)
cs_sw_grab:
    push cx
    push si
    push ds
    mov ds, ax
    xor si, si
    mov cx, 256
    rep movsw
    pop ds
    pop si
    pop cx
    ret

; cs_sw_cmp - AX = a segment, SI = its reference here, DL = its CSW_ flag:
; wrong bytes counted
cs_sw_cmp:
    push ax
    push cx
    push si
    push di
    push es
    mov es, ax
    xor di, di
    mov cx, 512
.c:
    lodsb
    cmp al, [es:di]
    je .ok
    inc word [cs_sw_bad]
    or [cs_sw_why], dl
.ok:
    inc di
    loop .c
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; cs_sw_pat - ES:DI = 512 bytes, AL = the seed, AH = the step: write it
cs_sw_pat:
    push cx
    push di
    mov cx, 512
.w:
    stosb
    add al, ah
    loop .w
    pop di
    pop cx
    ret

; cs_sw_chk - ES:DI = 512 bytes, AL = the seed, AH = the step: count misses
cs_sw_chk:
    push cx
    push di
    mov cx, 512
.r:
    cmp [es:di], al
    je .ok
    inc word [cs_sw_bad]
    or byte [cs_sw_why], CSW_PM
.ok:
    inc di
    add al, ah
    loop .r
    pop di
    pop cx
    ret

; cs_sw_io - the two I/O checks at the current setting: 3F4h against its
; reference, sixteen reads; 85h written four ways and read back
cs_sw_io:
    push ax
    push cx
    push dx
    push si
    cmp byte [cs_sw_fdc], 0FFh
    je .page
    mov dx, 0x3F4
    mov cx, 16
.fdc:
    in al, dx
    cmp al, [cs_sw_fdc]
    je .fok
    inc word [cs_sw_bad]
    or byte [cs_sw_why], CSW_FDC
.fok:
    loop .fdc
.page:
    cmp byte [cs_sw_pg], 0
    je .out
    mov si, cs_sw_pats
    mov cx, 4
.pat:
    mov al, [si]
    out CS_SW_PAGE, al
    in al, CS_SW_PAGE
    cmp al, [si]
    je .pok
    inc word [cs_sw_bad]
    or byte [cs_sw_why], CSW_PAGE
.pok:
    inc si
    loop .pat
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret
cs_sw_pats: db 0x55, 0xAA, 0x0F, 0xF0

; -----------------------------------------------------------------------------
; cs_sw_try - AL = the candidate. CF = 0: every reference read right, it is
; SET, timed (cs_sw_ns) and logged; CF = 1: [cs_sw_bad] wrong reads and
; [cs_sw_why] what they were, and HOME is back. Every other
; register preserved.
; -----------------------------------------------------------------------------
cs_sw_try:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [cs_sw_cand], al
    mov word [cs_sw_bad], 0
    mov byte [cs_sw_why], 0
    mov si, cs_sw_ns                ; untimed until it passes
    mov cx, 4
.clr:
    mov word [si], 0xFFFF
    add si, 2
    loop .clr
    pushf
    cli
    cld
    mov al, [cs_sw_home]            ; the references, at home
    call cs_sw_set
    push ds
    pop es
    mov di, cs_sw_ref
    mov ax, [cs_sw_sseg]
    call cs_sw_grab                 ; the screen, cs_sw_ref + 0
    mov ax, [cs_sw_rseg]
    or ax, ax
    jz .noref
    call cs_sw_grab                 ; the ROM, cs_sw_ref + 512
.noref:
    mov ax, [cs_sw_pseg]            ; pattern A into the card, at the book's
    or ax, ax
    jz .fdcref
    mov es, ax
    mov di, CS_SW_PBUF
    mov al, [cs_sw_cand]
    mov ah, 7
    call cs_sw_pat
.fdcref:
    mov dx, 0x3F4                   ; 3F4h: sixteen reads, all alike, or
    in al, dx                       ; it is not a reference
    mov ah, al
    mov cx, 15
.fr:
    in al, dx
    cmp al, ah
    jne .fdcno
    loop .fr
    mov [cs_sw_fdc], ah
    jmp short .pgref
.fdcno:
    mov byte [cs_sw_fdc], 0FFh
.pgref:
    in al, CS_SW_PAGE               ; 85h: kept, and it must read back here
    mov [cs_sw_pg0], al
    mov byte [cs_sw_pg], 0
    mov al, 0x55
    out CS_SW_PAGE, al
    in al, CS_SW_PAGE
    cmp al, 0x55
    jne .pgdone
    mov al, 0xAA
    out CS_SW_PAGE, al
    in al, CS_SW_PAGE
    cmp al, 0xAA
    jne .pgdone
    mov byte [cs_sw_pg], 1
.pgdone:
    mov al, [cs_sw_pg0]
    out CS_SW_PAGE, al
    mov al, [cs_sw_cand]            ; the candidate
    call cs_sw_set
    mov bx, [cs_sw_desc]            ; ...read back, where the chip can say
    mov bx, [bx + CSD_GET]
    or bx, bx
    jz .pass0
    call bx
    cmp al, [cs_sw_cand]
    je .pass0
    inc word [cs_sw_bad]
    or byte [cs_sw_why], CSW_RB
.pass0:
    mov bx, 4                       ; every reference, four times over
.pass:
    mov ax, [cs_sw_sseg]
    mov si, cs_sw_ref
    mov dl, CSW_SCR
    call cs_sw_cmp
    mov ax, [cs_sw_rseg]
    or ax, ax
    jz .nor
    mov si, cs_sw_ref + 512
    mov dl, CSW_ROM
    call cs_sw_cmp
.nor:
    call cs_sw_io
    mov ax, [cs_sw_pseg]
    or ax, ax
    jz .next
    mov es, ax
    mov di, CS_SW_PBUF
    mov al, [cs_sw_cand]
    mov ah, 7
    call cs_sw_chk                  ; pattern A back
    mov dx, 0x2A3                   ; the card's counter: one more a read
    in al, dx
    mov ah, al
    mov cx, 255
.ramp:
    in al, dx
    inc ah
    cmp al, ah
    je .rok
    inc word [cs_sw_bad]
    or byte [cs_sw_why], CSW_RAMP
    mov ah, al
.rok:
    loop .ramp
.next:
    dec bx
    jnz .pass
    mov ax, [cs_sw_pseg]            ; pattern B written AT the candidate...
    or ax, ax
    jz .timed
    mov es, ax
    mov di, CS_SW_PBUF + 512
    mov al, [cs_sw_cand]
    not al
    mov ah, 13
    call cs_sw_pat
    mov al, [cs_sw_home]            ; ...and read back at home
    call cs_sw_set
    mov al, [cs_sw_cand]
    not al
    mov ah, 13
    call cs_sw_chk
    mov al, [cs_sw_cand]            ; the candidate again, to be timed
    call cs_sw_set
.timed:
    cmp word [cs_sw_bad], 0
    jne .verdict
    call cs_sw_measure              ; where it stands, interrupts still off
.verdict:
    mov al, [cs_sw_home]
    cmp word [cs_sw_bad], 0
    jne .set
    mov al, [cs_sw_cand]
.set:
    call cs_sw_set                  ; BEFORE interrupts come back
    cmp byte [cs_sw_pg], 0          ; 85h as it was
    je .pgk
    mov al, [cs_sw_pg0]
    out CS_SW_PAGE, al
.pgk:
    popf
    call cs_sw_logit
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp word [cs_sw_bad], 1         ; CF = 1 when bad < 1, so flip it:
    cmc                             ; CF = 1 when there was a wrong read
    ret

; cs_sw_logit - this step into cs_sw_log, while there is room
cs_sw_logit:
    push ax
    push cx
    push si
    push di
    push es
    mov al, [cs_sw_n]
    cmp al, CS_SWMAX
    jae .out
    inc byte [cs_sw_n]
    mov ah, CSL_SIZE
    mul ah
    add ax, cs_sw_log
    mov di, ax
    push ds
    pop es
    cld
    mov ax, [cs_sw_desc]
    stosw
    mov al, [cs_sw_cand]
    stosb
    mov al, [cs_sw_why]
    stosb
    mov ax, [cs_sw_bad]
    stosw
    mov al, [cs_sw_home]
    stosb
    inc di
    mov si, cs_sw_ns
    mov cx, 4
    rep movsw
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_sw_sweep - BX = a knob's descriptor, AL = HOME (what the board runs
; now: read off a chipset that can say, measured off one that cannot). The
; book's value, then one bus clock less at a time to the floor, stopping at
; the first wrong read. out AL = the fastest that passed - HOME when not even
; the book's did - and it is SET; [cs_sw_fail] = the first that failed or
; 0; every step tried appended to cs_sw_log.
; -----------------------------------------------------------------------------
cs_sw_sweep:
    push bx
    push cx
    mov [cs_sw_desc], bx
    mov [cs_sw_home], al
    mov byte [cs_sw_fail], 0
    mov cl, al                      ; CL = the last that passed: home
    mov al, [bx + CSD_BOOK]
.step:
    call cs_sw_try
    jc .bad
    mov cl, al
    sub al, [bx + CSD_STEP]
    jc .done
    cmp al, [bx + CSD_FLOOR]
    jae .step
    jmp short .done
.bad:
    mov [cs_sw_fail], al
    mov al, cl
    call cs_sw_set                  ; the last that passed
.done:
    mov al, cl
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cs_sw_tune - SI = a row's knob list (cs_klist_*), AL = HOME for a knob
; that cannot be read back (the FE3600's R4, measured by the caller). Every
; knob in turn, each from where it reads now; the log starts empty. out AL =
; the FIRST knob's fastest. Every other register preserved
; -----------------------------------------------------------------------------
cs_sw_tune:
    push bx
    push cx
    push dx
    push si
    mov byte [cs_sw_n], 0
    mov dl, al
    xor cx, cx                      ; CL = the first knob's answer, CH = had it
.k:
    mov bx, [si]
    or bx, bx
    jz .out
    mov al, dl
    mov ax, [bx + CSD_GET]
    or ax, ax
    mov al, dl
    jz .home
    call [bx + CSD_GET]             ; its own answer, in AL
.home:
    call cs_sw_sweep
    or ch, ch
    jnz .next
    mov cl, al
    inc ch
.next:
    add si, 2
    jmp short .k
.out:
    mov al, cl
    pop si
    pop dx
    pop cx
    pop bx
    ret
