; =============================================================================
; os8088 - CDROM.DRV / atapi.inc - the drive (SPEC.md 119.2)
;
; ATAPI on the IDE task file: polled PIO, `rep insw` on a 286 or better, and
; every wait bounded by the system tick rather than by a loop count - a CD's
; seek is up to a second and its spin-up several, and a count tuned on one CPU
; is a different time on the next.
;
; THE DEVICE CONTROL REGISTER IS NEVER WRITTEN. nIEN stays as the BIOS left
; it, so a hard disk sharing the channel keeps its IRQ 14 (an AT BIOS's int
; 13h waits on it); the drive's own interrupt is acknowledged by the status
; read every poll ends with.
;
; Prefix cd_. ES is KERNEL_SEG on entry to every callback (SPEC.md 20.1), so
; every string instruction here loads its own segments.
; =============================================================================

ATA_DATA     equ 0
ATA_ERR      equ 1              ; read: the error register (sense key, 7-4)
ATA_FEAT     equ 1              ; write: features (0 = PIO)
ATA_IREASON  equ 2              ; read: interrupt reason (bit 0 CoD, bit 1 IO)
ATA_BCL      equ 4              ; byte count, low
ATA_BCH      equ 5              ; ...and high
ATA_DRVH     equ 6
ATA_STAT     equ 7              ; read: status; write: command
ATA_ALT      equ 0x206          ; base + this = alternate status (no ack)

ATA_ST_BSY   equ 0x80
ATA_ST_DRQ   equ 0x08
ATA_ST_ERR   equ 0x01

ATA_C_PACKET equ 0xA0
ATA_C_PIDENT equ 0xA1           ; IDENTIFY PACKET DEVICE

CD_BCLIM     equ 0x8000         ; the byte count limit a PACKET carries: the
                                ; largest DRQ block we take (even, < FFFEh)
