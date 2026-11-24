; =============================================================================
; drivers/zip/zipppa.inc - VPI0 - the Zip 100's PPA3 (Linux drivers/scsi/ppa.c), SPEC.md 138.2.1
;
; The three procs of ziptrans.inc: ppa_probe, ppa_cmd, ppa_reset. Written from
; SPEC.md 138.2.1's description of the wire, not translated from ppa.c.
;
; Internal helpers (ppa_od/oc/is, ppa_wait, ppa_connect ...) take DS = the
; driver's segment and read the port from [zt_base]; each preserves every
; register but its documented outputs. DATA lives here only as constants -
; the transport has no bss of its own.
;
; Needs ZIPM_NIBBLE / ZIPM_PS2 (apps/os88api.inc) and ziptrans.inc before it.
; =============================================================================

PPA_LONG    equ 255             ; ticks a WRITE/spin-up may keep the drive busy
                                ; (~14 s: a Zip waking from sleep needs ~10 s)
PPA_BURST   equ 512             ; bytes moved per phase check on a bulk command

; --- the port, one register at a time (AL in/out, nothing else touched) -------
ppa_od:                         ; AL -> data latch
    push dx
    mov dx, [zt_base]
    out dx, al
    pop dx
    ret

ppa_oc:                         ; AL -> control register
    push dx
    mov dx, [zt_base]
    add dx, ZT_CTRL
    out dx, al
    pop dx
    ret

; --- delays: the XT's `in` from the port is ~1 us, so a port read IS the clock
; (ISA-bus bound on every machine: a 486 is not 100 times faster at it)
ppa_dly:                        ; CX = port reads to spend (CX = 0 after)
    push ax
    push dx
    mov dx, [zt_base]
    inc dx
.l: in al, dx
    loop .l
    pop dx
    pop ax
    ret

ppa_ms:                         ; ~1 ms (>= 1000 us on ISA; ~8 ms on an 8088)
    push cx
    mov cx, 1000
    call ppa_dly
    pop cx
    ret

ppa_tick:                       ; AX = the BIOS tick, low word (0040:006C)
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    pop es
    ret

; --- ppa_wait: poll the status port until ((status ^ BH) & BL) == 0 ------------
;   in  AL = give-up limit in BIOS ticks (>= 1: the real wait is limit-1..limit)
;       BH = flip mask, BL = test mask (BX = 0FFC0h waits for bits 7 AND 6 high)
;   out AL = status & F0h, CF = 0; or CF = 1 on timeout. BX preserved.
; 256 reads between tick looks: a pure count would be 100x short on a 486.
ppa_wait:
    push bp
    push cx
    push dx
    push si
    xor ah, ah
    mov bp, ax
    call ppa_tick
    mov si, ax
    mov dx, [zt_base]
    inc dx
.o: mov cx, 256
.s: in al, dx
    mov ah, al
    xor ah, bh
    test ah, bl
    jz .g
    loop .s
    call ppa_tick
    cmp ax, si
    je .o
    mov si, ax
    dec bp
    jnz .o
    stc
    jmp short .x
.g: and al, 0F0h                ; clears CF
.x: pop si
    pop dx
    pop cx
    pop bp
    ret

ppa_waitc:                      ; AL = limit; wait for bits 7+6 (the drive is ready)
    push bx
    mov bx, 0FFC0h
    call ppa_wait
    pop bx
    ret

; --- connect / disconnect: four data bytes, each with its control pulse --------
; (the `c` pulse is 04,06,04,0C; the `d` pulse 0C,0E,0C,04,0C)
ppa_connect:
    push bx
    push si
    mov si, ppa_cdat
    mov bx, ppa_cctl
    jmp short ppa_pls
ppa_disconnect:
    push bx
    push si
    mov si, ppa_ddat
    mov bx, ppa_dctl
ppa_pls:
    push ax
    push cx
    mov ch, 4
.d: lodsb
    call ppa_od
    mov cl, [bx]                ; the pulse's length
    push bx
.c: inc bx
    mov al, [bx]
    call ppa_oc
    dec cl
    jnz .c
    pop bx
    dec ch
    jnz .d
    pop cx
    pop ax
    pop si
    pop bx
    ret

; cable check, connected: control 0Eh must raise status bit 3 and 0Ch drop it.
; Three tries, a millisecond apart (the check is electrical, not a wait on the
; drive). out CF = 1: nothing answered; the port is then DISconnected.
ppa_conn:
    push ax
    push cx
    mov cx, 3
.t: call ppa_connect
    mov al, 0Eh
    call ppa_oc
    call ppa_is
    test al, 8
    jz .b
    mov al, 0Ch
    call ppa_oc
    call ppa_is
    test al, 8
    jz .ok
.b: call ppa_disconnect
    call ppa_ms
    loop .t
    stc
.ok:                            ; (test cleared CF on the way here)
    pop cx
    pop ax
    ret

