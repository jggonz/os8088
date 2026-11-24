; =============================================================================
; drivers/zip/zipimm.inc - VPI2 - the Zip Plus/250's MatchMaker (Linux
; drivers/scsi/imm.c), SPEC.md 138.2.2
;
; The three procs of ziptrans.inc: imm_probe, imm_cmd, imm_reset. The protocol
; facts and the per-byte cycle costs are SPEC.md 138.2.2; written from that
; description, not translated from the C.
;
; Conventions inside this file:
;   - the port is [zt_base]; ctrl = base+2, status = base+1, data = base+0
;   - the helpers (imm_wd/_wc/_rs, imm_dN, imm_cpp, imm_connect, ...) preserve
;     every register but their outputs; imm_wait clobbers AH
;   - the data movers (imm_in, imm_out) take BP = bytes and clobber AX, BP, DX
;   - sub-microsecond-class protocol minimums (the udelay()s of imm_cpp) are
;     met by READING THE STATUS PORT n times: an ISA port read is at least
;     ~1 us on any machine (and ~5 us with the loop on a 4.77 MHz XT), the
;     diagram's upper bounds are all "infinite", so overshooting is safe
;   - waits longer than a millisecond are BIOS ticks (0040:006C), never counts
;   - EPP is not done (SPEC.md 138.11)
; =============================================================================

; ----- the data movers' inner bodies, written once, expanded ×3 ---------------
; IMM_NIB: one byte, nibble mode. DX = ctrl on entry and exit, CL = 4,
; ES:DI the destination. Low nibble FIRST (status bits 7-4 after ctrl 06h),
; high nibble second (after ctrl 05h).
%macro IMM_NIB 0
    mov al,6
    out dx,al
    dec dx
    in al,dx
    inc dx
    shr al,cl
    mov ah,al
    mov al,5
    out dx,al
    dec dx
    in al,dx
    inc dx
    and al,0F0h
    or al,ah
    stosb
    mov al,4
    out dx,al
%endmacro

; IMM_BYT: one byte, PS/2 mode. DX = ctrl on entry and exit.
%macro IMM_BYT 0
    mov al,26h
    out dx,al
    dec dx
    dec dx
    in al,dx
    stosb
    inc dx
    inc dx
    mov al,25h
    out dx,al
%endmacro

; ----- the port, one register a call --------------------------------------------
imm_wd:                        ; AL -> data
    push dx
    mov dx,[zt_base]
    out dx,al
    pop dx
    ret

imm_wc:                        ; AL -> control
    push dx
    mov dx,[zt_base]
    add dx,2
    out dx,al
    pop dx
    ret

imm_rs:                        ; out AL = status
    push dx
    mov dx,[zt_base]
    inc dx
    in al,dx
    pop dx
    ret

; ----- delays: n status-port reads (see the header) ----------------------------
imm_d2:
    push cx
    mov cl,2
    jmp short imm_dl
imm_d5:
    push cx
    mov cl,5
    jmp short imm_dl
imm_d10:
    push cx
    mov cl,10
    jmp short imm_dl
imm_d50:
    push cx
    mov cl,50
    jmp short imm_dl
imm_d100:
    push cx
    mov cl,100
imm_dl:
    push ax
    push dx
    mov dx,[zt_base]
    inc dx
    xor ch,ch
.l: in al,dx
    loop .l
    pop dx
    pop ax
    pop cx
    ret

imm_ticks:                     ; out AX = the BIOS tick count's low word
    push ds
    mov ax,40h
    mov ds,ax
    mov ax,[6Ch]
    pop ds
    ret

imm_ms:                        ; a settle delay of at least one full tick (55 ms)
    push ax
    push bx
    call imm_ticks
    mov bl,al
.l: call imm_ticks
    sub al,bl
    cmp al,2
    jb .l
    pop bx
    pop ax
    ret

; ----- imm_cpp: the Command Packet Protocol ------------------------------------
; in  AL = the command byte (30h deselect all, E0h select, compatible mode)
; out AL = 1 connected, 0 disconnected, FFh no device answered
%macro IMM_PB 0               ; one CPP data byte, held >= 7 us
    lodsb
    call imm_wd
    call imm_d10
