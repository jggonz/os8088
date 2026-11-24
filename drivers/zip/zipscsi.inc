; =============================================================================
; zipscsi.inc - ZIP.DRV's SCSI layer (SPEC.md 138.3)
;
; One command at a time through whichever transport probed (zt_vec,
; ziptrans.inc), and the three things the transports deliberately do not
; know: when to try again, what a CHECK CONDITION means, and what is in the
; drive. Everything above this file speaks int 13h status bytes, because that
; is what DSV_BLK answers the kernel in (SPEC.md 51.8).
;
; THE WAITS ARE TIME (ziptrans.inc, CLAUDE.md Performance rule 3): BUSY and
; NOT READY "becoming ready" (a disk spinning up) are ridden out a system
; tick at a time, up to ZS_WAIT ticks, and a tick that never comes (IF = 0
; somewhere it should not be) is bounded by a poll count under it -
; atapi.inc's two-bound shape, on OSAPI_GET_TICKS for atapi.inc's reason.
; =============================================================================

ZS_BUFSZ    equ 36              ; INQUIRY's standard answer, the longest asked
ZS_WAIT     equ 182             ; ~10 s of ticks: a Zip spins up in ~3
ZS_UA_N     equ 4               ; UNIT ATTENTIONs a query rides out: a drive
                                ; just powered on reports its reset and then
                                ; the disk, one per command (atapi.inc's
                                ; CD_UA_N, for the same reason)

ZM_UNKNOWN  equ 0               ; zp_med: the volume has not learned the disk
ZM_KNOWN    equ 1               ; ...it has: zp_base/zp_len describe it
ZM_CHANGED  equ 2               ; ...it had, and the disk went or changed
                                ; since - the next DSV_BLK answers 06h

SC_TUR      equ 0x00            ; the seven commands (SPEC.md 138.3)
SC_SENSE    equ 0x03
SC_INQUIRY  equ 0x12
SC_MSENSE   equ 0x1A
SC_STARTSTOP equ 0x1B
SC_CAPACITY equ 0x25
SC_READ10   equ 0x28
SC_WRITE10  equ 0x2A