ppa_is:                         ; AL <- status port
    push dx
    mov dx, [zt_base]
    inc dx
    in al, dx
    pop dx
    ret

; select [AL]: wait for the bus to be free (status bit 6 low), put 1<<id on the
; data lines with the select strobe, then the "not the initiator" 80h and
; control 08h, and wait for the target to raise bit 6. CF = 1: nobody.
ppa_select:
    push ax
    push bx
    push cx
    mov cl, al
    mov ch, 1
    shl ch, cl
    mov bx, 0040h               ; bit 6 CLEAR
    mov al, 2
    call ppa_wait
    jc .x
    mov al, ch
    call ppa_od
    mov al, 0Eh
    call ppa_oc
    mov al, 0Ch
    call ppa_oc
    mov al, 80h
    call ppa_od
    mov al, 08h
    call ppa_oc
    mov bx, 0FF40h              ; bit 6 SET
    mov al, 2
    call ppa_wait
.x: pop cx
    pop bx
    pop ax
    ret

; --- the data bursts: THE speed of the driver on a 4.77 MHz XT ---------------
; Each byte's body is a macro, and each burst runs it as: an odd byte first
; (a count of 1 is the whole burst), then pairs with ONE loop per two bytes -
; the `loop` (17 clocks) is paid per pair. Cycle costs: SPEC.md 138.2.1.
%macro PPA_BYTE_OUT 0           ; es lodsb; data; ctrl 0Eh; ctrl 0Ch  (DX data, BX ctrl)
    es lodsb
    out dx, al
    xchg dx, bx
    mov al, 0Eh
    out dx, al
    mov al, 0Ch
    out dx, al
    xchg dx, bx
%endmacro
%macro PPA_BYTE_PS2 0           ; data; ctrl 27h; ctrl 25h  (DX data, BX ctrl)
    in al, dx
    stosb
    xchg dx, bx
    mov al, 27h
    out dx, al
    mov al, 25h
    out dx, al
    xchg dx, bx
%endmacro
%macro PPA_BYTE_NIB 0           ; ctrl 04h, high nibble; ctrl 06h, low  (DX ctrl, CL 4)
    mov al, 04h
    out dx, al
    dec dx
    in al, dx
    mov ah, al
    inc dx
    mov al, 06h
    out dx, al
    dec dx
    in al, dx
    shr al, cl
    and ah, 0F0h
    or al, ah
    stosb
    inc dx
%endmacro

; ppa_out_n   CX bytes ES:DI -> the drive
ppa_out_n:
    push cx
    push dx
    push bx
    push si
    mov dx, [zt_base]
    mov bx, dx
    add bx, ZT_CTRL
    xchg si, di                 ; es lodsb reads ES:SI
    shr cx, 1
    jnc .e
    PPA_BYTE_OUT
.e: jcxz .x
.l: PPA_BYTE_OUT
    PPA_BYTE_OUT
    loop .l
.x: xchg si, di
    pop si
    pop bx
    pop dx
    pop cx
    ret

; ppa_in_n    CX bytes drive -> ES:DI, by [zt_mode]. Clobbers AX.
ppa_in_n:
    push cx
    push dx
    push bx
    push bp
    mov dx, [zt_base]
    cmp byte [zt_mode], ZIPM_PS2
    je .ps
    add dx, ZT_CTRL
    shr cx, 1
    mov bp, cx
    mov cl, 4
    jnc .ne
    PPA_BYTE_NIB
.ne:test bp, bp
    jz .nx
.nl:PPA_BYTE_NIB
    PPA_BYTE_NIB
    dec bp
    jnz .nl
.nx:mov al, 0Ch
    out dx, al
    jmp short .x
.ps:mov bx, dx
    add bx, ZT_CTRL
    xchg dx, bx
    mov al, 25h                 ; turn the data lines round
    out dx, al
    xchg dx, bx
    shr cx, 1
    jnc .pe
    PPA_BYTE_PS2
.pe:jcxz .px
.pl:PPA_BYTE_PS2
    PPA_BYTE_PS2
    loop .pl
.px:xchg dx, bx
    mov al, 04h
    out dx, al
    mov al, 0Ch
    out dx, al
.x: pop bp
    pop bx
    pop dx
    pop cx
    ret

ppa_rd1:                        ; AL <- one byte from the drive (ES, DI kept)
    push es
    push di
    push cx
    push ss
    pop es
    push ax
    mov di, sp
    mov cx, 1
    call ppa_in_n
    pop ax
    pop cx
    pop di
    pop es
    ret

ppa_size:                       ; CX = this burst: 512 on a bulk command with
    mov cx, 1                   ; >= 512 left (DH = 0, BP = left), else 1
    or dh, dh
    jnz .r
    cmp bp, PPA_BURST
    jb .r
    mov cx, PPA_BURST