%endmacro

imm_pre: db 0AAh, 55h, 00h, 0FFh, 87h, 78h

imm_cpp:
    push bx
    push cx
    push si
    push dx
    mov bh,al
    mov al,0Ch
    call imm_wc
    call imm_d2
    mov si,imm_pre
    IMM_PB                     ; AAh
    IMM_PB                     ; 55h
    IMM_PB                     ; 00h
    IMM_PB                     ; FFh
    call imm_rs                ; s1
    and al,0B8h
    push ax
    IMM_PB                     ; 87h
    call imm_rs                ; s2
    and al,0B8h
    push ax
    IMM_PB                     ; 78h
    call imm_rs                ; s3
    and al,0B8h
    push ax
    mov al,bh
    call imm_wd
    call imm_d2
    mov al,0Ch
    call imm_wc
    call imm_d10
    mov al,0Dh
    call imm_wc
    call imm_d2
    mov al,0Ch
    call imm_wc
    call imm_d10
    mov al,0FFh
    call imm_wd
    call imm_d10
    pop ax                     ; s3
    pop bx                     ; s2
    pop cx                     ; s1
    and al,38h
    mov dl,0FFh
    cmp cl,0B8h
    jne .r
    cmp bl,18h
    jne .r
    mov dl,1                   ; s3 = 30h: connected
    cmp al,30h
    je .r
    mov dl,0                   ; s3 = 38h: disconnected
    cmp al,38h
    je .r
    mov dl,0FFh
.r: mov al,dl
    pop dx
    pop si
    pop cx
    pop bx
    ret

imm_disc:                      ; deselect all; preserves AX
    push ax
    mov al,30h
    call imm_cpp
    pop ax
    ret

imm_connect:                   ; out CF = 1 if the drive did not answer
    push ax
    mov al,0E0h
    call imm_cpp
    mov al,30h
    call imm_cpp
    mov al,0E0h
    call imm_cpp
    cmp al,1
    pop ax
    je .y
    stc
    ret
.y: clc
    ret

; ----- selection ---------------------------------------------------------------
imm_select:                    ; select [zt_id]; out CF = 1 nobody latched the bus
    push ax
    push cx
    push dx
    mov dx,[zt_base]
    add dx,2
    mov al,0Ch
    out dx,al
    dec dx
    mov cx,500                 ; 0.5 ms on a fast port, ~4 ms on the XT
.a: in al,dx                   ; the bus must be free first
    test al,8
    loopnz .a
    jnz .f
    inc dx
    mov al,4
    out dx,al
    dec dx
    dec dx
    mov cl,[zt_id]
    mov al,1
    shl al,cl
    or al,80h                  ; the host's ID bit and the target's
    out dx,al
    call imm_d2
    inc dx
    inc dx
    mov al,0Ch
    out dx,al
    mov al,0Dh
    out dx,al
    dec dx
    mov cx,500
.b: in al,dx                   ; /ERROR rises when the drive latches the bus
    test al,8
    loopz .b
    jz .f
    inc dx
    mov al,0Ch
    out dx,al
    clc
    jmp short .x
.f: inc dx                     ; (DX = status in .a's failure, = ctrl-1 in .b's)
    mov al,0Ch
    out dx,al
    stc
.x: pop dx
    pop cx
    pop ax
    ret

