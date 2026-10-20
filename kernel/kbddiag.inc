; =============================================================================
; kbddiag.inc - KBDDIAG=1: what the KEYBOARD sent, and what the BIOS made of
;               it, inside an fsx bracket and out of one (SPEC.md 9.8.1)
;
; docs/FIELD-NOTES.md 63: a Pentium 4 with a USB keyboard on a USB->PS/2
; converter types on the desktop - Space even RESUMES MIDIRack - and cannot
; PAUSE MIDIRack while it plays through the speaker. That play is an FSXF_RATE
; bracket (SPEC.md 34.11, 105.8.3) and its key path is the BIOS's own int 16h
; polled from mrk_input, exactly the desktop's - what differs is that IRQ0
; runs at the SAMPLE RATE and every keystroke's int 09h runs among thousands
; of nested pulses a second. Which of the three links broke is the question,
; and each leaves a different picture here:
;
;   no 'I' rows between 'E' and 'L'   IRQ1 never arrived. The 'S' rows say
;                                     why: OBF set with IRR1/ISR1/IMR1
;   'I' rows, tail never moves        the ROM took the byte and stored nothing
;   tail moves, 'L' row head != tail  stored, and int 16h never handed it over
;
; It is a KNOB for moudiag.inc's reason: it DRAWS, because the machine it is
; about has no debugger. Recording is IF = 0 throughout and reads only what
; is safe to read twice - port 64h (status, no side effect) before the ROM's
; handler and port 60h AFTER it, when the 8042 has already handed the byte
; over and a re-read returns the same byte and consumes nothing. So the ROM
; sees exactly the hardware state it sees on a plain kernel; the one thing
; that changes is that kbm_isr CALLS the ROM rather than jumping to it.
;
; Drawn with font_run (SPEC.md 6.6): every line is padded to KBG_W cells, so
; the padding is the erase and nothing is drawn twice.
; =============================================================================
%ifdef KBD_DIAG
%ifdef MOU_DIAG
  %error "KBDDIAG=1 and MOUDIAG=1 both draw at the desktop's top-left - build one"
%endif
section .text

KBG_X       equ 8               ; byte-aligned: font_run's fast path
KBG_Y       equ 28              ; below the menu bar (MBAR_H 20)
KBG_ROW     equ 10              ; 8px cell + 2
KBG_W       equ 56              ; cells a line - 448px, inside 640 and 720
KBG_RATE    equ 9               ; ticks between redraws, ~0.5 s
KBG_REC     equ 16              ; bytes a record
KBG_N       equ 64              ; records in the ring (a power of two)
KBG_SHOWMAX equ 20              ; rows of it on a tall screen

; a record - every field is filled for every kind, so one line format serves
KR_KIND     equ 0               ; 'I' IRQ1, 'S' an IRQ0 sample in a bracket,
                                ; 'E' a bracket entered, 'L' one leaving
KR_FLG      equ 1               ; bit 0 an fsx bracket is armed, bit 1 the
                                ; sub-tick / FSXF_RATE is on ([sch_fast])
KR_TICK     equ 2               ; [ticks]
KR_ST       equ 4               ; port 64h at the record (FF on an 8088 tier)
KR_ST2      equ 5               ; 'I': port 64h after the ROM's handler
KR_DAT      equ 6               ; 'I': port 60h after the ROM's handler
KR_HD       equ 7               ; BDA head (0040:001A), low byte
KR_TL       equ 8               ; BDA tail (0040:001C), low byte, at the record
KR_TL2      equ 9               ; 'I': the tail after the ROM's handler
KR_KEY      equ 10              ; 'I': the word the ROM stored, if it stored
KR_ISR      equ 12              ; PIC1 in-service
KR_IRR      equ 13              ; PIC1 request
KR_IMR      equ 14              ; PIC1 mask
KR_X        equ 15              ; 'I': bit 0 [kbd_armed], bit 1 a mouse has
                                ; spoken, bit 2 a PS/2 aux port; 'E': the
                                ; bracket's CX flags