.r: ret

; --- ppa_cmd: connect, select, CDB, data, status, message, disconnect --------
ppa_cmd:
    push bx
    push dx
    push si
    push di
    push bp
    push es
    push cx                     ; the count asked for (moved = this - what is left)
    mov bp, cx
    mov al, [si]                ; DH = 0 for READ/WRITE (6) and (10): the bulk
    and al, 0DDh                ; opcodes, 08h 0Ah 28h 2Ah, which clear to 08h
    sub al, 08h
    mov dh, al
    call ppa_conn
    mov al, ZTE_NOCONN
    jc .err
    mov al, [zt_id]
    call ppa_select
    mov al, ZTE_NOSEL
    jc .err
    mov al, 0Ch
    call ppa_oc
.cb:mov al, ZT_TMO_TICKS        ; one CDB byte: the drive must say "command"
    call ppa_waitc
    jc .tmo
    and al, 50h
    cmp al, 40h
    jne .ph
    lodsb
    call ppa_od
    mov al, 0Eh
    call ppa_oc
    mov al, 0Ch
    call ppa_oc
    dec bl
    jnz .cb
.dp:mov al, PPA_LONG            ; the data phases, until the drive says "status"
    call ppa_waitc
    jc .tmo
    cmp al, 0F0h
    je .st
    test bp, bp                 ; it wants more than the buffer holds
    jz .ph
    cmp al, 0C0h
    je .ou
    cmp al, 0D0h
    jne .ph
    cmp bh, ZTD_IN
    jne .ph
    call ppa_size
    call ppa_in_n
    jmp short .ad
.ou:cmp bh, ZTD_OUT
    jne .ph
    call ppa_size
    call ppa_out_n
.ad:sub bp, cx
    jmp short .dp
.st:call ppa_rd1                ; the status byte...
    and al, 3Eh
    mov dl, al
    mov al, 1                   ; ...and the optional message, which is dropped
    call ppa_waitc
    jc .ok
    cmp al, 0F0h
    jne .ok
    call ppa_rd1
.ok:mov al, dl
    xor ah, ah
    jmp short .fin
.tmo:
    mov al, ZTE_TIMEOUT
    jmp short .err
.ph:mov al, ZTE_PHASE
.err:
    mov ah, 1
.fin:
    pop cx
    sub cx, bp                  ; bytes moved
    push ax
    call ppa_disconnect
    pop ax
    shr ah, 1                   ; AH was 1 for an error: CF
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    ret

; --- ppa_reset: connect, pulse the SCSI RST line (data 40h, ctrl 08h, ~30 us),
; settle, disconnect, settle
ppa_reset:
    push ax
    push cx
    call ppa_connect
    mov al, 40h
    call ppa_od
    mov al, 08h
    call ppa_oc
    mov cx, 40
    call ppa_dly
    mov al, 0Ch
    call ppa_oc
    call ppa_ms
    call ppa_disconnect
    call ppa_ms
    pop cx
    pop ax
    ret

; --- ppa_probe: TEST UNIT READY at every ID until one answers; in PS/2 mode a
; 36-byte INQUIRY must also read "IOMEGA", or the mode drops to nibble ---------
ppa_probe:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call ppa_disconnect
    xor dx, dx
.id:mov [zt_id], dl
    mov si, ppa_tur
    mov bx, 6                   ; BL = 6 bytes, BH = ZTD_IN
    xor cx, cx
    call ppa_cmd
    jnc .fd
    cmp al, ZTE_NOSEL           ; nobody at this ID: try the next; anything
    jne .no                     ; else (no cable, a dead bus) ends the search
    inc dx
    cmp dl, 8
    jb .id
.no:stc
    jmp short .x
.fd:cmp byte [zt_mode], ZIPM_PS2
    jne .rs
    sub sp, 36
    push ss
    pop es
    mov di, sp
    mov si, ppa_inq
    mov bx, 6
    mov cx, 36
    call ppa_cmd
    jc .lo
    mov si, ppa_iom
    add di, 8
    mov cx, 6
    repe cmpsb
    je .sp
.lo:mov byte [zt_mode], ZIPM_NIBBLE
.sp:add sp, 36
.rs:call ppa_reset
    clc
.x: pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- constants ---------------------------------------------------------------
ppa_cdat: db 00h, 3Ch, 20h, 8Fh        ; connect: the four data bytes
ppa_cctl: db 4, 04h, 06h, 04h, 0Ch     ;   each with this control pulse
ppa_ddat: db 00h, 3Ch, 20h, 0Fh        ; disconnect
ppa_dctl: db 5, 0Ch, 0Eh, 0Ch, 04h, 0Ch
ppa_tur:  db 00h, 00h, 00h, 00h, 00h, 00h
ppa_inq:  db 12h, 00h, 00h, 00h, 24h, 00h
ppa_iom:  db 'IOMEGA'