; ----- imm_wait: the drive's phase -----------------------------------------------
; ctrl 0Ch, spin on status bit 7 (REQ) up to AH ticks (imm_wait: ZT_TMO_TICKS;
; imm_wtm: the caller's), ctrl 04h. out AL = status & B8h, CF = 1 timed out.
; Phases: 88h wants data, 98h sends data, A8h wants command bytes,
; B8h status/message. Clobbers AH.
imm_wait:
    mov ah,ZT_TMO_TICKS
imm_wtm:
    push bx
    push cx
    push dx
    mov bh,ah
    mov dx,[zt_base]
    add dx,2
    mov al,0Ch
    out dx,al
    dec dx
    call imm_ticks
    mov bl,al
.t: mov cx,64
.s: in al,dx
    test al,80h
    jnz .g
    loop .s
    call imm_ticks
    sub al,bl
    cmp al,bh
    jb .t
    stc
    jmp short .x
.g: mov bl,al
    and bl,0B8h
    clc
.x: pushf
    inc dx
    mov al,4
    out dx,al
    popf
    mov al,bl
    pop dx
    pop cx
    pop bx
    ret

; ----- IEEE 1284 negotiation: out CF = 1 no data available ----------------------
imm_negotiate:
    push ax
    mov al,4
    call imm_wc
    call imm_d5
    mov al,[zt_mode]           ; ZIPM_NIBBLE (1) -> 00h, ZIPM_PS2 (2) -> 01h
    dec al
    call imm_wd
    call imm_d100
    mov al,6
    call imm_wc
    call imm_d5
    call imm_rs
    and al,20h                 ; PAPER OUT high = data available
    mov ah,al
    call imm_d5
    mov al,7
    call imm_wc
    call imm_d5
    mov al,6
    call imm_wc
    cmp ah,1                   ; CF = 1 if bit 5 was clear
    pop ax
    ret

; the post-read sequence: ctrl 04h, 0Ch, 0Eh, 04h
imm_endseq:
    push ax
    mov al,4
    call imm_wc
    mov al,0Ch
    call imm_wc
    mov al,0Eh
    call imm_wc
    mov al,4
    call imm_wc
    pop ax
    ret

; the bus reset pulse
imm_pulse:
    push ax
    mov al,4
    call imm_wc
    mov al,40h
    call imm_wd
    call imm_d2
    mov al,0Ch
    call imm_wc
    mov al,0Dh
    call imm_wc
    call imm_d50
    mov al,0Ch
    call imm_wc
    mov al,4
    call imm_wc
    pop ax
    ret

; ----- the data movers -----------------------------------------------------------
; imm_out: BP = bytes (even), ES:SI the source. Two bytes a strobe cycle:
; data, ctrl 05h (strobe), data, ctrl 00h; ctrl 04h around the burst.
imm_out:
    mov dx,[zt_base]
    add dx,2
    mov al,4
    out dx,al
    dec dx
    dec dx
    shr bp,1
    jz .e
.p: es lodsb
    out dx,al
    inc dx
    inc dx
    mov al,5
    out dx,al
    dec dx
    dec dx
    es lodsb
    out dx,al
    inc dx
    inc dx
    xor al,al
    out dx,al
    dec dx
    dec dx
    dec bp
    jnz .p
.e: inc dx
    inc dx
    mov al,4
    out dx,al
    ret

; imm_in: BP = bytes (any), ES:DI the destination; ctrl ends at 0Ch.
imm_in:
    cmp byte [zt_mode],ZIPM_PS2
    je imm_bin
    push cx
    mov cl,4
    mov dx,[zt_base]
    add dx,2
    mov al,4
    out dx,al
    shr bp,1
    pushf
    jz .t
.p: IMM_NIB
    IMM_NIB
    dec bp
    jnz .p
.t: popf
    jnc .e
    IMM_NIB
.e: mov al,0Ch
    out dx,al
    pop cx
    ret

imm_bin:
    mov dx,[zt_base]
    add dx,2
    mov al,4
    out dx,al
    shr bp,1
    pushf
    jz .t
.p: IMM_BYT
    IMM_BYT
    dec bp
    jnz .p
.t: popf
    jnc .e
    IMM_BYT
.e: mov al,0Ch
    out dx,al
    ret

; ----- imm_cmd: one SCSI command, start to finish -----------------------------------
; Frame: bx dx si di bp es cx, then a scratch word (status, message) on top.
imm_cmd:
    push bx
    push dx
    push si
    push di
    push bp
    push es
    push cx
    push ax
    call imm_connect
    jc .nc
    call imm_select
    jc .ns
    push es                    ; the CDB goes out of DS
    push ds
    pop es
    mov al,bl
    xor ah,ah
    mov bp,ax
.c: call imm_wait              ; the CDB, a pair per wait; the drive must be
    jc .ct                     ; listening (S_BSY high, I/O low)
    and al,18h
    cmp al,8
    jne .cp
    push bp
    mov bp,2
    call imm_out
    pop bp
    sub bp,2
    ja .c
    pop es                     ; the caller's buffer
    mov si,di                  ; (a write reads through ES:SI)
    call imm_wait
    jc .tm
    test al,20h                ; bit 5 clear: a data phase
    jnz .st
    jcxz .pj                   ; the drive wants more than the caller allows
    mov bl,98h                 ; the phase byte we expect: 98h in, 88h out
    test bh,bh
    jnz .w
    call imm_negotiate         ; reads are negotiated once, before the data
    jc .ph
    jmp short .d
.pj: jmp .ph
.w: mov bl,88h
.d: cmp al,0B8h
    je .de
    mov ah,al
    and ah,98h
    cmp ah,bl
    jne .ph
    jcxz .pj
    mov bp,512                 ; a sector-multiple burst, else the minimum unit
    cmp cx,bp
    jae .go
    mov bp,2
    test bh,bh
    jnz .go
    dec bp
.go: push bp
    test bh,bh
    jnz .o
    call imm_in
    jmp short .n
.o: call imm_out
.n: pop ax
    sub cx,ax
    jnb .m
    xor cx,cx
.m: call imm_wait
    jc .tm
    jmp short .d
.de: test bh,bh
    jnz .st
    call imm_endseq            ; after an IN data phase
.st: call imm_wait             ; status phase: B8h or the command failed
    jc .tm
    cmp al,0B8h
    jne .ph
    call imm_negotiate
    jc .ph
    push ss
    pop es
    mov di,sp                  ; the scratch word
    mov bp,1
    call imm_in                ; the status byte
    mov ah,2                   ; the message byte, if the drive sends one
    call imm_wtm
    jc .nm
    cmp al,0B8h
    jne .nm
    mov bp,1
    call imm_in
.nm: call imm_endseq
    mov di,sp
    mov al,[es:di]
    call imm_disc
    add sp,2
    pop bp                     ; the original CX
    sub bp,cx
    mov cx,bp                  ; = the bytes moved
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    clc
    ret
.ct: pop es                    ; a failure inside the CDB loop owes the
    jmp short .tm              ; extra ES it pushed
.cp: pop es
    jmp short .ph
.nc: mov al,ZTE_NOCONN
    jmp short .f
.ns: mov al,ZTE_NOSEL
    jmp short .f
.tm: mov al,ZTE_TIMEOUT
    jmp short .f
.ph: mov al,ZTE_PHASE
.f: call imm_disc
    add sp,2
    pop cx
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    stc
    ret

; ----- imm_reset: the bus reset pulse ------------------------------------------------
imm_reset:
    push ax
    call imm_connect
    call imm_pulse
    call imm_ms
    call imm_disc
    call imm_ms
    pop ax
    ret

; ----- imm_probe ----------------------------------------------------------------------
imm_cdb:  db 0,0,0,0,0,0       ; TEST UNIT READY
imm_cdbi: db 12h,0,0,0,10h,0   ; INQUIRY, 16 bytes
imm_vend: db 'IOMEGA'

imm_probe:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call imm_connect           ; nothing on the wire answers the CPP: stop
    jc .no
    call imm_pulse             ; a drive in an unknown state: reset it first
    call imm_ms
    call imm_disc
    call imm_ms
    mov byte [zt_id],0
.s: mov si,imm_cdb             ; a TEST UNIT READY on each ID in turn
    mov bx,6
    xor cx,cx
    call imm_cmd
    jnc .f
    cmp al,ZTE_NOSEL
    jne .no
    inc byte [zt_id]
    cmp byte [zt_id],8
    jb .s
.no: call imm_disc
    stc
    jmp short .x
.f: cmp byte [zt_mode],ZIPM_PS2
    jne .ok
    sub sp,16                  ; prove the byte-wide reads: INQUIRY, "IOMEGA"
    push ss
    pop es
    mov di,sp
    mov si,imm_cdbi
    mov bx,6
    mov cx,16
    call imm_cmd
    jc .lo
    add di,8
    mov si,imm_vend
    mov cx,6
    repe cmpsb
    jne .lo
    add sp,16
    jmp short .ok
.lo: add sp,16
    call imm_reset
    mov byte [zt_mode],ZIPM_NIBBLE
.ok: clc
.x: pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