CD_PROBE_T   equ 10             ; ticks a probe waits on one device (~0.55 s)
CD_CMD_T     equ 364            ; ticks a command may take (20 s): spin-up
CD_READY_T   equ 9              ; ticks between "becoming ready" retries
CD_READY_N   equ 30             ; ...and how many (~15 s)
CD_UA_N      equ 4              ; UNIT ATTENTIONs one command rides out: a
                                ; drive just powered on reports the reset AND
                                ; the medium in it, one per command, before it
                                ; answers anything (86Box's did: the CD
                                ; Player's first poll failed, the next did not)

CD_TMO       equ 0x80           ; cd_packet's AL for a timeout - no sense key
                                ; is above 0Fh

; -----------------------------------------------------------------------------
; cd_tmo_set - start a bound of AX ticks (internal)
; clobbers: nothing
;
; TWO bounds, either of which ends the wait. The TICK bound is the real one:
; it counts CHANGES of the system tick (OSAPI_GET_TICKS, the kernel's [ticks]),
; so a wrap is one more change and not 65,000 ticks. The POLL bound is the net
; under it for a machine whose tick is not running (IF = 0 on a path that
; should not have it): 256 x 65,536 status reads, tens of seconds of ISA, so
; it never decides anything on a machine that is working.
;
; THE KERNEL'S TICK AND NOT THE BIOS'S 0:046C. Segment 0 is the NULL selector
; under kern_mega's protected mode (CPU_F_PM), so `mov es, 0` and the first
; read through it is #GP(0) - on every command, since this bound is taken on
; every one. The slot reads the same clock on every kernel and is one far call
; with no segment register touched (KERN-MEGA-PLAN 10.1 item 11).
; -----------------------------------------------------------------------------
cd_tmo_set:
    push ax
    mov [cd_tleft], ax
    call OSAPI_GET_TICKS
    mov [cd_tlast], ax
    mov word [cd_ilo], 0xFFFF   ; 256 x 65,536 polls: the high byte is
    mov byte [cd_ihi], 0xFF     ; spent when the low word borrows, and the
                                ; bound is the borrow out of it
    pop ax
    ret

; cd_tmo_up - has the bound run out? out: CF = 1 yes. clobbers: nothing
cd_tmo_up:
    push ax
    call OSAPI_GET_TICKS
    cmp ax, [cd_tlast]
    je .same
    mov [cd_tlast], ax
    dec word [cd_tleft]         ; a tick went by: one fewer
    jz .exp
.same:
    sub word [cd_ilo], 1        ; ...and one poll fewer, every call
    jnc .ok
    sub byte [cd_ihi], 1
    jc .out                     ; the net: 256 x 65,536 polls (CF = 1)
.ok:
    clc
.out:
    pop ax
    ret
.exp:
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; cd_wait - poll the status until BSY clears, inside the bound cd_tmo_set set
; in:  BX = the base port
; out: CF = 0 and AL = the status; CF = 1 = it never cleared. A floating bus
;      (0FFh) is answered at once as CF = 0, AL = 0FFh, for the caller to
;      refuse - it is never a drive's status (§52.1.3's rule)
; clobbers: AX (AL the output)
; -----------------------------------------------------------------------------
cd_wait:
    push dx
    lea dx, [bx+ATA_STAT]
.poll:
    in al, dx
    cmp al, 0xFF
    je .ok
    test al, ATA_ST_BSY
    jz .ok
    call cd_tmo_up
    jnc .poll
    pop dx
    ret                         ; CF = 1
.ok:
    clc
    pop dx
    ret

; cd_delay - ~400 ns after a select or a command: four reads of the
; alternate status, which acknowledge nothing (internal). BX = base
cd_delay:
    push ax
    push dx
    lea dx, [bx+ATA_ALT]
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_select - point the task file at our unit (internal)
; in:  BX = base, CL = unit 0/1
; clobbers: AX, flags
; -----------------------------------------------------------------------------
cd_select:
    push dx
    lea dx, [bx+ATA_DRVH]
    mov al, 0xA0
    test cl, 1
    jz .d0
    mov al, 0xB0
.d0:
    out dx, al
    pop dx
    jmp short cd_delay

; =============================================================================
; THE PROBE (SPEC.md 119.2)
; =============================================================================

cd_bases:   dw 0x1F0, 0x170, 0x1E8, 0x168
CD_NBASE    equ 4

; -----------------------------------------------------------------------------
; cd_probe - find the first CD-ROM. out: CF = 0 and [cd_base], [cd_unit],
;            [cd_pkw] and [cd_model] set; CF = 1 = none
;
; The first two channels, then - only if neither held one - the other two:
; 1E8h and 168h are where a sound card's own IDE port sits, and a machine
; that already showed us its CD-ROM has no reason to have them written.
; -----------------------------------------------------------------------------
cd_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.base:
    mov bx, [cd_bases+si]
    lea dx, [bx+ATA_STAT]
    in al, dx
    cmp al, 0xFF
    je .nextbase                ; a floating bus: nothing written to it
    xor cl, cl
.unit:
    call cd_ident
    jnc .found
    inc cl
    cmp cl, 2
    jb .unit
.nextbase:
    add si, 2
    cmp si, CD_NBASE * 2
    jb .base
    stc
    jmp short .out
.found:
    mov [cd_base], bx
    mov [cd_unit], cl
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_ident - IDENTIFY PACKET DEVICE on one unit (internal)
; in:  BX = base, CL = unit
; out: CF = 0 a CD-ROM: [cd_pkw] and [cd_model] set; CF = 1 not one
; clobbers: AX, DX, flags
;
; An ATA disk aborts A1h; an absent device never raises DRQ inside
; CD_PROBE_T; an ATAPI device that is not a CD-ROM (a tape, a ZIP) answers
; with another type in word 0. All 256 words are drained whatever they say -
; the drive holds DRQ until they are.
; -----------------------------------------------------------------------------
cd_ident:
    push cx
    push di
    push es
    mov ax, CD_PROBE_T
    call cd_tmo_set
    call cd_select
    call cd_wait
    jc .no
    cmp al, 0xFF
    je .no
    lea dx, [bx+ATA_STAT]
    mov al, ATA_C_PIDENT
    out dx, al
    call cd_delay
    call cd_wait
    jc .no
    test al, ATA_ST_ERR
    jnz .no
    test al, ATA_ST_DRQ
    jz .no                      ; nobody answered: no device there
    mov dx, bx                  ; ATA_DATA
    in ax, dx                   ; word 0
    mov [cd_w0], ax
    mov cx, 26                  ; words 1-26, dropped
.skip:
    in ax, dx
    loop .skip
    push ds
    pop es
    mov di, cd_model
    mov cx, 20                  ; words 27-46: the model, two characters a
.model:                         ; word and the FIRST in the HIGH byte
    in ax, dx
    xchg al, ah
    stosw
    loop .model
    mov cx, 256 - 47
.rest:
    in ax, dx
    loop .rest
    mov byte [cd_model+40], 0
    mov ax, [cd_w0]
    and ah, 0xDF                ; bits 15-14 = 10b (ATAPI) and 12-8 = 5 (a
    cmp ah, 0x85                ; CD-ROM): bit 13 is reserved
    jne .no
    and al, 3                   ; the packet's size: 0 = 12 bytes, 1 = 16
    mov word [cd_pkw], 6
    jz .trim
    cmp al, 1
    jne .no                     ; a size no ATAPI version defines
    mov word [cd_pkw], 8
.trim:                          ; the model is padded with spaces: trim them
    mov di, cd_model + 39
.tr:
    cmp byte [di], ' '
    jne .ok
    mov byte [di], 0
    dec di
    cmp di, cd_model
    jae .tr
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop cx
    ret

; =============================================================================
; THE PACKET COMMAND (SPEC.md 119.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cd_packet - send [cd_pkt] and move its data (internal)
; in:  [cd_pkt] = the packet (16 bytes, the tail zero for a 12-byte command)
;      [cd_xseg]:[cd_xoff] = the buffer, [cd_xmax] = its capacity in bytes
;      (0 = no data), [cd_xout] = 1 when the data goes TO the drive
; out: CF = 0 done, [cd_xgot] = bytes moved; CF = 1 and AL = the sense key the
;      drive reported (1..0Fh), 0 for an error with none, or CD_TMO
; clobbers: AX, flags
;
; A DRQ block longer than the buffer has room for is drained into nothing:
; the drive decides how much it sends, and stopping early would hang it.
;
; THE BUFFER MUST NOT WRAP ITS SEGMENT: [cd_xoff] + [cd_xmax] <= 65,536, and
; ES:DI walks the offset only. It used to fold DI's paragraphs into ES after
; every DRQ block, and under kern_mega's protected mode (CPU_F_PM) a segment
; off the 8-paragraph grid is no selector at all - #GP with RPL bits, and with
; bit 2 alone SILENTLY an LDT alias of some other memory (KERN-MEGA-PLAN
; 14.19). Every caller already keeps the rule: cd_bytes hands over a
; destination cd_bnorm has folded to under 128 bytes and asks for at most
; CD_RUN x 2,048 = 32 KB, and the rest read into our own image.
; -----------------------------------------------------------------------------
cd_packet:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [cd_xgot], 0
    mov bx, [cd_base]
    mov ax, CD_CMD_T
    call cd_tmo_set
    call cd_wait                ; whatever the drive was doing, finished
    jc .tmo
    mov cl, [cd_unit]
    call cd_select
    call cd_wait
    jc .tmo
    lea dx, [bx+ATA_FEAT]
    xor al, al
    out dx, al                  ; PIO, no overlap
    lea dx, [bx+ATA_BCL]
    mov al, CD_BCLIM & 0xFF
    out dx, al
    inc dx
    mov al, CD_BCLIM >> 8
    out dx, al
    lea dx, [bx+ATA_STAT]
    mov al, ATA_C_PACKET
    out dx, al
    call cd_delay
    call cd_wait
    jc .tmo
    test al, ATA_ST_ERR
    jnz .err
    test al, ATA_ST_DRQ
    jz .err                     ; no packet phase: refused outright
    mov dx, bx                  ; the packet, a word at a time
    mov si, cd_pkt
    mov cx, [cd_pkw]
    cld
.pw:
    lodsw
    out dx, ax
    loop .pw
    ; --- the data phase: a DRQ block at a time ------------------------------
    mov es, [cd_xseg]
    mov di, [cd_xoff]
.phase:
    call cd_delay
    call cd_wait
    jc .tmo
    test al, ATA_ST_ERR
    jnz .err
    test al, ATA_ST_DRQ
    jz .done
    lea dx, [bx+ATA_BCL]        ; this block's length
    in al, dx
    mov cl, al
    inc dx
    in al, dx
    mov ch, al
    inc cx
    shr cx, 1                   ; words, the odd byte rounded up
    jcxz .empty
    lea dx, [bx+ATA_IREASON]
    in al, dx
    mov dx, bx                  ; ATA_DATA from here
    test al, 2                  ; IO: 1 = to us, 0 = from us
    jz .outblk
    ; data in: what fits goes to ES:DI, the rest is drained
    mov ax, [cd_xmax]
    sub ax, [cd_xgot]
    shr ax, 1                   ; words of room left
    cmp ax, cx
    jbe .take
    mov ax, cx
.take:                          ; AX = words to keep, CX = words offered
    sub cx, ax
    push cx
    mov cx, ax
    shl ax, 1
    add [cd_xgot], ax
    jcxz .drain
    db 0xF3, 0x6D               ; REP INSW: a 286 or better (SPEC.md 119.2)
.drain:
    pop cx
    jcxz .phase                 ; (DI is NOT folded into ES: see above)
.dr:
    in ax, dx
    loop .dr
    jmp short .phase
.outblk:                        ; data out (MODE SELECT): from ES:DI,
    push ds                     ; one short block, never normalised
    push es
    pop ds
    mov si, di
.ow:
    lodsw
    out dx, ax
    loop .ow
    pop ds
    mov di, si
    mov ax, si
    sub ax, [cd_xoff]
    mov [cd_xgot], ax
    jmp short .phase
.empty:                         ; DRQ with a count of 0 (or FFFFh): nothing to
    call cd_tmo_up              ; move, and BSY is clear, so cd_wait would not
    jc .tmo                     ; count it - the bound is counted here instead
    jmp .phase
.done:
    clc
    jmp short .out
.err:
    lea dx, [bx+ATA_ERR]
    in al, dx
    mov cl, 4
    shr al, cl                  ; the sense key, 0 = none given
    stc
    jmp short .out
.tmo:
    mov al, CD_TMO
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cd_pclear - [cd_pkt] all zero, and no data phase (internal)
; out: DI = cd_pkt; clobbers: AX, CX
; -----------------------------------------------------------------------------
cd_pclear:
    push es
    push ds
    pop es
    mov di, cd_pkt
    xor ax, ax
    mov cx, 8
    cld
    rep stosw
    pop es
    mov di, cd_pkt
    mov word [cd_xmax], 0
    mov byte [cd_xout], 0
    ret

; cd_xin - the data phase reads CX bytes into our own DS:SI (internal)
cd_xin:
    mov [cd_xseg], ds
    mov [cd_xoff], si
    mov [cd_xmax], cx
    ret

; -----------------------------------------------------------------------------
; cd_cmd - cd_packet, with the drive's two ordinary refusals handled
; in:  as cd_packet. out: CF = 0 done; CF = 1 and AX = a CDE_*
; clobbers: AX, flags
;
; UNIT ATTENTION (key 6) is a medium changed or a drive reset: the media
; generation moves (everything read off the old disc is forgotten) and the
; command is sent again, up to CD_UA_N times. NOT READY (key 2) asks REQUEST SENSE why: 3Ah is
; no disc, 04h is a disc still spinning up and is waited out (CD_READY_N x
; CD_READY_T ticks). The packet is banked across the REQUEST SENSE, which
; reuses [cd_pkt].
; -----------------------------------------------------------------------------
cd_cmd:
    push bx
    push cx
    push si
    push di
    push es
    mov byte [cd_tries], CD_READY_N
    mov byte [cd_uatry], CD_UA_N
.again:
    call cd_packet
    jnc .out
    mov [cd_lkey], al           ; the last refusal, for a diagnosis: the key
    mov ah, [cd_pkt]            ; and the command it answered (CDV_DRIVE's
    mov [cd_lop], ah            ; registers can be read off a live machine)
    cmp al, 6
    jne .notua
    call cd_changed             ; the disc is not the one we knew
    dec byte [cd_uatry]
    jnz .again
    jmp short .io
.notua:
    cmp al, 2
    jne .other
    call cd_sense               ; AL = the ASC
    cmp al, 0x3A
    je .nodisc
    cmp al, 0x04
    jne .notready
    dec byte [cd_tries]
    jz .notready
    mov ax, CD_READY_T          ; wait, then try again
    call cd_tmo_set
.nap:
    call cd_tmo_up
    jnc .nap
    jmp short .again
.other:
    cmp al, 5                   ; ILLEGAL REQUEST: the drive does not do
    jne .io                     ; that - an audio command on a drive with
    mov ax, CDE_NOAUDIO         ; none, the one place it is expected
    jmp short .fail
.nodisc:
    call cd_forget              ; no disc is a disc we do not know either
    mov ax, CDE_NODISC
    jmp short .fail
.notready:
    mov ax, CDE_NOTREADY
    jmp short .fail
.io:
    mov ax, CDE_IO
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cd_sense - REQUEST SENSE, for the additional sense code (internal)
; out: AL = the ASC (0 when the drive gave none); [cd_pkt] and the transfer
;      words are put back as they were, so the caller can re-send
; clobbers: AX, flags
; -----------------------------------------------------------------------------
cd_sense:
    push cx
    push si
    push di
    mov si, cd_pkt              ; bank the packet and the transfer
    mov di, cd_pkt2
    mov cx, 8 + 4               ; 16 bytes of packet, then xseg/xoff/xmax/xout
    call cd_wcopy
    call cd_pclear
    mov byte [di], 0x03         ; REQUEST SENSE
    mov byte [di+4], 18         ; allocation length
    mov si, cd_snsbuf
    mov cx, 18
    call cd_xin
    mov byte [cd_snsbuf+12], 0
    call cd_packet
    mov al, [cd_snsbuf+12]      ; the ASC (0 if it failed: nothing to add)
    push ax
    mov si, cd_pkt2
    mov di, cd_pkt
    mov cx, 8 + 4
    call cd_wcopy
    pop ax
    pop di
    pop si
    pop cx
    ret

; cd_wcopy - CX words DS:SI -> DS:DI (internal); clobbers CX, SI, DI
cd_wcopy:
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    ret

; -----------------------------------------------------------------------------
; cd_changed - the disc is not the one we read (SPEC.md 119.3.4): the media
; generation moves - UNIT ATTENTION is the one event that says so - and
; cd_forget runs. clobbers: nothing
;
; cd_forget - everything read off the disc is forgotten and every handle
; refuses until the root is mounted again; the generation is left alone, so a
; drive that keeps answering "no disc" does not look like a disc changing on
; every poll. clobbers: nothing
; -----------------------------------------------------------------------------
cd_changed:
    inc byte [cd_gen]
cd_forget:
    mov byte [cd_valid], 0
    mov byte [cd_stale], 1
    mov word [cd_stag+2], 0xFFFF    ; the sector buffer names no sector
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cd_hcache           ; and the handle cache names no handle
    xor ax, ax
    mov cx, CD_HC_N * CD_HC_SZ / 2
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret
