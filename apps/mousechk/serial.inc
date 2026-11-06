; =============================================================================
; os8088 - apps/mousechk/serial.inc - the serial mice section (SPEC.md 126.6)
;
; What the kernel's boot probe and identify window (SPEC.md 9.4, 9.5) made of
; COM1 and COM2, the contest's outcome, and what each UART says RIGHT NOW. All
; of it is read: the kernel's 'MO' block through mc_dbgfind (offsets in
; moblk.inc, decoded only while [mc_kok]), the BIOS's COM table through an
; OSAPI_SEG window over 0040:0000, and the UART's own MSR, MCR, LSR and IIR.
; It NEVER reads a UART's receive register (that would take a byte from the
; kernel's ISR) and never writes any UART register. Every datum it keeps is a
; static in the image: this file owns no bss.
;
; ES = KERNEL_SEG on entry (the row API's contract) and is the kernel's
; throughout, but for the one push/pop around the BDA read.
; =============================================================================

%macro SROW 1
    mov si, %1
    call mc_row
%endmacro
%macro SPUT 1
    mov si, %1
    call mc_put
%endmacro
%macro SLINE 1
    mov si, %1
    call mc_line
%endmacro

SER_P0BASE  equ 0x3F8
SER_P2BASE  equ 0x2F8
SER_IDMAX   equ 128             ; kernel/mouse.inc MOU_IDMAX
SER_L4      equ 0x10            ; the 8259 bits a mouse's line can be
SER_L3      equ 0x08

; -----------------------------------------------------------------------------
; ser_init - once at open: a window over the BIOS data area
; keeps every register
; -----------------------------------------------------------------------------
ser_init:
    push ax
    push bx
    push cx
    push dx
    xor ax, ax
    mov cx, 16
    mov dx, 0x0040                  ; the paragraph; a SELECTOR over it in
    mov bl, SEG_WIN                 ; protected mode (OSAPI_SEG), the
    call OSAPI_SEG                  ; paragraph itself on a real-mode kernel
    jnc .ok
    xor dx, dx
.ok:
    mov [ser_bda], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ser_nth - in SI = a table of NUL-terminated strings, AL = n; out SI = the nth
; -----------------------------------------------------------------------------
ser_nth:
    push ax
    push cx
    mov cl, al
    xor ch, ch
    jcxz .out
.s:
    lodsb
    or al, al
    jnz .s
    loop .s
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ser_flags - append the names of the set bits: in DL = the byte, its tested
; bits shifted up to bit 7, CX = how many, SI = a table of that many names, the
; first for bit 7. Appends "-" if none is set. Keeps every register
; -----------------------------------------------------------------------------
ser_flags:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bh, bh
.l:
    shl dl, 1
    jnc .skip
    call mc_put
    mov al, ' '
    call mc_putc
    mov bh, 1
.skip:
.a:
    lodsb
    or al, al
    jnz .a
    loop .l
    or bh, bh
    jnz .out
    mov al, '-'
    call mc_putc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ser_yn - append "yes" for AL <> 0, else "no"
; -----------------------------------------------------------------------------
ser_yn:
    push si
    mov si, ser_s_no
    or al, al
    jz .p
    mov si, ser_s_yes
.p:
    call mc_put
    pop si
    ret

; -----------------------------------------------------------------------------
; ser_secs - append "N ticks (~S s)" and then the NUL string at SI, for AX = a
; count of [ticks] (18.2 Hz). Keeps every register
; -----------------------------------------------------------------------------
ser_secs:
    push ax
    push cx
    push dx
    call mc_dec
    push si
    SPUT ser_s_tk
    xor dx, dx
    mov cx, 18
    div cx
    call mc_dec
    SPUT ser_s_tr
    pop si
    call mc_put
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ser_rows - the whole section; keeps every register
; -----------------------------------------------------------------------------
ser_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ser_s_head
    mov al, LP_NA
    call mc_head
    mov byte [ser_have], 0
    mov ax, DBG_TAG_MOUSE
    call mc_dbgfind
    jc .nomo
    mov ax, [es:bx+MOB_SPAN]
    mov [ser_sp], ax
    mov ax, [es:bx+MOB_BASES]
    mov [ser_bp], ax
    mov al, [mc_kok]
    mov [ser_have], al
    call ser_bios
    cmp byte [ser_have], 0
    je .raw
    mov al, 1                       ; Close: CF=1 (busy) exactly when another
    call OSAPI_SERIAL               ; instance holds a SERIAL.DRV lease - this
    sbb al, al                      ; one never opens one, and with no lease
    mov [ser_lease], al             ; it is two compares (SPEC.md 115.1)
    xor bx, bx
    call ser_port
    mov bx, 2
    call ser_port
    call ser_glob
    jmp .out
.nomo:
    call ser_bios
    SLINE ser_s_nomo
    mov al, LP_INFO
    call mc_lamp
    jmp .out
.raw:                               ; O1: never guess a layout
    SLINE ser_s_raw
    mov al, LP_INFO
    call mc_lamp
    mov di, [ser_sp]
    xor bx, bx
.r:
    test bl, 15                     ; a line of sixteen bytes
    jnz .rb
    SLINE ser_s_nul
.rb:
    mov al, [es:di+bx]
    call mc_hex8
    mov al, ' '
    call mc_putc
    inc bx
    cmp bx, MOS_SIZE
    jb .r
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ser_bios - the BIOS's four COM bases, and what the kernel did with COM1/2
; -----------------------------------------------------------------------------
ser_bios:
    xor bx, bx
.l:
    mov ax, bx
    shr ax, 1
    add al, '1'
    mov [ser_lbl + 3], al
    SROW ser_lbl
    xor ax, ax
    cmp word [ser_bda], 0
    je .none
    push es
    mov es, [ser_bda]
    mov ax, [es:bx]
    pop es
    or ax, ax
    jz .none
    call mc_hex16
    jmp short .k
.none:
    SPUT ser_s_none
.k:
    cmp bx, 4
    jae .np
    cmp byte [ser_have], 0
    je .nx
    mov si, [ser_bp]
    mov al, 1
    cmp word [es:si+bx], 0
    jne .p
    xor al, al
.p:
    SPUT ser_s_kern
    call ser_yn
    SPUT ser_s_uart
    jmp short .nx
.np:
    SPUT ser_s_nopr
.nx:
    add bx, 2
    cmp bx, 8
    jb .l
    ret

; -----------------------------------------------------------------------------
; ser_port - one probed row: in BX = 0 (COM1) or 2 (COM2); ES = kernel
; -----------------------------------------------------------------------------
ser_port:
    SLINE ser_s_com
    mov ax, bx
    shr ax, 1
    add al, '1'
    call mc_putc
    SPUT ser_s_at
    mov ax, SER_P0BASE
    mov dx, SER_L4
    or bx, bx
    jz .h
    mov ax, SER_P2BASE
    mov dx, SER_L3
.h:
    mov [ser_exl], dl               ; the line this base's convention says
    mov [ser_base], ax
    call mc_hex16
    SPUT ser_s_irq
    mov al, 4
    cmp dl, SER_L4
    je .i
    dec al
.i:
    xor ah, ah
    call mc_dec
    mov si, [ser_bp]
    cmp word [es:si+bx], 0
    jne .have
    SPUT ser_s_nouart
    ret
.have:
    mov al, LP_INFO
    call mc_lamp
    mov di, [ser_sp]
    ; --- the boot record ---
    SROW ser_s_idb
    mov al, [es:di+MOS_IDN+bx]
    or al, al
    jnz .got
    SPUT ser_s_nothing
    jmp .ident
.got:
    xor ah, ah
    call mc_dec
    SPUT ser_s_first
    mov al, [es:di+MOS_IDB0+bx]
    call mc_hex8
    cmp al, 0x20
    jb .np
    cmp al, 0x7E
    ja .np
    mov dl, al
    mov al, ' '
    call mc_putc
    mov al, 0x27
    call mc_putc
    mov al, dl
    call mc_putc
    mov al, 0x27
    call mc_putc
.np:
    cmp byte [es:di+MOS_IDN+bx], SER_IDMAX
    jbe .ident
    SPUT ser_s_stream
.ident:
    cmp byte [es:di+MOS_IDN+bx], 0
    je .noid
    SROW ser_s_last                 ; ABSOLUTE: the window's open tick is
    mov ax, [es:di+MOS_IDLAST+bx]   ; kept only by a MOUDIAG kernel
    mov si, ser_s_boot
    call ser_secs
.noid:
    SROW ser_s_shape
    mov al, [es:di+MOS_IDENT+bx]
    call ser_yn
    SROW ser_s_run
    mov al, [es:di+MOS_RUN+bx]
    xor ah, ah
    call mc_dec
    SPUT ser_s_need
    mov al, [es:di+MOS_NEED+bx]
    xor ah, ah
    call mc_dec
    ; --- the UART now (SPEC.md 126.6.1): MCR and MSR, which hold nobody's
    ; evidence, and IIR where no SERIAL.DRV lease can be. NEVER LSR, whose
    ; read clears the error bits the ISR's 9.5.6 test and a lease's sticky
    ; byte are built from, and never RBR. Nothing is written ---
    mov dx, [ser_base]
    add dx, 4
    in al, dx                       ; MCR
    mov [ser_mcr], al
    inc dx
    inc dx
    in al, dx                       ; MSR: no kernel path reads it and no
    mov [ser_msr], al               ; IER here enables its interrupt
    SROW ser_s_msr
    mov al, [ser_msr]
    call mc_hex8
    mov al, ' '
    call mc_putc
    mov dl, [ser_msr]
    mov cx, 4
    mov si, ser_t_msr
    call ser_flags
    SROW ser_s_mcr
    mov al, [ser_mcr]
    call mc_hex8
    mov al, ' '
    call mc_putc
    mov dl, [ser_mcr]
    shl dl, 1
    shl dl, 1
    mov cx, 6
    mov si, ser_t_mcr
    call ser_flags
    SROW ser_s_iir
    mov si, bx
    shr si, 1                       ; SI = 0 / 1, the per-port statics' index
    mov cl, 0                       ; CL = "a received byte is waiting"
    cmp byte [ser_lease], 0
    je .iir                         ; no lease anywhere
    cmp byte [es:di+MOS_SEEN], 0
    je .noiir                       ; a lease can only be on a port that did
    cmp [es:di+MOS_PORT], bl        ; NOT win (115.1), and before the contest
    je .iir                         ; settles that is either of them
.noiir:
    push si
    SPUT ser_s_lsd
    pop si
    jmp short .drn
.iir:
    mov dx, [ser_base]
    inc dx
    inc dx
    in al, dx                       ; IIR: its one side effect is to retire a
    mov ah, al                      ; TX-empty cause, and IER = RX only on
    call mc_hex8                    ; every port the kernel owns
    mov al, ' '
    call mc_putc
    xor al, al
    test ah, 1
    jnz .nm                         ; bit 0 SET is "nothing pending"
    mov al, ah
    and al, 0x0E
    shr al, 1
    inc al                          ; 1..8, the pending cause's own number + 1
    cmp al, 3                       ; RX data (2) or its FIFO timeout (6) is
    je .rx                          ; a byte nobody has collected yet
    cmp al, 7
    jne .nm
.rx:
    inc cx
.nm:
    push si
    mov si, ser_t_iir
    call ser_nth
    call mc_put
    pop si
.drn:
    or cl, cl
    jz .nodr
    inc byte [ser_drn + si]
    jmp short .dr
.nodr:
    mov byte [ser_drn + si], 0
.dr:
    ; --- the verdict: SPEC.md 9.4.6's table, in words ---
    cmp byte [es:di+MOS_IDENT+bx], 0
    jne .v_id
    mov al, [es:di+MOS_IDN+bx]
    or al, al
    jnz .v1
    SLINE ser_s_v0a
    SLINE ser_s_v0b
    jmp .v2
.v1:
    cmp al, SER_IDMAX
    jbe .v1b
    SLINE ser_s_vstr
    jmp .v2
.v1b:
    cmp byte [es:di+MOS_IDB0+bx], 'M'
    je .v1c
    SLINE ser_s_vnm
    jmp .v2
.v1c:
    SLINE ser_s_vnq
    jmp .v2
.v_id:
    SLINE ser_s_vid
.v2:
    cmp byte [es:di+MOS_SEEN], 0
    je .v3
    cmp [es:di+MOS_PORT], bl
    jne .v3
    SLINE ser_s_vwon                ; this row won the contest
    mov al, LP_OK
    call mc_lamp
    mov al, [es:di+MOS_LINE]
    cmp al, [ser_exl]
    je .v3
    SLINE ser_s_vcx                 ; SPEC.md 9.5.2.1: data here, the
    mov al, LP_WARN                 ; interrupt on the other line
    call mc_lamp
.v3:
    mov si, bx
    shr si, 1
    cmp byte [ser_drn + si], 2
    jb .v5
    SLINE ser_s_vdr1                ; a byte nobody collected, twice running
    SLINE ser_s_vdr2
    mov al, LP_BAD
    call mc_lamp
.v5:
    cmp byte [es:di+MOS_SEEN], 0
    je .v5a
    cmp [es:di+MOS_PORT], bl
    jne .v6                         ; another pointer won: reported, not judged
.v5a:
    mov al, [ser_mcr]
    and al, 3
    cmp al, 3
    je .v6
    cmp byte [es:di+MOS_HPST], 1   ; (the poller drops them on purpose)
    je .v6
    SLINE ser_s_vpw
    mov al, LP_WARN
    call mc_lamp
.v6:
    ret

; -----------------------------------------------------------------------------
; ser_glob - the contest's outcome and the poller (kernel state, not a UART)
; -----------------------------------------------------------------------------
ser_glob:
    mov di, [ser_sp]
    SROW ser_s_win
    mov bl, [es:di+MOS_PORT]
    cmp byte [es:di+MOS_SEEN], 0
    jne .w
    SPUT ser_s_noone
    jmp .line
.w:
    mov si, ser_t_win
    mov al, bl
    shr al, 1
    cmp al, 4
    jbe .wi
    mov al, 5
.wi:
    call ser_nth
    call mc_put
.line:
    SROW ser_s_ln
    mov al, [es:di+MOS_LINE]
    mov si, ser_s_l0
    or al, al
    jz .lp
    mov si, ser_s_l4
    cmp al, SER_L4
    je .lp
    mov si, ser_s_l3
    cmp al, SER_L3
    je .lp
    mov si, ser_s_lx
.lp:
    call mc_put
    SROW ser_s_any                  ; [mou_idany] is ALSO stored by every
    cmp byte [es:di+MOS_SEEN], 0    ; backend's settle (mouse.inc's
    je .any                         ; MOU_SETTLE_STORES: PS/2, a driver's
    SPUT ser_s_anys                 ; feed, VMware), so once a pointer has
    jmp .anyd                       ; settled it says nothing about a UART
.any:
    mov al, [es:di+MOS_IDANY]
    call ser_yn
.anyd:
    SROW ser_s_hp
    mov al, [es:di+MOS_HPST]
    and al, 3
    mov si, ser_t_hp
    call ser_nth
    call mc_put
    SROW ser_s_hpt                  ; its last act, as an AGE: the stamp is
    call OSAPI_GET_TICKS            ; the desktop's first frame, then each
    sub ax, [es:di+MOS_HPT]         ; drop and raise (wrap-safe, like [ticks])
    mov si, ser_s_ago
    call ser_secs
    SROW ser_s_dr_
    mov al, [es:di+MOS_DRAIN]
    or al, al
    mov si, ser_s_idle
    jz .d
    mov si, ser_s_armed
.d:
    call mc_put
    ret

; ---------------------------------------------------------------- data -------
ser_s_head: db 'Serial mice', 0
ser_s_nomo: db 'The kernel publishes no mouse block.', 0
ser_s_raw:  db 'Another kernel build: not decoded. Raw block:', 0
ser_s_nul:  db 0
ser_s_no:   db 'no', 0
ser_s_yes:  db 'yes', 0
ser_s_tk:   db ' ticks (~', 0
ser_s_tr:   db ' s)', 0
ser_s_boot: db ' after boot', 0
ser_s_ago:  db ' ago', 0
ser_s_none: db 'none', 0
ser_s_kern: db '  kernel: ', 0
ser_s_uart: db ' UART', 0
ser_s_nopr: db '  not probed by the kernel', 0
ser_s_com:  db 'COM', 0
ser_s_at:   db ' at ', 0
ser_s_irq:  db ', IRQ', 0
ser_s_nouart: db ': no UART answered', 0
ser_s_idb:  db '  Identify burst', 0
ser_s_nothing: db 'none: nothing answered', 0
ser_s_first: db ' bytes, first ', 0
ser_s_stream: db ' stream', 0
ser_s_last: db '  Last byte at', 0
ser_s_shape: db '  Answered as a mouse', 0
ser_s_run:  db '  Clean packets', 0
ser_s_need: db ' of ', 0
ser_s_msr:  db '  MSR (pins in)', 0
ser_s_mcr:  db '  MCR (pins out)', 0
ser_s_iir:  db '  IIR', 0
ser_t_msr:  db 'DCD', 0, 'RI', 0, 'DSR', 0, 'CTS', 0
ser_t_mcr:  db 'AFE', 0, 'LOOP', 0, 'OUT2', 0, 'OUT1', 0, 'RTS', 0, 'DTR', 0
ser_s_lsd:  db 'not read: a serial link is open', 0
ser_t_iir:  db 'none pending', 0, 'modem', 0, 'TX empty', 0, 'RX data', 0
            db 'line status', 0, '?', 0, '?', 0, 'RX timeout', 0, '?', 0