; -----------------------------------------------------------------------------
; kbg_st - AL = port 64h, or FF on an 8088-tier machine, whose port 64h is no
; status register (an XT's PPI aliases port A there)
; -----------------------------------------------------------------------------
kbg_st:
    mov al, 0xFF
    cmp byte [cpu_tier], CPU_8086
    je .x
    in al, 0x64
.x:
    ret

; -----------------------------------------------------------------------------
; kbg_new - AL = kind, IF = 0: BX = a fresh ring slot, common fields filled.
; Preserves every other register
; -----------------------------------------------------------------------------
kbg_new:
    push ax
    push cx
    push es
    mov bx, [kbg_wp]
    push bx
    add bx, KBG_REC
    and bx, KBG_N * KBG_REC - 1
    mov [kbg_wp], bx
    pop bx
    add bx, kbg_ring
    inc word [kbg_cnt]
    mov cx, KBG_REC / 2
    push bx
.z:
    mov word [bx], 0
    inc bx
    inc bx
    loop .z
    pop bx
    mov [bx+KR_KIND], al
    xor al, al
    cmp byte [fsx_task], 0xFF
    je .f1
    or al, 1
.f1:
    cmp byte [sch_fast], 0
    je .f2
    or al, 2
.f2:
    mov [bx+KR_FLG], al
    mov ax, [ticks]
    mov [bx+KR_TICK], ax
    call kbg_st
    mov [bx+KR_ST], al
    mov [bx+KR_ST2], al
    mov al, 0x0B                ; OCW3: read the IN-SERVICE register...
    out 0x20, al
    in al, 0x20
    mov [bx+KR_ISR], al
    mov al, 0x0A                ; ...and select the IRR again, which is where
    out 0x20, al                ; the PIC is left everywhere else
    in al, 0x20
    mov [bx+KR_IRR], al
    in al, 0x21
    mov [bx+KR_IMR], al
    xor ax, ax
    mov es, ax
    mov al, [es:0x41A]
    mov [bx+KR_HD], al
    mov al, [es:0x41C]
    mov [bx+KR_TL], al
    mov [bx+KR_TL2], al
    pop es
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbg_pre - kbm_isr's head, DS = KERNEL_SEG, IF = 0. Every register preserved
; -----------------------------------------------------------------------------
kbg_pre:
    push ax
    push bx
    mov al, 'I'
    call kbg_new
    mov [kbg_cur], bx
    mov al, [kbd_armed]
    and al, 1
    cmp byte [mou_ptr], 0
    je .np
    or al, 2
.np:
%ifdef KERN_BIG
    cmp byte [mou_p2], 0
    je .na
    or al, 4
.na:
%endif
    mov [bx+KR_X], al
    inc word [kbg_n1]
    test byte [bx+KR_FLG], 1
    jz .out
    inc word [kbg_n1b]
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbg_post - after the ROM's int 09h returned to kbm_isr, DS = KERNEL_SEG,
; IF = 0. Every register preserved
; -----------------------------------------------------------------------------
kbg_post:
    push ax
    push bx
    push si
    push es
    mov bx, [kbg_cur]
    in al, 0x60                 ; the byte the ROM just took: a re-read
    mov [bx+KR_DAT], al
    call kbg_st
    mov [bx+KR_ST2], al
    xor ax, ax
    mov es, ax
    mov al, [es:0x41C]
    mov [bx+KR_TL2], al
    cmp al, [bx+KR_TL]
    je .out                     ; nothing stored
    mov al, [bx+KR_TL]          ; the slot the ROM wrote: 0040:old tail
    xor ah, ah
    mov si, ax
    mov ax, [es:si+0x400]
    mov [bx+KR_KEY], ax
    inc word [kbg_enq]
    test byte [bx+KR_FLG], 1
    jz .out
    inc word [kbg_enqb]
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbg_irq0 - sch_isr's head, DS = KERNEL_SEG, IF = 0: inside a bracket, count
; what the keyboard's hardware looks like each tick, and record a row when
; that changes. Outside one, nothing. Every register preserved
; -----------------------------------------------------------------------------
kbg_irq0:
    cmp byte [fsx_task], 0xFF
    je .ret
    push ax
    push bx
    inc word [kbg_ns]
    call kbg_st
    mov bl, al
    and bl, 0x21                ; OBF (bit 0) and the AUX flag (bit 5)
    test al, 1
    jz .o
    cmp al, 0xFF
    je .o
    inc word [kbg_tobf]
.o:
    mov al, 0x0B
    out 0x20, al
    in al, 0x20                 ; ISR
    test al, 2
    jz .i
    inc word [kbg_tisr]
    or bl, 0x02
.i:
    mov al, 0x0A
    out 0x20, al
    in al, 0x20                 ; IRR
    test al, 2
    jz .r
    inc word [kbg_tirr]
    or bl, 0x04
.r:
    in al, 0x21                 ; IMR
    test al, 2
    jz .m
    inc word [kbg_tmsk]
    or bl, 0x08
.m:
    cmp bl, [kbg_last]
    je .out
    mov [kbg_last], bl
    mov al, 'S'
    call kbg_new
.out:
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; kbg_mark - AL = 'E' or 'L', AH = KR_X: a bracket's edge. Task context.
; Every register preserved
; -----------------------------------------------------------------------------
kbg_mark:
    pushf
    cli
    push bx
    cmp al, 'E'
    jne .n
    inc word [kbg_nbrk]
    mov byte [kbg_last], 0xFF   ; the first sample in it always records
.n:
    call kbg_new
    mov [bx+KR_X], ah
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; kbg_tick - ui_task's pass: the panel every KBG_RATE ticks
; -----------------------------------------------------------------------------
kbg_tick:
    push ax
    mov ax, [ticks]
    sub ax, [kbg_dlast]
    cmp ax, KBG_RATE
    jb .out
    mov ax, [ticks]
    mov [kbg_dlast], ax
    call kbg_show
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbg_show - the panel. Every register preserved
; -----------------------------------------------------------------------------
kbg_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call gfx_lock
    mov bp, KBG_Y

    mov si, kbg_t1
    call kbg_tpl
    mov ax, [kbg_n1]
    mov di, kbg_buf + 13
    call kbg_h16
    mov ax, [kbg_n1b]
    mov di, kbg_buf + 18
    call kbg_h16
    mov ax, [kbg_enq]
    mov di, kbg_buf + 28
    call kbg_h16
    mov ax, [kbg_enqb]
    mov di, kbg_buf + 33
    call kbg_h16
    mov ax, [kbg_nbrk]
    mov di, kbg_buf + 43
    call kbg_h16
    call kbg_line

    mov si, kbg_t2
    call kbg_tpl
    mov ax, [kbg_ns]
    mov di, kbg_buf + 13
    call kbg_h16
    mov ax, [kbg_tobf]
    mov di, kbg_buf + 22
    call kbg_h16
    mov ax, [kbg_tirr]
    mov di, kbg_buf + 32
    call kbg_h16
    mov ax, [kbg_tisr]
    mov di, kbg_buf + 42
    call kbg_h16
    mov ax, [kbg_tmsk]
    mov di, kbg_buf + 52
    call kbg_h16
    call kbg_line

    mov si, kbg_t3
    call kbg_tpl
    call kbg_line

    ; --- the newest rows of the ring, oldest first. How many fit is the
    ; screen's question: a 200-line CGA takes ten, a VGA or Hercules twenty
    mov ax, [vid_h]
    sub ax, KBG_Y + 3 * KBG_ROW + 40
    mov cl, KBG_ROW
    div cl
    xor ah, ah
    cmp ax, KBG_SHOWMAX
    jbe .nmax
    mov ax, KBG_SHOWMAX
.nmax:
    mov cx, ax                  ; CX = rows to draw
    mov dx, cx                  ; DX = how far back the first one is
.row:
    mov si, kbg_t4
    call kbg_tpl
    cmp dx, [kbg_cnt]
    ja .blank                   ; the ring has not got this many yet
    mov bx, dx
    push cx
    mov cl, 4
    shl bx, cl                  ; BX = records back * KBG_REC
    pop cx
    neg bx
    add bx, [kbg_wp]
    and bx, KBG_N * KBG_REC - 1
    add bx, kbg_ring
    pushf
    cli                         ; one record, read whole
    push cx
    push ds
    pop es
    mov si, bx
    mov di, kbg_rec
    mov cx, KBG_REC / 2
    cld
    rep movsw
    pop cx
    popf
    mov al, [kbg_rec+KR_KIND]
    mov [kbg_buf], al
    mov ax, [kbg_rec+KR_TICK]
    mov di, kbg_buf + 2
    call kbg_h16
    mov al, [kbg_rec+KR_FLG]
    mov di, kbg_buf + 7
    call kbg_h1
    mov al, [kbg_rec+KR_ST]
    mov di, kbg_buf + 9
    call kbg_h8
    mov al, [kbg_rec+KR_ST2]
    mov di, kbg_buf + 12
    call kbg_h8
    mov al, [kbg_rec+KR_DAT]
    mov di, kbg_buf + 15
    call kbg_h8
    mov al, [kbg_rec+KR_HD]
    mov di, kbg_buf + 18
    call kbg_h8
    mov al, [kbg_rec+KR_TL]
    mov di, kbg_buf + 21
    call kbg_h8
    mov al, [kbg_rec+KR_TL2]
    mov di, kbg_buf + 24
    call kbg_h8
    mov ax, [kbg_rec+KR_KEY]
    mov di, kbg_buf + 27
    call kbg_h16
    mov al, [kbg_rec+KR_ISR]
    mov di, kbg_buf + 32
    call kbg_h8
    mov al, [kbg_rec+KR_IRR]
    mov di, kbg_buf + 35
    call kbg_h8
    mov al, [kbg_rec+KR_IMR]
    mov di, kbg_buf + 38
    call kbg_h8
    mov al, [kbg_rec+KR_X]
    mov di, kbg_buf + 41
    call kbg_h8
    jmp short .draw
.blank:
    mov si, kbg_t5
    call kbg_tpl
.draw:
    call kbg_line
    dec dx
    dec cx
    jz .done
    jmp .row
.done:

    call gfx_unlock
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; kbg_tpl - DS:SI template -> kbg_buf, space-padded to KBG_W, NUL-ended
kbg_tpl:
    push ax
    push cx
    push si
    push di
    mov di, kbg_buf
    mov cx, KBG_W
.copy:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    mov [di], al
    inc di
    loop .copy
    jmp short .end
.pad:
    mov al, ' '
.pz:
    mov [di], al
    inc di
    loop .pz
.end:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; kbg_line - kbg_buf at KBG_X, BP, and BP a row on
kbg_line:
    push ax
    push cx
    push dx
    push si
    mov si, kbg_buf
    mov cx, KBG_X
    mov dx, bp
    mov ax, (CBLACK << 8) | CWHITE
    call font_run
    add bp, KBG_ROW
    pop si
    pop dx
    pop cx
    pop ax
    ret

; kbg_h16 / kbg_h8 / kbg_h1 - AX / AL / AL's low nibble as hex at [di]
kbg_h16:
    push ax
    mov al, ah
    call kbg_h8
    pop ax
    add di, 2
    call kbg_h8
    sub di, 2
    ret

kbg_h8:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call kbg_h1
    pop cx
    pop ax
    push ax
    inc di
    call kbg_h1
    dec di
    pop ax
    ret

kbg_h1:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .store
    add al, 7
.store:
    mov [di], al
    pop ax
    ret

;            0         1         2         3         4         5
;            0123456789012345678901234567890123456789012345678901234567
kbg_t1: db 'KBDDIAG irq1 ..../....  enq ..../....  fsx ....', 0
kbg_t2: db 'fsx-ticks    .... obf .... irr1 .... isr1 .... msk1 ....', 0
kbg_t3: db 'c tick f st>st 60 hd tl>tl key  is ir im x', 0
kbg_t4: db '? .... . ..>.. .. .. ..>.. .... .. .. .. ..', 0
kbg_t5: db '-', 0

; .text and INITIALISED, not .bss: .bss is not zeroed (SPEC.md 12.8.5.1) and
; a knob kernel's budget is not billed (docs/KERNEL-MEMORY.md), so the zeroes
; are cheaper than an init routine and a call to it
kbg_ring:   times KBG_N * KBG_REC db 0
kbg_rec:    times KBG_REC db 0
kbg_buf:    times KBG_W + 1 db 0
kbg_wp:     dw 0                ; the next slot's offset into kbg_ring
kbg_cnt:    dw 0                ; records ever written
kbg_cur:    dw kbg_ring         ; the 'I' record kbg_post completes
kbg_n1:     dw 0                ; IRQ1s
kbg_n1b:    dw 0                ; ...inside a bracket
kbg_enq:    dw 0                ; IRQ1s after which the BIOS tail had moved
kbg_enqb:   dw 0                ; ...inside a bracket
kbg_nbrk:   dw 0                ; brackets entered
kbg_ns:     dw 0                ; IRQ0 samples inside a bracket
kbg_tobf:   dw 0                ; ...with 64h OBF set
kbg_tirr:   dw 0                ; ...with IRQ1 requested
kbg_tisr:   dw 0                ; ...with IRQ1 in service
kbg_tmsk:   dw 0                ; ...with IRQ1 masked
kbg_dlast:  dw 0                ; [ticks] at the last redraw
kbg_last:   db 0xFF             ; the last sample's packed state
%endif
