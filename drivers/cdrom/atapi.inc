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
; clobbers: AX - every caller's next step overwrites it (a select, a wait)
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
    mov [cd_tleft], ax
    call OSAPI_GET_TICKS
    mov [cd_tlast], ax
    mov ax, 0xFFFF              ; 256 x 65,536 polls: the high byte is
    mov [cd_ilo], ax            ; spent when the low word borrows, and the
    mov [cd_ihi], al            ; bound is the borrow out of it
    ret

; cd_tmo_up - has the bound run out? out: CF = 1 yes. clobbers: AX - which
; no caller holds across it: cd_wait reads the status again, and the other two
; are counting the bound down and nothing else
cd_tmo_up:
    call OSAPI_GET_TICKS
    cmp ax, [cd_tlast]
    je .same
    mov [cd_tlast], ax
    dec word [cd_tleft]         ; a tick went by: one fewer
    jz .exp
.same:
    sub word [cd_ilo], 1        ; ...and one poll fewer, every call. The
    sbb byte [cd_ihi], 0        ; borrow out of the low word is taken from
    ret                         ; the high byte, and the borrow out of THAT
                                ; is the net: 256 x 65,536 polls (CF = 1)
.exp:
    stc
    ret

; -----------------------------------------------------------------------------
; cd_wait - poll the status until BSY clears, inside the bound cd_tmo_set set
; in:  BX = the base port
; out: CF = 0 and AL = the status; CF = 1 = it never cleared. A floating bus
;      (0FFh) is answered at once as CF = 0, AL = 0FFh, for the caller to
;      refuse - it is never a drive's status (§52.1.3's rule)
;      DX = base + ATA_STAT, which the probe's command write reuses
; clobbers: AX (AL the output), DX - every caller loads DX for its next port
; itself, so the bank this used to make of it protected nothing
; -----------------------------------------------------------------------------
cd_wait:
    lea dx, [bx+ATA_STAT]
.poll:
    in al, dx
    cmp al, 0xFF
    je .ok                      ; CF = 0 off the equal compare...
    test al, ATA_ST_BSY
    jz .ok                      ; ...or off the `test`
    call cd_tmo_up
    jnc .poll
.ok:
    ret                         ; CF = 1 only out of cd_tmo_up: the bound

; -----------------------------------------------------------------------------
; cd_select - point the task file at our unit (internal)
; in:  BX = base, CL = unit 0/1
; clobbers: AX, CL, DX, flags - and falls into cd_delay
;
; cd_delay - ~400 ns after a select or a command: four reads of the
; alternate status, which acknowledge nothing (internal). BX = base
; clobbers: AL, DX
;
; NEITHER BANKS A REGISTER, because every call of either is followed by
; cd_wait, which loads DX and AL itself - and CL is the unit, which no caller
; reads again (cd_ident banks CX whole, and cd_packet reloads it).
; -----------------------------------------------------------------------------
cd_select:
    lea dx, [bx+ATA_DRVH]
    mov al, 0xA0
    shr cl, 1                   ; the unit's bit, into CF
    jnc .d0
    mov al, 0xB0
.d0:
    out dx, al
cd_delay:
    lea dx, [bx+ATA_ALT]
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    ret

; =============================================================================
; THE PROBE (SPEC.md 119.2)
; =============================================================================

cd_bases:   dw 0x168, 0x1E8, 0x170, 0x1F0   ; BACKWARDS: cd_probe walks it down
CD_NBASE    equ 4                           ; from the end, 1F0h first

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
    mov si, (CD_NBASE - 1) * 2  ; the table's LAST row is 1F0h
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
    xor cl, 1                   ; unit 0, then 1, then back to 0 and out
    jnz .unit
.nextbase:
    sub si, 2                   ; the borrow past the first row is CF = 1:
    jnc .base                   ; "none", with nothing more to say
    jmp short .out
.found:
    mov [cd_base], bx           ; CF = 0 off the `jnc`
    mov [cd_unit], cl
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
; clobbers: AX, DX, ES, flags
;
; An ATA disk aborts A1h; an absent device never raises DRQ inside
; CD_PROBE_T; an ATAPI device that is not a CD-ROM (a tape, a ZIP) answers
; with another type in word 0. All 256 words are drained whatever they say -
; the drive holds DRQ until they are.
; -----------------------------------------------------------------------------
cd_ident:
    push cx
    push di
    mov ax, CD_PROBE_T
    call cd_tmo_set
    call cd_select
    call cd_wait                ; DX = the status port, on every answer
    jc .no
    cmp al, 0xFF
    je .no
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
    in ax, dx                   ; word 0, ON THE STACK across the drain
    push ax
    mov cx, 26                  ; words 1-26, dropped
.skip:
    in ax, dx
    loop .skip
    push ds                     ; ES = ours for the model, and NOT put back:
    pop es                      ; the one caller is DRVV_ATTACH, and the
                                ; drv_stamped that sent it restores ES itself
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
                                ; (cd_model+40, the NUL, is 0 from the load
                                ; and nothing writes it: the 20 words above
                                ; are bytes 0-39, and the trim writes zeros)
    pop ax                      ; word 0
    and ah, 0xDF                ; bits 15-14 = 10b (ATAPI) and 12-8 = 5 (a
    cmp ah, 0x85                ; CD-ROM): bit 13 is reserved
    jne .no
    and al, 3                   ; the packet's size: 0 = 12 bytes, 1 = 16,
    cmp al, 1                   ; and 2 or 3 a size no ATAPI version defines
    ja .no
    shl al, 1
    add al, 6
    cbw                         ; 6 or 8 WORDS - and AH = 0, the high byte the
    mov [cd_pkw], ax            ; trim below stops on
    mov di, cd_model + 39       ; the model is padded with spaces: trim them,
.tr:                            ; down to cd_pkw's high byte if it is ALL
    cmp byte [di], ' '          ; spaces - which is never one
    jne .ok
    mov byte [di], 0
    dec di
    jmp short .tr
.ok:
    CLC_OR_STC .no              ; CF = 0 here, CF = 1 at .no: cd_probe asks
                                ; for nothing but CF
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
; clobbers: AX, BX, CX, SI, DI, ES, flags - DX is kept
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
    push dx                     ; DX alone: cd_cmd banks BX, CX, SI, DI and ES
                                ; round this already, and cd_sense - the one
                                ; other caller - is reached only from inside
                                ; cd_cmd. DX is the one its callers keep
    xor ax, ax
    mov [cd_xgot], ax
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
    out dx, al                  ; CD_BCLIM's low byte - the 0 still in AL
    inc dx
    mov al, CD_BCLIM >> 8
    out dx, al
    inc dx                      ; BCH + 2 = ATA_STAT
    inc dx
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
    jz .out                     ; done - and CF = 0 off the `test`
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
.err:
    lea dx, [bx+ATA_ERR]
    in al, dx
    mov cl, 4
    shr al, cl                  ; the sense key, 0 = none given
    stc
    pop dx
    ret
.empty:                         ; DRQ with a count of 0 (or FFFFh): nothing to
    call cd_tmo_up              ; move, and BSY is clear, so cd_wait would not
    jnc .phase                  ; count it - the bound is counted here instead
.tmo:
    mov al, CD_TMO
    stc
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; cd_pclear - [cd_pkt] all zero, and no data phase (internal)
; out: DI = cd_pkt; clobbers: CX - AX is KEPT, so a caller with an LBA or a
;      verb in it need not bank it across (cd_read10, cd_v_play, cd_pausebit)
;
; ONE FILL: [cd_xmax] and [cd_xout] sit straight behind the packet
; (cdrom.asm), so ten words clear all three. [cd_xseg]:[cd_xoff] are NOT in
; it and must not be: a drive that asks for data on a command that has none
; is answered out of whatever they last named, which is always a real segment.
; -----------------------------------------------------------------------------
cd_pclear:
    mov di, cd_pkt
    push di
    mov cx, (cd_xout + 2 - cd_pkt) / 2
    call cd_wzero
    pop di
    ret

; cd_wzero - CX words of zero at DS:DI (internal). out: DI past them, CX = 0;
;            AX and ES kept
cd_wzero:
    push ax
    push es
    push ds
    pop es
    xor ax, ax
    cld
    rep stosw
    pop es
    pop ax
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
    mov word [cd_tries], CD_READY_N | (CD_UA_N << 8)  ; and [cd_uatry]
.again:
    call cd_packet
    jnc .out
    mov ah, [cd_pkt]            ; the last refusal, for a diagnosis: the key
    mov [cd_lkey], ax           ; and the command it answered (CDV_DRIVE's
                                ; registers can be read off a live machine) -
                                ; [cd_lop] being the byte after [cd_lkey]
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
    mov al, CDE_NOAUDIO         ; none, the one place it is expected
    jmp short .fail
    ; THE REFUSALS ARE A CHAIN: each `db 0x3D` is `cmp ax, imm16`, whose
    ; immediate is the next two-byte `mov al, CDE_*` - so falling in runs the
    ; compare (flags only, and stc follows) and a jump to the label runs the
    ; `mov`. Every CDE_* is under 80h, so cbw makes it the whole of AX.
.nodisc:
    call cd_forget              ; no disc is a disc we do not know either
    mov al, CDE_NODISC
    db 0x3D
.notready:
    mov al, CDE_NOTREADY
    db 0x3D
.io:
    mov al, CDE_IO
.fail:
    cbw
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
; clobbers: AX, BX, CX, SI, DI, ES, flags - its one caller is cd_cmd, which
;      banks all of them but AX round everything it does
; -----------------------------------------------------------------------------
cd_sense:
    mov si, cd_pkt              ; bank the packet and the transfer
    mov di, cd_pkt2
    mov cx, 8 + 4               ; 16 bytes of packet, then xmax/xout/xoff/xseg
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
    mov si, cd_pkt2
    mov di, cd_pkt
    mov cx, 8 + 4
                                ; ...and the bank back, which is the tail
                                ; call: cd_wcopy keeps AX
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
    mov word [cd_valid], 0x0100 ; [cd_valid] = 0 and [cd_stale] = 1
    mov word [cd_stag+2], 0xFFFF    ; the sector buffer names no sector
    push cx
    push di
    mov di, cd_hcache           ; and the handle cache names no handle
    mov cx, CD_HC_N * CD_HC_SZ / 2
    call cd_wzero
    pop di
    pop cx
    ret