ser_s_v0a:  db 'Nothing answered the reset: no mouse here, or it', 0
ser_s_v0b:  db 'gets no power (a mouse draws it from DTR/RTS).', 0
ser_s_vstr: db 'A stream, not one burst: a peer or modem talks.', 0
ser_s_vnm:  db "Answered, but not 'M' (the kernel needs 'M').", 0
ser_s_vnq:  db "Answered 'M' but never went quiet.", 0
ser_s_vid:  db 'Answered like a mouse.', 0
ser_s_vwon: db 'This port won: the mouse is here.', 0
ser_s_vcx:  db 'Cross-wired: data here, interrupt on the other IRQ.', 0
ser_s_vdr1: db 'A received byte sits unread: no interrupt came.', 0
ser_s_vdr2: db 'Is the IRQ jumper right, or the line not wired?', 0
ser_s_vpw:  db 'DTR/RTS are not both high: a mouse is unpowered.', 0
ser_s_win:  db 'Contest winner', 0
ser_s_noone: db 'none yet: no packet seen', 0
ser_t_win:  db 'serial COM1', 0, 'serial COM2', 0, 'PS/2 aux port', 0
            db 'VMware absolute', 0, 'a pointing driver', 0, '?', 0
ser_s_ln:   db 'Winning serial line', 0
ser_s_l0:   db 'not settled / none', 0
ser_s_l4:   db 'IRQ4', 0
ser_s_l3:   db 'IRQ3', 0
ser_s_lx:   db 'not a serial line', 0
ser_s_any:  db 'Any port mouse-shaped', 0
ser_s_anys: db 'set: a pointer settled', 0
ser_s_hp:   db 'Hot-plug poller', 0
ser_t_hp:   db 'waiting, never fired', 0, 'DTR/RTS held low', 0
            db 'stood down: settled', 0, 'waiting, has fired', 0
ser_s_hpt:  db '  its last tick', 0
ser_s_dr_:  db 'Identify drain', 0
ser_s_idle: db 'idle', 0
ser_s_armed: db 'discarding input', 0
ser_lbl:    db 'COM1 (BIOS)', 0

; --- state: statics in the image (this file owns no bss) ----------------------
ser_bda:    dw 0                ; the BIOS data area window, 0 = none
ser_sp:     dw 0                ; the kernel's 34-byte span
ser_bp:     dw 0                ; mou_bases
ser_base:   dw 0
ser_have:   db 0
ser_exl:    db 0
ser_mcr:    db 0
ser_msr:    db 0
ser_lease:  db 0                ; FF: some instance holds a SERIAL.DRV lease
ser_drn:    db 0, 0             ; refreshes in a row with an RX cause pending