; -----------------------------------------------------------------------------
; zs_cdb6 - zs_cdb := AL, 0, 0, 0, AH, 0 (and zeros to its tenth byte)
; out: BX = 6 | ZTD_IN << 8, the length and direction zs_cmd wants
; clobbers: BX (the output)
;
; Every six-byte command here is an opcode and one byte at +4 (an allocation
; length, or START STOP's LoEj), and READ CAPACITY is the same with BL = 10.
; -----------------------------------------------------------------------------
zs_cdb6:
    mov [zs_cdb], al
    xor bx, bx
    mov [zs_cdb+1], bx
    mov [zs_cdb+3], bx
    mov [zs_cdb+4], ah
    mov [zs_cdb+5], bx
    mov [zs_cdb+7], bx
    mov [zs_cdb+9], bl
    mov bl, 6
    ret

; -----------------------------------------------------------------------------
; zs_cmd - ONE SCSI command, with the waits, answered as an int 13h status
; in:  [zs_cdb] built, BL = its length, BH = ZTD_IN / ZTD_OUT, ES:DI = the
;      data buffer, CX = the bytes the command moves (0 = none)
; out: CF = 0 done, CX = the bytes moved
;      CF = 1, AL = SPEC.md 138.3's status: AAh not ready / no disk, 10h a
;      medium error, 01h an illegal request, 06h UNIT ATTENTION, 03h write
;      protected, 80h the drive stopped answering, 20h anything else
; clobbers: AX, CX (outputs), flags
;
; WHAT IT ALSO RECORDS, because nothing else sees the answers go by:
;   - a transport error says the drive is gone: [zp_st] = ZIPS_NODRIVE, and a
;     timeout or a phase the drive should never have asked for is followed by
;     the bus reset (Linux ppa.c / imm.c reset on the same two);
;   - NOT READY (but not "becoming ready") or a UNIT ATTENTION while the
;     volume KNOWS the medium means the disk under the kernel's FAT is gone or
;     is another one: [zp_med] = ZM_CHANGED, so the next DSV_BLK says 06h even
;     when it was the page or the door that saw it (SPEC.md 138.4).
; -----------------------------------------------------------------------------
zs_cmd:
    push dx
    push si
    mov dx, cx                  ; the bytes asked, for a retry
    mov [zs_dir], bh
    mov byte [zs_left], ZS_WAIT
    call OSAPI_GET_TICKS
    mov [zs_tick], ax
.again:
    mov cx, dx
    mov si, zs_cdb
    call [zt_vec+ZTV_CMD]       ; CF = 1: AL = a ZTE_*
    jc .terr
    cmp al, 0x02                ; CHECK CONDITION
    je .check
    or al, al                   ; GOOD (CF = 0 off the `or`)
    jz .out
    cmp al, 0x08                ; BUSY: wait and ask again - and a wait
    mov al, 0x80                ; that runs out is a timeout
    je .wait
    mov al, 0x20                ; a status nothing here expects
.fail:
    stc
.out:
    pop si
    pop dx
    ret
.wait:                          ; AL = the answer if the wait runs out
    push ax
    call zs_tickwait            ; CF = 1: the wait is spent
    pop ax
    jc .fail
    mov bh, [zs_dir]
    jmp short .again
.terr:
    mov byte [zp_st], ZIPS_NODRIVE
    cmp al, ZTE_TIMEOUT
    jb .t20                     ; NOCONN / NOSEL: nobody to reset
    call [zt_vec+ZTV_RESET]
    cmp al, ZTE_TIMEOUT
    jne .t20
    mov al, 0x80                ; "timeout", int 13h's own word for it
    jmp short .fail
.t20:
    mov al, 0x20
    jmp short .fail
.check:
    call zs_sense               ; AL = the key's status, AH = the ASC
    jc .fail                    ; ...or REQUEST SENSE itself failed
    or al, al
    jz .out                     ; RECOVERED ERROR: done (CF = 0 off `or`)
    cmp al, 0x06
    je .gone                    ; UNIT ATTENTION
    cmp al, 0xAA
    jne .fail
    cmp ah, 0x04                ; NOT READY, "becoming ready": a disk
    je .wait                    ; spinning up - wait it out (AL = AAh)
    mov byte [zp_st], ZIPS_NODISK
.gone:
    cmp byte [zp_med], ZM_KNOWN
    jne .fail
    mov byte [zp_med], ZM_CHANGED
    jmp short .fail

; -----------------------------------------------------------------------------
; zs_sense - REQUEST SENSE, and its key as an int 13h status (zs_cmd's)
; out: CF = 0: AL = the status (0 = RECOVERED ERROR, which is success), AH =
;      the additional sense code; CF = 1: AL = a status (the command failed)
; clobbers: AX, flags
;
; Into zs_buf in OUR segment, from a CDB of its own: the caller's CDB is still
; wanted for a retry, and the caller's buffer may be nobody's to write here.
; -----------------------------------------------------------------------------
zs_sense:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, zs_buf
    mov si, zs_sncdb
    mov cx, 18
    mov bx, 6 | (ZTD_IN << 8)
    call [zt_vec+ZTV_CMD]
    jc .terr
    or al, al
    mov al, 0x20                ; a REQUEST SENSE that is not GOOD
    stc
    jnz .out
    mov al, [zs_buf+2]
    and al, 0x0F                ; the sense key
    mov bx, zs_keymap
    cmp al, 8
    jb .map
    mov al, 0                   ; keys 8..15: the table's 20h, below
.map:
    xlatb                       ; AL = the status
    mov ah, [zs_buf+12]         ; AH = the ASC
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret
.terr:
    mov al, 0x20
    jmp short .out

zs_sncdb:   db SC_SENSE, 0, 0, 0, 18, 0
zs_keymap:  db 0x20             ; 0 NO SENSE - and 8..15, sent here
            db 0x00             ; 1 RECOVERED ERROR: done
            db 0xAA             ; 2 NOT READY
            db 0x10             ; 3 MEDIUM ERROR
            db 0x20             ; 4 HARDWARE ERROR
            db 0x01             ; 5 ILLEGAL REQUEST
            db 0x06             ; 6 UNIT ATTENTION
            db 0x03             ; 7 DATA PROTECT

; -----------------------------------------------------------------------------
; zs_tickwait - wait for the next system tick, and count it against the bound
; out: CF = 1 the bound [zs_left] is spent; CF = 0 wait and try again
; clobbers: AX, flags
; -----------------------------------------------------------------------------
zs_tickwait:
    push cx
    mov cx, 0xFFFF              ; the net under the tick: polls, not time
.poll:
    call OSAPI_GET_TICKS
    cmp ax, [zs_tick]
    loope .poll
    mov [zs_tick], ax
    pop cx
    sub byte [zs_left], 1       ; CF = 1 on the borrow out of 0
    ret

; -----------------------------------------------------------------------------
; zs_simple - a six-byte command with no data, or with its answer in zs_buf
; in:  AL = the opcode, AH = byte 4, CX = the bytes it returns into zs_buf
; out: zs_cmd's
; clobbers: AX, CX (outputs), flags
;
; zs_go - the same for a CDB already built: BX = zs_cmd's length/direction
; -----------------------------------------------------------------------------
zs_simple:
    push bx
    call zs_cdb6
    call zs_go
    pop bx
    ret
zs_go:
    push di
    push es
    push ds
    pop es
    mov di, zs_buf
    call zs_cmd
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; zs_ident - INQUIRY: is it an Iomega? (attach's, SPEC.md 138.3)
; out: CF = 0 the vendor reads IOMEGA; CF = 1 not ours, or no answer
; clobbers: AX, flags
; -----------------------------------------------------------------------------
zs_ident:
    push bx
    push cx
    push si
    mov ax, SC_INQUIRY | (ZS_BUFSZ << 8)
    mov cx, ZS_BUFSZ
    call zs_simple
    jc .out
    xor bx, bx
.cmp:
    mov al, [zs_buf+8+bx]       ; the vendor field, bytes 8..15
    cmp al, [zs_iomega+bx]
    stc
    jne .out
    inc bx
    cmp bx, 6
    jb .cmp                     ; the sixth matched: CF = 0 off the compare
.out:
    pop si
    pop cx
    pop bx
    ret

zs_iomega:  db 'IOMEGA'

; -----------------------------------------------------------------------------
; zs_query - what is in the drive? (the volume's first step, the page's and
;            ZIPV_INFO's)
; out: CF = 0 a disk: [zp_cap] its sectors, [zp_st] ZIPS_READY or ZIPS_WP;
;      CF = 1 none we can read: AL = the status, [zp_cap] = 0, [zp_st]
;      ZIPS_NODISK, or ZIPS_NODRIVE when the wire said so
; clobbers: AX, flags
;
; TEST UNIT READY riding out ZS_UA_N UNIT ATTENTIONs, READ CAPACITY, then
; MODE SENSE(6) for the write-protect bit - the device-specific byte of the
; header, bit 7 (SPEC.md 138.3); a drive that refuses MODE SENSE is taken as
; writable and the write itself is the test. A block length that is not 512
; is a disk os8088 cannot read, and reads as no disk.
; -----------------------------------------------------------------------------
zs_query:
    push bx
    push cx
    push dx
    mov byte [zp_st], ZIPS_NODISK   ; until the drive says otherwise - and
                                ; zs_cmd says NODRIVE if the wire does
    mov dl, ZS_UA_N
.tur:
    mov ax, SC_TUR
    xor cx, cx
    call zs_simple
    jnc .cap
    cmp al, 0x06
    jne .none
    dec dl
    jnz .tur
    jmp short .none
.cap:
    mov al, SC_CAPACITY
    call zs_cdb6
    mov bl, 10                  ; READ CAPACITY(10): ten bytes, all zero
    mov cx, 8
    call zs_go
    jc .none
    mov ax, [zs_buf+4]          ; the block length, big-endian: 00 00 02 00
    or ax, ax
    jnz .bad
    cmp word [zs_buf+6], 0x0002
    jne .bad
    mov dx, [zs_buf]            ; the LAST LBA, big-endian, + 1
    xchg dl, dh
    mov ax, [zs_buf+2]
    xchg al, ah
    add ax, 1
    adc dx, 0
    mov [zp_cap], ax
    mov [zp_cap+2], dx
    mov ax, SC_MSENSE | (12 << 8)
    call zs_cdb6
    mov byte [zs_cdb+2], 0x3F   ; every page: the header is what is read
    mov cx, 12
    call zs_go
    mov al, ZIPS_READY
    jc .st                      ; refused: writable, and the write will say
    test byte [zs_buf+2], 0x80  ; the device-specific byte's WP bit
    jz .st
    inc ax                      ; ZIPS_WP
.st:
    mov [zp_st], al
    clc
    jmp short .out
.bad:
    mov al, 0xAA
.none:
    xor dx, dx
    mov [zp_cap], dx
    mov [zp_cap+2], dx
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret
