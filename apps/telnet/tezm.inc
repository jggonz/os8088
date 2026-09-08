; =============================================================================
; os8088 - apps/telnet/tezm.inc
;
; ZMODEM RECEIVE (SPEC.md 70.11)
;
; Receive only. Uploads are out of scope and docs/TELNET-PLAN.md carries the
; arithmetic. The transfer is started by SPEC.md 70.9.6's auto-start - the six
; bytes `**\x18B00` in GROUND, which is a hex ZRQINIT header's opening - or by
; Session > Receive File (Zmodem), which sends a ZRINIT and waits.
;
; --- THREE THINGS DECIDE THE SHAPE OF EVERYTHING BELOW -----------------------
;
; 1. **EVERY BYTE ARRIVES SEPARATELY, AND MAY BE THE LAST ONE FOR A WHILE.**
;    tz_byte is a state machine over ONE byte with not a single byte of state
;    in a local, which is teansi.inc's property one layer down and for the same
;    reason: TCP splits a header in half whenever it feels like it, and a
;    receiver that reads a header with a loop is a receiver that works against
;    a pipe and fails against a cable.
;
; 2. **A WORKER MAY NOT TOUCH A FILE** (SPEC.md 20.6 rule 7). The file slots
;    are the UI task's, because they share dsk_secbuf, the FAT snapshot and
;    sch_lock. So the worker STAGES and the UI task COMMITS, over one byte -
;    [tz_req] - written last by the producer and cleared last by the consumer.
;    apps/ftpd (SPEC.md 77) is the worked example and this follows it.
;
; 3. **THE TELNET LAYER IS STILL ABOVE THIS.** te_byte hands us application
;    bytes, so an `IAC IAC` on the wire is one 0xFF here - which is not
;    cosmetic, because a data subpacket contains every byte value and a
;    receiver that forgot would corrupt every download with an 0xFF in it while
;    agreeing perfectly with a sender that also forgot.
;
; WHAT IS NOT HERE. CRC-32 and ZBIN32 in either direction (SPEC.md 70.11.1 -
; a table is 1,024 bytes of a package on a floppy with four clusters left, and
; the untabled form is 32-bit arithmetic on an 8086, for a link TCP and
; Ethernet have both already checksummed); ZSINIT, which this end never sends
; and never needs; and uploads.
; =============================================================================

; --- the protocol's own bytes ------------------------------------------------
ZPAD        equ 0x2A
ZDLE        equ 0x18
ZBIN        equ 'A'                 ; a binary header: ONE ZPAD
ZHEX        equ 'B'                 ; ...a hex one: TWO
ZBIN32      equ 'C'                 ; refused with ZNAK (SPEC.md 70.11.1)
ZXON        equ 0x11

ZF_RQINIT   equ 0
ZF_RINIT    equ 1
ZF_ACK      equ 3
ZF_FILE     equ 4
ZF_SKIP     equ 5
ZF_NAK      equ 6
ZF_FIN      equ 8
ZF_RPOS     equ 9
ZF_DATA     equ 10
ZF_EOF      equ 11

ZCRCE       equ 0x68                ; the frame is over, say nothing
ZCRCG       equ 0x69                ; it continues, say nothing
ZCRCQ       equ 0x6A                ; it continues, ZACK the position
ZCRCW       equ 0x6B                ; it is over, ZACK the position
ZRUB0       equ 0x6C                ; a literal 0x7F
ZRUB1       equ 0x6D                ; ...and a literal 0xFF

CANFDX      equ 0x01                ; what ZRINIT advertises, and CANFC32 is
CANOVIO     equ 0x02                ; deliberately NOT among it

; --- the receiver's states (SPEC.md 70.11.2) --------------------------------
ZR_OFF      equ 0
ZR_WAIT     equ 1                   ; ZRINIT sent, waiting for a header
ZR_FILE     equ 2                   ; collecting the ZFILE info subpacket
ZR_DLG      equ 3                   ; HELD: nothing is sent until the UI answers
ZR_DATA     equ 4                   ; ZDATA subpackets into the staging halves
ZR_EOF      equ 5                   ; the file is closed
ZR_SKIP     equ 6                   ; ZSKIP sent
ZR_FIN      equ 7                   ; ZFIN sent, waiting for the sender's `OO`
ZR_ERR      equ 8                   ; the retry count is spent
ZR_DRAIN    equ 9                   ; **the transfer is over and the SENDER MAY
                                    ; NOT KNOW YET.** Every byte is swallowed
                                    ; here rather than handed to the ANSI
                                    ; parser, which would otherwise paint the
                                    ; board's screen with the file - `ESC[`
                                    ; sequences and all, since binary data is
                                    ; full of them (the w4 review's MAJOR 5)

; --- and the BYTE-LEVEL state, which is a different question ----------------
; [tz_st] is where the protocol is; [tz_ps] is where the parser is inside a
; frame. Two bytes because they answer two questions: a ZFILE header and a
; ZDATA header put the parser in the same place (collecting a subpacket) and
; the protocol in different ones.
ZP_IDLE     equ 0                   ; scanning for a ZPAD
ZP_PAD      equ 1                   ; one or more ZPADs seen
ZP_FMT      equ 2                   ; ...and the ZDLE: A, B or C follows
ZP_HEX      equ 3                   ; fourteen hex digits
ZP_HEOL     equ 4                   ; ...and the CR LF behind them
ZP_BIN      equ 5                   ; seven ZDLE-decoded header bytes
ZP_DATA     equ 6                   ; subpacket data
ZP_DCRC     equ 7                   ; ...and its two CRC bytes
ZP_OO       equ 8                   ; after our ZFIN: the sender's `OO`

; --- where a decoded data byte goes -----------------------------------------
TZS_NONE    equ 0                   ; swallowed (a subpacket we are not for)
TZS_INFO    equ 1                   ; the ZFILE info block
TZS_STAGE   equ 2                   ; the staging halves

; --- the handshake with the UI task (SPEC.md 70.11.3) -----------------------
TZ_NONE     equ 0
TZ_NAME     equ 1                   ; DFREE, size the chunk, open the Save dialog
TZ_DATA     equ 2                   ; write [tz_un] bytes of half [tz_uh]
TZ_DONE     equ 3                   ; the transfer is over: put the terminal back

TZR_NONE    equ 0                   ; ...and what came back
TZR_OK      equ 1
TZR_CANCEL  equ 2
TZR_BIG     equ 3                   ; the destination's cluster is over 8,192
TZR_ERR     equ 4                   ; a file slot refused

TZ_STGSZ    equ 8192                ; the staging area, two 4,096-byte halves
TZ_HALF     equ 4096
TZ_INFOSZ   equ 64                  ; the ZFILE info block: only the NAME and
                                    ; the SIZE are read (SPEC.md 70.11.2), and
                                    ; the rest is CRC'd and dropped. A longer
                                    ; one is still checked, still answered and
                                    ; still mangled - it is the TAIL that is
                                    ; discarded, and the tail is mtime, mode,
                                    ; serial and two counters this terminal has
                                    ; no opinion about
TZ_TMO      equ 182                 ; ten seconds at 18.2 Hz (SPEC.md 70.11.2)
TZ_DRAINQ   equ 36                  ; ...and two seconds of QUIET is what ends a
                                    ; drain. Long enough that a sender still
                                    ; sending keeps it open, short enough that
                                    ; the terminal comes back while the user is
                                    ; still reading why it stopped
TZ_CANS     equ 10                  ; the cancel this end sends: five CANs and
                                    ; five backspaces, which is what every
                                    ; Zmodem implementation reads as an abort
TZ_TRIES    equ 5                   ; ...five of those in a row is ZR_ERR
TZ_DLGTMO   equ 1092                ; sixty seconds: the backstop under the two
                                    ; cancel INFERENCES, and not the mechanism
TZ_DLGARM   equ 18                  ; ...and one second before a W_PAINT counts
                                    ; as one of them, which is what tells the
                                    ; dialog's own arrival repaint from its
                                    ; departure repaint
TZ_KICK     equ 9                   ; ...and how often the worker kicks the UI
                                    ; while a dialog is up, so that backstop is
                                    ; reached on the UI task where every write
                                    ; to [tz_req] belongs
TZ_DTRY     equ 20                  ; ...and how many times a REFUSED
                                    ; OSAPI_FILE_DLG is asked again before the
                                    ; transfer gives up on it. About ten
                                    ; seconds at TZ_KICK, and the refusal it is
                                    ; for - "one is already up" - is the
                                    ; previous file's dialog waiting to be
                                    ; reaped (SPEC.md 38.1.1)

; =============================================================================
; THE CRC, BITWISE AND WITH NO TABLE (SPEC.md 70.11.1)
;
; Eight shifts a byte is about 80 cycles on a 4.77 MHz 8088 - 59,500 bytes a
; second of checking capacity against a cable that delivers 3,741. A 512-byte
; table would buy throughput this transfer cannot use, on a package whose
; floppy has four clusters left.
;
; **AND THE AUGMENTATION IS NOT NEEDED.** lrzsz spells its update
; `tab[(crc>>8)] ^ (crc<<8) ^ b` and then feeds two zero bytes at the end,
; which puts the byte in at the LOW end and shifts it through; the plain
; MSB-first form below with no trailing zeros is the same function, and
; tools/os88bbs.py's own selfcheck pins both against `binascii.crc_hqx`.
; =============================================================================
; --- tz_crcb - AL through [tz_crc] ------------------------------------------
tz_crcb:
    push ax
    push bx
    push cx
    mov bx, [tz_crc]
    xor bh, al                      ; crc ^= b << 8
    mov cx, 8
.bit:
    shl bx, 1
    jnc .no
    xor bx, 0x1021
.no:
    loop .bit
    mov [tz_crc], bx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; STARTING AND STOPPING
; =============================================================================

; -----------------------------------------------------------------------------
; tz_start - SPEC.md 70.9.6's handover, called by te_zdetect from the parser
;
; It replaces the stub teansi.inc carried through wave 3. [te_zon] is what stops
; the parser being fed: te_byte sends every later application byte here instead.
; -----------------------------------------------------------------------------
tz_start:
    push ax
    mov ax, [te_soff]
    mov [te_zat], ax                ; the stream offset the receiver takes over,
                                    ; which is ansisim's `zmodem_at` to the byte
    inc byte [te_zn]                ; ...and how many times it has fired, which
                                    ; is what a gate reads to tell "fired once"
    pop ax                          ; from "fired on every star of some art"
    call tz_begin
    ret

; -----------------------------------------------------------------------------
; tz_begin - a receive session, from the trigger or from the menu item
; in:  the WORKER's task (the wire is the worker's, SPEC.md 70.2)
; -----------------------------------------------------------------------------
tz_begin:
    push ax
    push cx
    push di
    push es
    push ds
    pop es                          ; ...ES is the KERNEL's on a callback
    cld
    mov di, tz_st
    mov cx, TZ_CTL
    xor al, al
    rep stosb                       ; one `rep stosb` over one contiguous run,
                                    ; so a state byte added later is covered by
                                    ; construction and not by somebody
                                    ; remembering (te_reset's own shape)
    mov byte [te_zon], 1
    mov byte [tz_st], ZR_WAIT
    mov byte [tz_pan], 1            ; the progress takeover, put back by the
                                    ; paint that follows TZ_DONE
    mov word [tz_chunk], TZ_HALF    ; ...the safe assumption until DFREE says
    mov byte [tz_dbl], 1            ; otherwise, which is every floppy
    call tz_touch
    call tz_rinit
    mov byte [te_dirty], 1
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_end - the terminal comes back (SPEC.md 70.11.5)
;
; The BUFFER was never touched, so putting the terminal back is the repaint the
; takeover already owed. TZ_DONE is what asks the UI task for it: this proc runs
; on the worker and the panel is the UI's to remove.
; -----------------------------------------------------------------------------
tz_end:
    push ax
    push bx
    mov byte [te_zon], 0
    mov byte [tz_st], ZR_OFF
    mov byte [tz_ps], ZP_IDLE
    mov bx, TE_ROWS - 1
    call te_mark                    ; ...and the row the FULL-SCREEN progress
                                    ; line was written over, so it is re-emitted
                                    ; from the buffer (SPEC.md 70.11.5)
    call te_markall
    cmp byte [tz_req], TZ_NONE
    jne .nopost                     ; **AND THERE IS NO NEXT PASS.** [te_zon] is
                                    ; already clear above, and te_step only
                                    ; calls tz_poll while it is set - so a
                                    ; transfer that ended with a commit
                                    ; outstanding left the panel owning the
                                    ; terminal's content for the rest of the
                                    ; session, with the session live underneath
                                    ; it (the w4 review's MINOR 4). Removing the
                                    ; panel is the UI task's ONLY job in
                                    ; TZ_DONE, and clearing the byte is a byte
                                    ; store: this end can do it itself. And
                                    ; posting TZ_DONE over an outstanding
                                    ; TZ_DATA would be worse than useless -
                                    ; tz_post writes [tz_req] flat, so the
                                    ; commit would be LOST.
    mov al, TZ_DONE
    call tz_post
    jmp short .out
.nopost:
    mov byte [tz_pan], 0
    call te_markall
.out:
    mov byte [te_dirty], 1
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_fail - SI = a reason string; the transfer is over, and BOTH ENDS KNOW
;
; **IT USED TO SEND NOTHING** (the w4 review's MAJOR 5). The sender was mid-file
; and had no idea; it went on transmitting ZDATA subpackets, and with [te_zon]
; clear te_byte handed every one of those bytes to the ANSI parser - so the
; status line said `Disk error` while the screen filled with CP437 noise and
; whatever `ESC[` sequences fell out of binary data cleared it, moved the cursor
; and changed the colours until the sender's own timeout expired.
;
; So: the standard cancel goes out first, and the receiver stays on the stream
; in ZR_DRAIN until it has been quiet for TZ_DRAINQ. The terminal comes back
; when the wire does, not when the sender happens to give up.
; -----------------------------------------------------------------------------
tz_fail:
    push ax
    push bx
    push cx
    push si
    mov [tz_why], si
    mov si, tz_cancels
    mov cx, TZ_CANS
    call te_reply                   ; ...whole or not at all (SPEC.md 70.10.3)
    pop si
    pop cx
    mov [tz_why], si
    mov byte [tz_st], ZR_DRAIN
    mov byte [tz_ps], ZP_IDLE
    mov byte [tz_pan], 0            ; the panel comes off NOW: the reason is on
    call te_markall                 ; the status line and the terminal is what
                                    ; the user wants back (SPEC.md 70.11.5)
    mov bx, TE_ROWS - 1
    call te_mark
    call tz_touchq
    mov byte [te_dirty], 1
    pop bx
    pop ax
    ret

tz_cancels: db 0x18, 0x18, 0x18, 0x18, 0x18, 8, 8, 8, 8, 8

; --- tz_touch - the clock this transfer is measured against ------------------
; OSAPI_GET_TICKS is 18.2 Hz and wraps at 65,536, so every comparison below is
; a SUBTRACTION and never a `cmp` against an absolute (SPEC.md 70.11.2).
tz_touch:
    push ax
    call OSAPI_GET_TICKS
    mov [tz_tick], ax
    mov byte [tz_try], 0
    pop ax
    ret

; =============================================================================
; SENDING - every header this end composes, and all of them go WHOLE
;
; te_reply is SPEC.md 70.10.3's enqueue-or-hold: half a Zmodem header on the
; wire is a header the sender NAKs for ever, which is the case that routine was
; written for. A hex header is 21 bytes and te_pnd is 32.
; =============================================================================

; --- tz_hex - AL = the frame type, tz_ohdr = its four header bytes -----------
tz_hex:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tz_ob
    mov byte [di], ZPAD
    mov byte [di+1], ZPAD           ; **TWO ZPADs on a hex header** and one on
    mov byte [di+2], ZDLE           ; a binary one (SPEC.md 70.11.1)
    mov byte [di+3], ZHEX
    add di, 4
    mov word [tz_crc], 0
    mov dl, al                      ; DL = the frame type, kept past the CRC
    call tz_crcb
    call tz_hex2                    ; the type, two hex digits
    mov si, tz_ohdr
    mov cx, 4
.h:
    mov al, [si]
    inc si
    call tz_crcb
    call tz_hex2
    loop .h
    mov ax, [tz_crc]
    mov al, ah                      ; the CRC goes out HIGH byte first
    call tz_hex2
    mov ax, [tz_crc]
    call tz_hex2
    mov byte [di], 0x0D
    mov byte [di+1], 0x0A
    add di, 2
    cmp dl, ZF_FIN
    je .nox                         ; **NO XON ON ZFIN OR ZACK**, which is
    cmp dl, ZF_ACK                  ; lrzsz's own behaviour and what the sender
    je .nox                         ; expects to read next
    mov byte [di], ZXON
    inc di
.nox:
    mov si, tz_ob
    mov cx, di
    sub cx, si
    call te_reply
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_hex2 - AL as two lower-case hex digits at DI -------------------------
tz_hex2:
    push ax
    push bx
    push cx
    mov bl, al
    mov cl, 4
    shr al, cl                      ; `shr reg, CL` is 8086; `shr reg, imm` is
    call .dig                       ; not (CLAUDE.md)
    mov al, bl
    and al, 0x0F
    call .dig
    pop cx
    pop bx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 'a' - '0' - 10
.st:
    mov [di], al
    inc di
    ret

; --- tz_ohdr32 - put the dword at BX into the outgoing header, little endian --
tz_ohdr32:
    push ax
    mov ax, [bx]
    mov [tz_ohdr + 0], al
    mov [tz_ohdr + 1], ah
    mov ax, [bx+2]
    mov [tz_ohdr + 2], al
    mov [tz_ohdr + 3], ah
    pop ax
    ret

; --- tz_zero4 - ...and the four zeros every flag frame but ZRINIT sends -------
tz_zero4:
    mov word [tz_ohdr + 0], 0
    mov word [tz_ohdr + 2], 0
    ret

; -----------------------------------------------------------------------------
; tz_rinit - what this terminal advertises (SPEC.md 70.11.1)
;
; A FLAG frame's four header bytes are ZF3 ZF2 ZF1 ZF0 - NOT the order they
; read - so hdr[3] is ZF0 and that is where CANFDX|CANOVIO goes. hdr[0..1] is
; the receive buffer size, little endian, and **this terminal sends 0**: no
; window, so the sender streams with ZCRCG and ends each file with a ZCRCW it
; waits on. The back-pressure is TCP's, which is the only place on this machine
; it can usefully live (SPEC.md 70.11.3).
; -----------------------------------------------------------------------------
tz_rinit:
    push ax
    call tz_zero4
    mov byte [tz_ohdr + 3], CANFDX | CANOVIO
    mov al, ZF_RINIT
    mov [tz_lst], al
    call tz_hex
    pop ax
    ret

; --- tz_say - AL = a frame type with an all-zero header ----------------------
tz_say:
    push ax
    mov [tz_lst], al
    call tz_zero4
    call tz_hex
    pop ax
    ret

; --- tz_saypos - AL = a frame type carrying the dword at BX ------------------
tz_saypos:
    push ax
    mov [tz_lst], al
    call tz_ohdr32
    call tz_hex
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_resend - the last header again, on a timeout
;
; ZRINIT is the one that carries a payload, so it is composed rather than
; replayed; everything else this end sends is a type and a position.
; -----------------------------------------------------------------------------
tz_resend:
    push ax
    push bx
    mov al, [tz_lst]
    cmp al, ZF_RINIT
    jne .other
    call tz_rinit
    jmp short .out
.other:
    cmp al, ZF_RPOS
    je .pos
    cmp al, ZF_ACK
    je .pos
    call tz_say
    jmp short .out
.pos:
    mov bx, tz_pos
    cmp al, ZF_ACK
    jne .go
    mov bx, tz_rcv                  ; an ACK names what ARRIVED and a ZRPOS what
.go:                                ; was COMMITTED: two different numbers
    call tz_saypos
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE WORKER'S PASS - timeouts, the UI's answer, and a deferred send
;
; Called once per te_step while the receiver has the stream. Everything here is
; something that has to happen WITHOUT a byte arriving, which is exactly the
; set of things a byte-driven state machine cannot do for itself.
; =============================================================================
tz_poll:
    push ax
    push bx
    push cx
    push dx
    push si
    ; --- the menu item, which sets a byte rather than touching the wire -----
    ; Session > Receive File runs on the UI task and the wire is the WORKER's
    ; (SPEC.md 70.2), so it asks here. te_pnd in particular has ONE writer.
    cmp byte [tz_want], 0
    je .nowant
    mov byte [tz_want], 0
    cmp byte [te_state], TS_UP
    jne .nowant
    cmp byte [te_zon], 0
    jne .nowant                     ; already receiving: a second ask is not a
    call tz_begin                   ; second transfer
.nowant:
    cmp byte [te_zon], 0
    je .out

    ; --- the UI task's answer ----------------------------------------------
    cmp byte [tz_req], TZ_NONE
    jne .noans
    mov al, [tz_rst]
    cmp al, TZR_NONE
    je .noans
    mov byte [tz_rst], TZR_NONE
    cmp al, TZR_OK
    je .chosen
    cmp al, TZR_BIG
    je .toobig
    cmp al, TZR_ERR
    je .diskerr
    ; TZR_CANCEL: ZSKIP, and the sender goes on to the next file
    mov byte [tz_st], ZR_SKIP
    mov al, ZF_SKIP
    call tz_say
    call tz_touch
    jmp .noans                      ; NEAR: the FERR ladder below put `.noans`
                                    ; past a short jump's reach
.chosen:
    cmp byte [tz_st], ZR_DLG
    jne .noans                      ; a stale answer to a transfer that ended
    mov word [tz_pos], 0
    mov word [tz_pos+2], 0
    mov word [tz_rcv], 0
    mov word [tz_rcv+2], 0
    mov word [tz_fill], 0
    mov byte [tz_half], 0
    mov byte [tz_made], 0
    mov word [tz_sub0], 0           ; **PER FILE.** tz_begin clears it once a
    mov word [tz_sub0+2], 0         ; SESSION, and `.repos` is reachable before
    mov word [tz_skip], 0           ; the first ZDATA of the second file records
                                    ; it - so without this the ZRPOS would name
                                    ; the PREVIOUS file's subpacket offset
    mov word [tz_why], 0            ; ...and the last file's refusal, which has
                                    ; been on the status line since it happened
    mov byte [tz_st], ZR_DATA
    mov bx, tz_pos
    mov al, ZF_RPOS
    call tz_saypos                  ; **ZRPOS 0**: start at the beginning, which
    call tz_touch                   ; is the only offset this end can name
    jmp .noans
.toobig:
    ; **ZSKIP, AND THE TRANSFER GOES ON** - SPEC.md 70.11.3's table says
    ; "refused: ZSKIP", and calling tz_fail here ended the whole batch for one
    ; unsuitable destination. The cluster is a per-FILE fact and the next file
    ; may be saved somewhere else.
    ;
    ; **AND THE SENTENCE IS COMPOSED ON THE UI TASK, not here.** tz_bigmsg
    ; calls tz_num, which writes tz_dig - and tz_text calls it too, from
    ; te_status under the gfx lock. Composing here would have put a second TASK
    ; on that scratch for the first time. tz_chunk_set already knows the cluster
    ; is too big and already runs on the UI task, so it composes the sentence
    ; there and this arm only points at it.
    mov si, tz_bmsg
    mov [tz_why], si
    mov byte [tz_st], ZR_SKIP
    mov al, ZF_SKIP
    call tz_say
    call tz_touch
    mov byte [te_dirty], 1
    jmp .noans
.diskerr:
    ; **WHICH REFUSAL** (SPEC.md 47, SPEC.md 54.4.1's rule one application
    ; along). `Disk error` is what every one of them looks like from the
    ; outside, and the gate found the difference the hard way: a folder that
    ; cannot take another entry is FERR_DIRFULL and is a different thing to go
    ; and do about than a disk with no clusters left.
    mov si, tz_s_derr
    cmp byte [tz_ferr], FERR_DIRFULL
    jne .d1
    mov si, tz_s_dfull
    jmp short .dsay
.d1:
    cmp byte [tz_ferr], FERR_FULL
    jne .d2
    mov si, tz_s_full
    jmp short .dsay
.d2:
    cmp byte [tz_ferr], FERR_WPROT
    jne .d3
    mov si, tz_s_wprot
    jmp short .dsay
.d3:
    cmp byte [tz_ferr], FERR_PROT
    jne .dsay
    mov si, tz_s_prot
.dsay:
    call tz_fail
    jmp .out                        ; NEAR: the ladder above pushed `.out` past
                                    ; a short jump's reach
.noans:

    ; --- the SINGLE-buffered raise (the w4 review's MAJOR 1) ----------------
    ; With one buffer there is no "other half" to fill through a commit, so
    ; tz_canrx refuses at a full buffer and the raise cannot be driven by a byte
    ; arriving - it happens here instead, on a pass where nothing is out with
    ; the UI task. The bytes wait in te_rx for one worker pass, which is what
    ; TCP back-pressure already is (SPEC.md 70.11.3).
    cmp byte [tz_dbl], 0
    jne .nosingle
    cmp byte [tz_req], TZ_NONE
    jne .nosingle
    mov ax, [tz_fill]
    cmp ax, [tz_chunk]
    jb .nosingle
    call tz_raise
.nosingle:

    ; --- a header we owe but could not send until a commit landed -----------
    ; SPEC.md 70.11.2: **ZRPOS restarts at the bytes COMMITTED, not the bytes
    ; received**, and the half in flight may or may not have reached the disk
    ; yet - so the honest offset is not known until [tz_req] clears. One byte
    ; of "a header is owed" is cheaper than a state, and the wait is at most
    ; one commit.
    cmp byte [tz_owe], 0
    je .noowe
    cmp byte [tz_req], TZ_NONE
    jne .noowe
    mov al, [tz_owe]
    mov byte [tz_owe], 0
    cmp al, 2
    je .owepos
    call tz_rinit                   ; 1: the ZRINIT that answers a ZEOF
    call tz_touch
    jmp short .noowe
.owepos:
    ; **THE RESUME IS `min([tz_pos], [tz_sub0])`, AND THE FIRST VERSION HAD THE
    ; INEQUALITY BACKWARDS.** [tz_pos] is the honest count of bytes on the disk
    ; and [tz_sub0] is where the subpacket now filling began; a straddling
    ; subpacket has part of ITSELF committed, so THAT case wants the ZRPOS at
    ; [tz_sub0] with the overlap skipped. But `sub0 > pos` is the NORMAL case,
    ; not the exception - with 1,024-byte subpackets and a 4,096-byte chunk,
    ; three subpacket starts in every four sit ABOVE the committed offset - and
    ; `sub 4096-6144` is 0xF800, so the old code named an offset PAST THE END OF
    ; THE FILE and then silently dropped 63,488 bytes while advancing [tz_rcv]
    ; past the hole. The ZEOF check still matched. Strictly worse than the
    ; defect it was fixing, and the same shape of unchecked premise as the
    ; divide in tz_frac.
    ;
    ; So: resume at whichever is LOWER, and skip only what is provably already
    ; written. The file's SIZE never moves either way, which is what keeps
    ; OSAPI_FILE_APPEND's cluster-multiple precondition true through a retry.
    push si
    push di
    mov word [tz_fill], 0           ; whatever was still filling is DISCARDED
    mov si, tz_sub0
    mov di, tz_pos
    call tz_cmp32
    jb .straddle                    ; sub0 < pos: part of this subpacket is on
    mov word [tz_skip], 0           ; the disk already
    mov si, tz_pos                  ; sub0 >= pos: [tz_pos] IS the honest resume
    jmp short .rp                   ; and nothing is skipped
.straddle:
    mov ax, [tz_pos]
    sub ax, [tz_sub0]               ; < one subpacket, so one word is enough
    mov [tz_skip], ax
    mov si, tz_sub0
.rp:
    mov di, tz_rcv
    call tz_cp32
    mov bx, si
    pop di
    pop si
    mov al, ZF_RPOS
    call tz_saypos
    call tz_touch
.noowe:

    ; --- the dialog's kick, so the 60-second backstop runs on the UI task ---
    ; --- **EVERY OUTSTANDING REQUEST IS KICKED FOR, not just TZ_NAME.**
    ; OSAPI_WM_WAKE answers CF=1 when the event ring was full of other events
    ; and nothing was posted, and its own cell says to kick again from the next
    ; callback - so a TZ_DATA whose wake was lost left the worker polling a byte
    ; the UI task was never told to clear. The transfer then stopped dead: the
    ; ZRINIT that answers a ZEOF waits for that commit, so the sender saw
    ; nothing for TZ_TMO x TZ_TRIES and the terminal ended in `Sender stopped`
    ; with the file half written. This periodic kick IS the retry the SDK asks
    ; for, and it costs one compare more than the version that only covered the
    ; dialog.
    cmp byte [tz_dlg], 0
    jne .kick
    cmp byte [tz_req], TZ_NONE
    je .nodlg
.kick:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [tz_kick]
    cmp ax, TZ_KICK
    jb .nodlg
    mov [tz_kick], bx
    mov bx, [te_win]
    call OSAPI_WM_WAKE
.nodlg:

    ; --- the DRAIN's exit: quiet for two seconds and the stream is ours -----
    cmp byte [tz_st], ZR_DRAIN
    jne .nodrain
    call OSAPI_GET_TICKS
    sub ax, [tz_tick]
    cmp ax, TZ_DRAINQ
    jb .out
    mov byte [tz_st], ZR_ERR        ; ...and the REASON stays on the status line
    call tz_end                     ; (SPEC.md 47), which tz_end does not touch
    mov byte [tz_st], ZR_ERR
    jmp .out
.nodrain:

    ; --- and the timeout, which is what returns the terminal ----------------
    cmp byte [tz_st], ZR_DLG
    je .out                         ; **HELD**: the transfer sends nothing and
                                    ; times out on nothing while the user is
                                    ; being asked where to put it
    call OSAPI_GET_TICKS
    sub ax, [tz_tick]
    cmp ax, TZ_TMO
    jb .out
    call tz_touchq                  ; ...the clock, without clearing the count
    inc byte [tz_try]
    cmp byte [tz_try], TZ_TRIES
    jb .again
    mov si, tz_s_tmo
    call tz_fail
    jmp short .out
.again:
    cmp byte [tz_st], ZR_FIN
    je .fin                         ; the sender's `OO` never came, and it does
    call tz_resend                  ; not have to: we are done either way
    jmp short .out
.fin:
    call tz_end
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_touchq - the clock alone, so a retry COUNT survives it ---------------
tz_touchq:
    push ax
    call OSAPI_GET_TICKS
    mov [tz_tick], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_canrx - CF=1 if the receiver cannot take another byte right now
;
; **THE BACK-PRESSURE IS TCP's** (SPEC.md 70.11.3). While both halves are
; spoken for the worker simply does not call NETV_RECV and the bytes already in
; te_rx stay there: the sender blocks on its own window and nothing is dropped
; anywhere in the chain. Downloads are bounded only by the disk.
; -----------------------------------------------------------------------------
tz_canrx:
    push ax
    cmp byte [tz_st], ZR_DATA
    jne .ok                         ; a HEADER is always welcome - only the
                                    ; staging area can be full, and refusing
                                    ; headers would stall a ZFILE the sender is
                                    ; repeating while a dialog is up
    cmp byte [tz_dbl], 0
    je .single
    ; --- DOUBLE-buffered: room unless the OTHER half is out AND this one is
    ;     full, in which case tz_store raises and switches.
    cmp byte [tz_req], TZ_NONE
    je .ok
    mov ax, [tz_fill]
    cmp ax, [tz_chunk]
    jb .ok
    jmp short .no
.single:
    ; --- **SINGLE-buffered, and this arm said YES when it must not.** There is
    ; ONE buffer, so a commit out means the buffer is the one being written and
    ; a full buffer means there is nowhere to put the byte - tz_raise does not
    ; switch halves here, so tz_store's `xor bx, bx` used to land on offset 0 of
    ; the chunk the UI task had just been handed. One silently wrong byte per
    ; 8,192 in the file, with the length right and the CRC that would have
    ; caught it already spent. Unreachable on a floppy, which is every geometry
    ; any gate drives.
    cmp byte [tz_req], TZ_NONE
    jne .no
    mov ax, [tz_fill]
    cmp ax, [tz_chunk]
    jae .no                         ; full, and tz_poll is what raises it
.ok:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; tz_wmap - out AX = a bitmap of the live window slots
;
; The dialog is a WINDOW, and this is what makes SPEC.md 70.11.4's second cancel
; signal a fact about THE DIALOG rather than about the desktop. A TALLY moves
; whenever anything else creates or destroys a window: one destroyed elsewhere
; brings the count back to the baseline and infers a cancel on a dialog nobody
; touched, one created hides a real cancel until the backstop. A BITMAP taken
; before the open and XORed with one taken after names the slot the dialog
; actually took, and after that only THAT slot is asked.
;
; OSAPI_WM_OWNSEG answers CF=1 for a free slot and CF=0 with the owning segment
; for a live one, and preserves every register but DX - so AL is re-loaded per
; slot and the map is built in BX.
; -----------------------------------------------------------------------------
tz_wmap:
    push bx
    push cx
    push dx
    xor bx, bx
    xor cx, cx
.s:
    mov al, cl
    call OSAPI_WM_OWNSEG
    jc .next
    push cx
    mov ax, 1
    shl ax, cl                      ; `shl reg, CL` is 8086 (CLAUDE.md)
    or bx, ax
    pop cx
.next:
    inc cl
    cmp cl, MAX_WIN
    jb .s
    mov ax, bx
    pop dx
    pop cx
    pop bx
    ret

; --- tz_dgone - CF=1 if the dialog's OWN window slot is free again ----------
tz_dgone:
    push ax
    cmp byte [tz_dslot], 0xFF
    je .no                          ; the slot was never worked out: the
                                    ; sixty-second backstop is the answer
    mov al, [tz_dslot]
    call OSAPI_WM_OWNSEG
    jc .yes
.no:
    pop ax
    clc
    ret
.yes:
    pop ax
    stc
    ret

; --- tz_post - AL = a request; THE FLAG IS WRITTEN LAST ----------------------
; Every argument is already in place, and the UI task may run the instant this
; byte lands (SPEC.md 70.11.3, apps/ftpd's fd_post).
tz_post:
    push ax
    push bx
    mov [tz_req], al
    mov bx, [te_win]
    call OSAPI_WM_WAKE
    pop bx
    pop ax
    ret

; =============================================================================
; THE BYTE MACHINE
;
; te_byte hands every application byte here while [te_zon] is set. Not one byte
; of state lives in a local across a call, so a stream split at any offset
; behaves like one that is not.
; =============================================================================
tz_byte:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tz_touchq                  ; a byte arrived, so nothing is stuck
    cmp byte [tz_st], ZR_DRAIN
    je .out                         ; ...and in a DRAIN that is all it does
.top:
    mov bl, [tz_ps]
    cmp bl, ZP_IDLE
    je .idle
    cmp bl, ZP_PAD
    je .pad
    cmp bl, ZP_FMT
    je .fmt
    cmp bl, ZP_HEX
    je .hex
    cmp bl, ZP_HEOL
    je .heol
    cmp bl, ZP_BIN
    je .bin
    cmp bl, ZP_DATA
    je .data
    cmp bl, ZP_DCRC
    je .dcrc
    ; ZP_OO - the sender's two `O`s after our ZFIN. **BOTH of them**: ending on
    ; the first left [te_zon] clear when the second arrived, and te_byte then
    ; handed it to the ANSI parser - so every completed batch printed a stray
    ; `O` on the board's screen (the w4 review's MINOR 9).
    cmp al, 'O'
    jne .ooend
    inc byte [tz_oo]
    cmp byte [tz_oo], 2
    jb .out
.ooend:
    call tz_end
    jmp .out

    ; --- IDLE: everything that is not a ZPAD is skipped ---------------------
    ; Which is what makes the trailer of a hex header free: the CR, the LF and
    ; the XON a sender leaves behind, and the XON after a ZCRCW subpacket, all
    ; fall through here (SPEC.md 70.11.1 - trying to consume the XON
    ; deterministically fails on ZFIN, so lrzsz leaves it for the next scan and
    ; so does this).
.idle:
    cmp al, ZPAD
    jne .out
    mov byte [tz_ps], ZP_PAD
    jmp .out

.pad:
    cmp al, ZPAD
    je .out                         ; a second (or third) pad: still a pad
    cmp al, ZDLE
    je .isfmt
    mov byte [tz_ps], ZP_IDLE
    jmp .out
.isfmt:
    mov byte [tz_ps], ZP_FMT
    jmp .out

.fmt:
    cmp al, ZHEX
    je .ishex
    cmp al, ZBIN
    je .isbin
    cmp al, ZBIN32
    je .is32
    mov byte [tz_ps], ZP_IDLE
    jmp .out
.ishex:
    mov byte [tz_ps], ZP_HEX
    mov word [tz_n], 0
    jmp .out
.isbin:
    mov byte [tz_ps], ZP_BIN
    mov word [tz_n], 0
    mov word [tz_crc], 0
    mov byte [tz_esc], 0
    jmp .out
.is32:
    ; **ZBIN32 IS ANSWERED ZNAK AND THE TRANSFER CONTINUES** (SPEC.md
    ; 70.11.1) - that is what the negotiation is for. The header's own bytes
    ; are then skipped by the ZPAD scan, which is safe rather than lucky: a
    ; false restart needs a ZPAD, a ZDLE and an A/B/C in that order, and a
    ; sender that has just been NAKed sends its next header immediately.
    mov byte [tz_ps], ZP_IDLE
    mov al, ZF_NAK
    call tz_say
    jmp .out

    ; --- a HEX header: fourteen digits, then CR LF --------------------------
.hex:
    call tz_nib
    jc .hexbad
    mov bx, [tz_n]
    mov si, bx
    shr si, 1                       ; SI = which of the seven bytes
    test bl, 1
    jnz .lo
    mov cl, 4
    shl al, cl                      ; `shl reg, CL` is 8086 (CLAUDE.md)
    mov [tz_hb + si], al
    jmp short .hexn
.lo:
    or [tz_hb + si], al
.hexn:
    inc word [tz_n]
    cmp word [tz_n], 14
    jb .out
    mov byte [tz_ps], ZP_HEOL
    call tz_hcrc
    jc .hexbad
    mov byte [tz_pend], 1           ; the frame is GOOD and is dispatched by
    jmp .out                        ; the trailer below, so a ZFILE's own
                                    ; subpacket is not read as CR LF XON
.hexbad:
    mov byte [tz_ps], ZP_IDLE
    jmp .out

.heol:
    cmp al, 0x0D
    je .out
    cmp al, 0x8D
    je .out
    mov byte [tz_ps], ZP_IDLE
    cmp byte [tz_pend], 0
    je .lf
    mov byte [tz_pend], 0
    call tz_frame                   ; ...which sets [tz_ps] for what follows
.lf:
    cmp al, 0x0A
    je .out
    cmp al, 0x8A
    je .out
    jmp .top                        ; a trailer that was not the LF belongs to
                                    ; whatever the frame put us in - one
                                    ; re-entry, and tz_frame never sets ZP_HEOL

    ; --- a BINARY header: seven ZDLE-decoded bytes --------------------------
.bin:
    call tz_unesc
    jc .cancel
    or ah, ah
    jnz .binx                       ; the ZDLE itself, or a terminator where a
    mov bx, [tz_n]                  ; header byte was wanted
    cmp bx, 7
    jae .binn
    mov [tz_hb + bx], al
    cmp bx, 5
    jae .binn                       ; the two CRC bytes are not IN the CRC
    call tz_crcb
.binn:
    inc word [tz_n]
    cmp word [tz_n], 7
    jb .out
    mov ax, [tz_crc]
    cmp ah, [tz_hb + 5]
    jne .binbad
    cmp al, [tz_hb + 6]
    jne .binbad
    mov byte [tz_ps], ZP_IDLE
    call tz_frame
    jmp .out
.binbad:
    mov byte [tz_ps], ZP_IDLE
    mov al, ZF_NAK
    call tz_say
    jmp .out
.binx:
    cmp ah, 1
    je .out
    mov byte [tz_ps], ZP_IDLE
    jmp .out

    ; --- SUBPACKET DATA -----------------------------------------------------
.data:
    call tz_unesc
    jc .cancel
    cmp ah, 1
    je .out                         ; the ZDLE itself
    cmp ah, 2
    je .term
    mov byte [tz_cans], 0           ; ...and a byte that decoded resets the run
    call tz_crcb
    call tz_sink
    jmp .out
.term:
    ; **THE CRC COVERS THE DATA PLUS THE FRAME-END BYTE** (SPEC.md 70.11.1),
    ; which is the classic way to build a receiver that NAKs every subpacket
    ; while looking correct.
    mov [tz_fend], al
    call tz_crcb
    mov byte [tz_ps], ZP_DCRC
    mov word [tz_n], 0
    jmp .out

.dcrc:
    call tz_unesc
    jc .cancel
    or ah, ah
    jnz .out
    mov bx, [tz_n]
    cmp bx, 2
    jae .out
    mov [tz_cb + bx], al
    inc word [tz_n]
    cmp word [tz_n], 2
    jb .out
    mov ax, [tz_crc]
    cmp ah, [tz_cb + 0]
    jne .crcbad
    cmp al, [tz_cb + 1]
    jne .crcbad
    call tz_subok
    jmp short .out
.crcbad:
    call tz_subbad
    jmp short .out

.cancel:
    ; ZDLE ZDLE is CANCEL and a bad escape is a protocol error - one of each
    ; resynchronises on the next ZPAD rather than ending the session, because a
    ; sender that has lost its place says so again.
    ;
    ; **FIVE IN A ROW IS THE SENDER'S ABORT** (the w4 review's MINOR 3), which
    ; is what a user cancelling at the BBS end sends. Resynchronising on those
    ; left the terminal showing nothing at all for TZ_TMO x TZ_TRIES = fifty
    ; seconds before the retry count ran out.
    mov byte [tz_ps], ZP_IDLE
    mov byte [tz_esc], 0
    inc byte [tz_cans]
    cmp byte [tz_cans], 5
    jb .out
    mov si, tz_s_scan
    call tz_fail
    jmp .out
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_nib - AL = a hex digit -> its value, CF=1 if it is not one -----------
tz_nib:
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .alpha
    sub al, '0'
    clc
    ret
.alpha:
    or al, 0x20                     ; a sender may spell it either way
    cmp al, 'a'
    jb .no
    cmp al, 'f'
    ja .no
    sub al, 'a' - 10
    clc
    ret
.no:
    stc
    ret

; --- tz_hcrc - the hex header's own CRC, over the type and the four ----------
; out: CF=1 = it does not check
tz_hcrc:
    push ax
    push bx
    push cx
    mov word [tz_crc], 0
    xor bx, bx
    mov cx, 5
.b:
    mov al, [tz_hb + bx]
    call tz_crcb
    inc bx
    loop .b
    mov ax, [tz_crc]
    cmp ah, [tz_hb + 5]
    jne .no
    cmp al, [tz_hb + 6]
    jne .no
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; tz_unesc - AL = a raw byte -> a decoded one (SPEC.md 70.11.1's table)
; out: CF=1 a protocol error or CANCEL; else AH = 0 a data byte in AL,
;      1 nothing yet (this byte WAS the ZDLE), 2 AL is a frame-end terminator
;
; The order of the arms IS the rule: the four terminators and the two rubouts
; sit inside the `(c AND 0x60) == 0x40` range and are tested first.
; -----------------------------------------------------------------------------
tz_unesc:
    cmp byte [tz_esc], 0
    jne .esc
    cmp al, ZDLE
    je .set
    xor ah, ah
    clc
    ret
.set:
    mov byte [tz_esc], 1
    mov ah, 1
    clc
    ret
.esc:
    mov byte [tz_esc], 0
    cmp al, ZCRCE
    jb .rub
    cmp al, ZCRCW
    ja .rub
    mov ah, 2
    clc
    ret
.rub:
    cmp al, ZRUB0
    jne .r1
    mov al, 0x7F
    xor ah, ah
    clc
    ret
.r1:
    cmp al, ZRUB1
    jne .gen
    mov al, 0xFF
    xor ah, ah
    clc
    ret
.gen:
    cmp al, ZDLE
    je .bad                         ; **ZDLE ZDLE IS CANCEL**, not a literal
    mov ah, al
    and ah, 0x60
    cmp ah, 0x40
    jne .bad
    xor al, 0x40                    ; ...which is how a literal 0x18 arrives,
    xor ah, ah                      ; as `ZDLE X`
    clc
    ret
.bad:
    stc
    ret

; =============================================================================
; WHERE A DECODED DATA BYTE GOES
; =============================================================================
tz_sink:
    push ax
    push bx
    mov bl, [tz_sk]
    cmp bl, TZS_INFO
    je .info
    cmp bl, TZS_STAGE
    je .stage
    pop bx
    pop ax
    ret
.info:
    mov bx, [tz_dn]                 ; **[tz_dn] AND NOT [tz_n]**: `.term` zeroes
                                    ; [tz_n] to count the two CRC bytes, so a
                                    ; reader of it afterwards sees 2 - which
                                    ; NUL-terminated the file name at its third
                                    ; character and left tz_infodone parsing the
                                    ; rest of the name as a decimal size
    cmp bx, TZ_INFOSZ - 1
    jae .idone                      ; the TAIL is dropped and still CRC'd
    mov [tz_info + bx], al
    inc word [tz_dn]
.idone:
    pop bx
    pop ax
    ret
.stage:
    call tz_store
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_store - AL into the staging half that is filling
;
; The half that fills is RAISED and the worker switches to the other one; the
; case where there is no other one - a destination whose cluster is over 4,096
; bytes, so the chunk is the whole 8,192 - is caught by tz_canrx, which stops
; the receive entirely while a commit is out.
; -----------------------------------------------------------------------------
tz_store:
    push ax
    push bx
    push si
    ; --- **THE SKIP, which is how a ZRPOS from the COMMITTED offset stays
    ; honest** (SPEC.md 70.11.2). A subpacket that straddles a chunk boundary
    ; has its first part committed before its CRC is checked, so a CRC error
    ; leaves that prefix in the file - and there is no truncate verb to take it
    ; back. So the ZRPOS names the SUBPACKET's start and this drops the bytes
    ; the sender re-sends over what is already on the disk. The file's size
    ; never moves, which is what keeps OSAPI_FILE_APPEND's cluster-multiple
    ; precondition true through a retry.
    cmp word [tz_skip], 0
    je .live
    dec word [tz_skip]
    mov bx, tz_rcv
    mov ax, 1
    call tz_add32
    jmp short .out
.live:
    mov bx, [tz_fill]
    cmp bx, [tz_chunk]
    jb .put
    cmp byte [tz_dbl], 0
    je .stuck                       ; SINGLE-buffered: tz_canrx refuses at a
    call tz_raise                   ; full buffer and tz_poll is what raises it,
    xor bx, bx                      ; so this cannot happen - and if it ever
.put:                               ; does, a ZRPOS resynchronises rather than a
                                    ; byte going somewhere it must not
    mov si, tz_stg                  ; **DX IS NOT AN ADDRESS REGISTER ON AN
    cmp byte [tz_half], 0           ; 8086**: the modes are [BX], [BP], [SI],
    je .h0                          ; [DI] and the four sums of one of each pair
    add si, TZ_HALF
.h0:
    add si, bx
    mov [si], al
    inc bx
    mov [tz_fill], bx
    mov bx, tz_rcv
    mov ax, 1
    call tz_add32
.out:
    pop si
    pop bx
    pop ax
    ret
.stuck:
    mov byte [tz_owe], 2            ; ...ask the sender to start again from the
    jmp short .out                  ; bytes committed, and drop this one

; --- tz_raise - hand the filling half to the UI task -------------------------
tz_raise:
    push ax
    mov al, [tz_half]
    mov [tz_uh], al
    mov ax, [tz_fill]               ; **[tz_fill] AND NOT [tz_chunk]**: they are
    mov [tz_un], ax                 ; the same number for every commit but the
                                    ; LAST, which is whatever is left when the
                                    ; ZEOF arrives (SPEC.md 18.4.4's own
                                    ; exception) - and a final chunk written as
                                    ; a full one appends up to 4,095 bytes of
                                    ; the PREVIOUS chunk to the file
    mov word [tz_fill], 0
    cmp byte [tz_dbl], 0
    je .same
    xor al, al
    cmp byte [tz_half], 0
    jne .set
    mov al, 1
.set:
    mov [tz_half], al
.same:
    mov al, TZ_DATA
    call tz_post
    pop ax
    ret

; --- tz_cp32 - the dword at SI -> the dword at DI ----------------------------
tz_cp32:
    push ax
    mov ax, [si]
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    pop ax
    ret

; --- tz_add32 - add AX to the dword at BX ------------------------------------
tz_add32:
    add [bx], ax
    adc word [bx+2], 0
    ret

; --- tz_cmp32 - the dword at SI against the dword at DI, UNSIGNED -----------
; out: CF=1 if [SI] < [DI]. `pop` does not touch the flags, so the answer rides
; out of the epilogue.
tz_cmp32:
    push ax
    mov ax, [si+2]
    cmp ax, [di+2]
    jne .out
    mov ax, [si]
    cmp ax, [di]
.out:
    pop ax
    ret

; --- tz_eq32 - the dwords at SI and DI; ZF=1 if they are equal ---------------
tz_eq32:
    push ax
    mov ax, [si]
    cmp ax, [di]
    jne .out
    mov ax, [si+2]
    cmp ax, [di+2]
.out:
    pop ax
    ret

; =============================================================================
; A COMPLETE SUBPACKET
; =============================================================================
tz_subok:
    push ax
    push bx
    mov byte [tz_ps], ZP_IDLE
    mov bl, [tz_sk]
    cmp bl, TZS_INFO
    je .info
    cmp bl, TZS_STAGE
    je .stage
    jmp short .cont
.info:
    call tz_infodone
    jmp short .out                  ; **AND NO ZACK.** SPEC.md 70.11.1's table
                                    ; is about a DATA subpacket; the ZFILE
                                    ; info's own ZCRCW is answered with the
                                    ; ZRPOS or the ZSKIP the dialog decides,
                                    ; which is what the sender reads next
.stage:
    mov al, [tz_fend]
    cmp al, ZCRCQ
    je .ack
    cmp al, ZCRCW
    je .ack
    jmp short .cont
.ack:
    mov bx, tz_rcv                  ; the ACK names what has ARRIVED, which is
    mov al, ZF_ACK                  ; what the sender's own position is
    call tz_saypos
.cont:
    mov al, [tz_fend]
    cmp al, ZCRCG
    je .more
    cmp al, ZCRCQ
    je .more
    jmp short .out
.more:
    mov byte [tz_ps], ZP_DATA       ; the FRAME continues: another subpacket
    mov word [tz_crc], 0            ; follows with no header in between
    mov word [tz_n], 0
    mov word [tz_dn], 0             ; ...and the DATA counter with them, which
                                    ; only an INFO block continued by a ZCRCG
                                    ; would ever have noticed
    cmp byte [tz_sk], TZS_STAGE
    jne .out
    push si
    push di
    mov si, tz_rcv                  ; ...and where the NEXT subpacket starts,
    mov di, tz_sub0                 ; which is what a CRC error resumes from
    call tz_cp32
    pop di
    pop si
.out:
    call tz_touch
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_subbad - a CRC error: ZRPOS from the bytes COMMITTED (SPEC.md 70.11.2)
;
; Not from the bytes received: the half still filling is discarded and the half
; in flight may or may not have reached the disk, so [tz_pos] is the only offset
; this end can honestly name - and it is not final until the UI task's commit
; comes back, which is what [tz_owe] waits for.
; -----------------------------------------------------------------------------
tz_subbad:
    push ax
    mov byte [tz_ps], ZP_IDLE
    cmp byte [tz_sk], TZS_STAGE
    jne .nak
    mov byte [tz_owe], 2
    jmp short .out
.nak:
    mov al, ZF_NAK
    call tz_say
.out:
    call tz_touch
    pop ax
    ret

; =============================================================================
; A COMPLETE HEADER
; =============================================================================
tz_frame:
    push ax
    push bx
    push si
    push di
    mov al, [tz_hb + 0]
    cmp al, ZF_RQINIT
    je .rqinit
    cmp al, ZF_FILE
    je .file
    cmp al, ZF_DATA
    je .data
    cmp al, ZF_EOF
    je .eof
    cmp al, ZF_FIN
    je .fin
    jmp .out                        ; ZRINIT is a STRAGGLER TO SKIP and never a
                                    ; reason to resend (SPEC.md 70.11.2); ZACK,
                                    ; ZNAK, ZSKIP and ZRPOS are the sender's
                                    ; side of the conversation
.rqinit:
    call tz_rinit                   ; it is still asking: say it again
    call tz_touch
    jmp .out

.file:
    ; The info subpacket follows immediately, ZCRCW-terminated.
    mov byte [tz_ps], ZP_DATA
    mov word [tz_crc], 0
    mov word [tz_n], 0
    mov word [tz_dn], 0
    mov byte [tz_esc], 0
    mov byte [tz_sk], TZS_INFO
    cmp byte [tz_st], ZR_DLG
    jne .fil2
    mov byte [tz_sk], TZS_NONE      ; a dialog is already up for this file and
                                    ; the sender is repeating itself: swallow it
.fil2:
    call tz_touch
    jmp .out

.data:
    cmp byte [tz_st], ZR_DATA
    jne .swallow
    mov si, tz_hb + 1
    mov di, tz_rcv
    call tz_eq32
    jne .repos                      ; it is resuming somewhere we are not
    mov byte [tz_ps], ZP_DATA
    mov word [tz_crc], 0
    mov word [tz_n], 0
    mov word [tz_dn], 0
    mov byte [tz_esc], 0
    mov byte [tz_sk], TZS_STAGE
    mov si, tz_rcv                  ; ...and where this subpacket STARTS, which
    mov di, tz_sub0                 ; is the offset a CRC error resumes from
    call tz_cp32
    call tz_touch
    jmp short .out
.repos:
    mov byte [tz_owe], 2
    jmp .swallow

.eof:
    cmp byte [tz_st], ZR_DATA
    jne .out
    mov si, tz_hb + 1               ; **ZEOF CARRIES THE FILE LENGTH AS ITS
    mov di, tz_rcv                  ; POSITION** (SPEC.md 70.11.1), so a
    call tz_eq32                    ; disagreement here is a lost subpacket
    jne .eofbad                     ; ...and NOT `.repos`, which falls into
                                    ; `.swallow` and puts the parser into
                                    ; SUBPACKET mode. No subpacket follows a
                                    ; ZEOF: the next bytes are the sender's
                                    ; answer to our ZRPOS, and eating them as
                                    ; data leaves recovery to whichever byte
                                    ; pair happens to decode as a terminator
    cmp word [tz_fill], 0
    je .eofr
    call tz_raise                   ; the LAST chunk is whatever is left, which
                                    ; is SPEC.md 18.4.4's own exception
.eofr:
    mov byte [tz_st], ZR_EOF
    mov byte [tz_owe], 1            ; ...and the ZRINIT waits for that commit,
    call tz_touch                   ; so the sender cannot start the next file
    jmp short .out                  ; while this one is still being written

.eofbad:
    mov byte [tz_owe], 2            ; a ZRPOS, and the scan stays in ZP_IDLE
    jmp short .out
.fin:
    mov al, ZF_FIN
    call tz_say
    mov byte [tz_st], ZR_FIN
    mov byte [tz_ps], ZP_OO         ; ...and the sender's `OO` ends it
    call tz_touch
    jmp short .out

.swallow:
    mov byte [tz_ps], ZP_DATA
    mov word [tz_crc], 0
    mov word [tz_n], 0
    mov word [tz_dn], 0
    mov byte [tz_esc], 0
    mov byte [tz_sk], TZS_NONE
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_infodone - `name NUL size mtime mode serial files_left bytes_left NUL`
;
; **ONLY THE FIRST TWO FIELDS ARE READ** (SPEC.md 70.11.2): the name for the
; Save dialog and the size for the progress line. The mtime is OCTAL, which is
; easy to misread as decimal and is exactly the kind of field a terminal gains
; nothing by having an opinion about.
; -----------------------------------------------------------------------------
tz_infodone:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [tz_dn]
    mov byte [tz_info + bx], 0      ; ...NUL-terminated whatever the sender did
    mov si, tz_info
    mov cx, TZ_INFOSZ               ; **SIXTY-FOUR AND NOT SIXTY-THREE.**
.skip:                              ; tz_sink stores 0..62 and the NUL above
    cmp byte [si], 0                ; goes at 63, so a scan of 63 inspects
    je .size                        ; 0..62, never sees it, and falls out with
    inc si                          ; SI at tz_info+63 - one `inc si` from
    loop .skip                      ; tz_stg, where the PREVIOUS download's
                                    ; digits were then parsed as this file's
                                    ; size and fed straight to the divisor
                                    ; BLOCKER 1 guards. The bound was written
                                    ; for exactly this and was one short
.size:
    inc si
    mov word [tz_fsz], 0
    mov word [tz_fsz+2], 0
    mov cx, TZ_INFOSZ               ; ...and the digits cannot leave the buffer
.dig:                               ; either, whatever a NUL-free field does
    jcxz .have
    dec cx
    mov al, [si]
    cmp al, '0'
    jb .have
    cmp al, '9'
    ja .have
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, [tz_fsz]                ; fsz = fsz*10 + digit, in 32 bits
    mov dx, [tz_fsz+2]
    mov cx, 10
    push dx
    mul cx                          ; DX:AX = lo * 10
    pop bx
    mov [tz_fsz], ax
    mov [tz_fsz+2], dx
    mov ax, bx
    mul cx                          ; the high half's carry-in is dropped: a
    add [tz_fsz+2], ax              ; file over 4 GB is not a thing this
    pop ax                          ; machine mounts
    mov bx, tz_fsz
    call tz_add32
    inc si
    jmp short .dig
.have:
    mov si, tz_info
    call tz_mangle                  ; -> tz_name, SPEC.md 77.20's rule
    cmp byte [tz_st], ZR_DATA
    jne .ask
    ; **A SECOND ZFILE FOR A FILE ALREADY OPEN IS IDEMPOTENT** (SPEC.md
    ; 70.11.2): our ZRPOS was lost, the sender repeated itself, and a receiver
    ; that treated the repeat as a new file would write the download twice or
    ; append it to itself - and the DISK would say so rather than the wire.
    mov word [tz_pos], 0
    mov word [tz_pos+2], 0
    mov word [tz_rcv], 0
    mov word [tz_rcv+2], 0
    mov word [tz_fill], 0
    mov byte [tz_half], 0
    mov byte [tz_made], 0
    mov bx, tz_pos
    mov al, ZF_RPOS
    call tz_saypos
    jmp short .out
.ask:
    or byte [tz_diag], 32
    mov byte [tz_st], ZR_DLG        ; HELD: nothing is sent until the UI answers
    mov al, TZ_NAME
    call tz_post
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE 8.3 MANGLE (SPEC.md 70.11.4, and it is SPEC.md 77.20's rule)
;
; A file this machine downloads by Zmodem and the same file uploaded to it by
; FTP must land on the same name, so this is a second copy of the RULE and not
; of the code - a package cannot call another package's proc - and
; tests/telzm.py asserts both ends against tools/os88bbs.py's own
; MANGLE83_CASES so the two cannot drift.
;
;   * everything up to and including the last `/` or `\` is dropped;
;   * **a name that is already legal 8.3 is left alone**, case and all, which
;     is what makes the mapping round-trip;
;   * otherwise the extension is the first three legal characters after the
;     LAST dot and the name is the first six legal plus `~1`;
;   * it is DETERMINISTIC - always `~1`, never a search for a free `~2`,
;     because a counter resolved against the directory makes the same download
;     land differently on two disks;
;   * a name with nothing legal in it becomes `FILE~1`.
; =============================================================================
tz_m83:     db "$%'-_@~`!(){}^#&", 0

; --- tz_leg - AL legal in an 8.3 name? CF=0 and AL upcased if it is ----------
tz_leg:
    push bx
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 32
.up:
    cmp al, 'A'
    jb .dig
    cmp al, 'Z'
    jbe .yes
.dig:
    cmp al, '0'
    jb .sym
    cmp al, '9'
    jbe .yes
.sym:
    mov bx, tz_m83
.s:
    cmp byte [bx], 0
    je .no
    cmp al, [bx]
    je .yes
    inc bx
    jmp short .s
.yes:
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; --- tz_is83 - CF=0 if DS:SI is already a legal 8.3 name ---------------------
tz_is83:
    push ax
    push cx
    push si
    xor cx, cx                      ; CL = stem length
.stem:
    mov al, [si]
    or al, al
    jz .nodot
    cmp al, '.'
    je .dot
    call tz_leg
    jc .no
    cmp cl, 8
    jae .no
    inc cl
    inc si
    jmp short .stem
.nodot:
    or cl, cl
    jz .no
    jmp short .yes
.dot:
    or cl, cl
    jz .no                          ; `.TXT` has no stem
    inc si
    xor ch, ch                      ; CH = extension length
.ext:
    mov al, [si]
    or al, al
    jz .edone
    cmp al, '.'
    je .no                          ; a SECOND dot
    call tz_leg
    jc .no
    cmp ch, 3
    jae .no
    inc ch
    inc si
    jmp short .ext
.edone:
    or ch, ch
    jz .no                          ; ...and a trailing dot is not an extension
.yes:
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop cx
    pop ax
    stc
    ret

; --- tz_mangle - DS:SI = the sender's pathname -> tz_name --------------------
tz_mangle:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- the LEAF: a Zmodem pathname is a path and the file goes where the
    ; user says, not where the sender says.
    mov bx, si
.leaf:
    mov al, [si]
    or al, al
    jz .haveleaf
    inc si
    cmp al, '/'
    je .cut
    cmp al, '\'
    jne .leaf
.cut:
    mov bx, si
    jmp short .leaf
.haveleaf:
    mov si, bx
    call tz_is83
    jc .mangle
    mov di, tz_name                 ; already legal: copied VERBATIM
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cp
    jmp .out
.mangle:
    ; --- where the extension starts, if there is one ------------------------
    xor dx, dx                      ; DX = the last dot, 0 = none
    mov bx, si
.fd:
    mov al, [bx]
    or al, al
    jz .fdone
    inc bx
    cmp al, '.'
    jne .fd
    mov dx, bx                      ; ...just PAST the dot
    jmp short .fd
.fdone:
    mov [tz_dot], dx
    ; --- the stem: the first six legal characters before it -----------------
    mov di, tz_name
    xor cx, cx
.st:
    mov al, [si]
    or al, al
    jz .stdone
    or dx, dx
    jz .stkeep
    mov bx, si
    inc bx
    cmp bx, dx
    jae .stdone                     ; SI has reached the dot
.stkeep:
    call tz_leg
    jc .stnext
    cmp cl, 6
    jae .stdone
    mov [di], al
    inc di
    inc cl
.stnext:
    inc si
    jmp short .st
.stdone:
    or cl, cl
    jnz .tilde
    mov byte [di], 'F'              ; nothing legal anywhere: it still has to be
    mov byte [di+1], 'I'            ; something a person can see and delete
    mov byte [di+2], 'L'
    mov byte [di+3], 'E'
    add di, 4
.tilde:
    mov byte [di], '~'
    mov byte [di+1], '1'
    add di, 2
    mov si, [tz_dot]
    or si, si
    jz .term
    mov byte [di], '.'
    inc di
    xor cx, cx
.ex:
    mov al, [si]
    or al, al
    jz .term
    inc si
    call tz_leg
    jc .ex
    cmp cl, 3
    jae .term
    mov [di], al
    inc di
    inc cl
    jmp short .ex
.term:
    mov byte [di], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE UI TASK'S HALF - everything here that touches a disk (SPEC.md 70.11.3)
;
; OSAPI_WM_ONWAKE's handler: called SI = our window, on the UI task, billed to
; this instance, **WITHOUT THE GFX LOCK**. That last part is what makes the
; design work at all - it is the one callback allowed to call the file slots,
; and it must take the lock itself before it draws.
;
; It is a KICK and not a message: a stale one is possible and one may arrive
; with nothing to do, so the first thing it does is look at [tz_req] and the
; commonest answer is "nothing".
; =============================================================================
tz_wake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES:BX is OURS for the file slots, and ES
                                    ; is the KERNEL's on the way in
    mov al, [tz_req]
    cmp al, TZ_NAME
    je .name
    cmp al, TZ_DATA
    je .data
    cmp al, TZ_DONE
    je .done
    jmp short .backstop
.name:
    cmp byte [te_txm], 0
    jne .backstop                   ; **NOT UNDER A FOREIGN TEXT MODE.** The
                                    ; Save dialog is a WINDOW and every kernel
                                    ; drawing slot is off-limits in there
                                    ; (SPEC.md 53.1), so te_fsx_main leaves the
                                    ; bracket when it sees this request and the
                                    ; dialog opens on the desktop the user came
                                    ; from. The request simply waits
    call tz_dlgopen
    jmp short .backstop             ; ...and [tz_req] STAYS SET: the dialog's
                                    ; own answer is what clears it
.data:
    call tz_commit
    mov byte [tz_req], TZ_NONE      ; **LAST, AND AFTER EVERY RESULT IS
                                    ; WRITTEN**: the worker reads the stage the
                                    ; moment this byte goes to zero
    jmp short .backstop
.done:
    mov byte [tz_pan], 0            ; the takeover comes off and the terminal is
    call te_markall                 ; back, which is the repaint it already owed
    mov byte [tz_req], TZ_NONE
.backstop:
    ; --- THE CANCEL, AND IT IS NOT A PAINT (SPEC.md 70.11.4) ----------------
    ; A cancelled file dialog calls nothing back at all (SPEC.md 38.6), so the
    ; cancel has to be inferred. **THE FIRST DESIGN INFERRED IT FROM A W_PAINT
    ; ARRIVING WHILE [tz_dlg] WAS SET, AND THAT IS WRONG** - the dialog is
    ; 300x170 at a fixed (90,60) and this window is 656x254, so it does not
    ; cover us, and `fdlg_open`'s own `wm_show` repaints the desktop the
    ; instant the dialog goes up. The gate saw it: every transfer was skipped
    ; before the user could touch anything, the real dialog stayed on screen
    ; because nothing had actually closed it, and the SECOND file's request was
    ; then refused for a dialog that was already up.
    ;
    ; **THE DIALOG IS A WINDOW, so count the windows.** [tz_nw] is the tally
    ; taken immediately before OSAPI_FILE_DLG created one; when the count is
    ; back to it and [tz_dlg] is still set, the dialog is gone and nobody
    ; called back, which is a cancel and nothing else.
    ;
    ; **A COMMIT CANNOT BE MISREAD AS ONE**, and the argument is the task and
    ; not the timing: `fdlg_commit` destroys the dialog and calls the
    ; completion proc in ONE UI-task call chain, and this handler is another
    ; UI-task callback - so no wake can be dispatched between the destroy and
    ; tz_dlgdone's `mov byte [tz_dlg], 0`. The UI task being one task is the
    ; whole proof.
    cmp byte [tz_dlg], 0
    je .glass
    cmp byte [te_txm], 0
    jne .glass                      ; no dialog can be up under a foreign text
                                    ; mode: tz_wake refuses to open one there
    ; --- the paint's SUSPICION, decided here (the w4 review's MAJOR 4) ------
    ; [tz_dlg] is still set, so tz_dlgdone has not run: either the dialog was
    ; cancelled, or a commit's completion proc is still ahead of us. It cannot
    ; be the latter - fdlg_commit's destroy and its callback are ONE UI-task
    ; call chain and this handler is a separate dispatch, so a suspicion that
    ; survives to the next kick is a cancel.
    ; **THE SLOT IS ASKED FIRST, AND IT IS AUTHORITATIVE.** OSAPI_WM_OWNSEG
    ; tests the record's USED bit, which wm_destroy clears and wm_hide does not
    ; - so while the dialog is on screen it answers "still there" and a paint
    ; that says otherwise is provably wrong. Trusting the paint FIRST left the
    ; other direction open: any wm_paint_all reaching us between TZ_DLGARM and
    ; the backstop - the screensaver's relight is the concrete one - cancelled a
    ; file whose dialog the user was still reading, and every later file was
    ; then refused for a dialog this end had already written off.
    cmp byte [tz_dslot], 0xFF
    je .paintonly                   ; tz_wmap's XOR came back empty and we never
    call tz_dgone                   ; worked out which slot it took
    jnc .tmo
    call tz_dcancel
    jmp short .glass
.paintonly:
    cmp byte [tz_pcan], 0
    je .tmo
    call tz_dcancel
    jmp short .glass
.tmo:
    ; ...and SIXTY SECONDS, which is a backstop and not the
    ; mechanism: it is there for the window that never goes away rather than
    ; for the user who is thinking, and it is long because a sender re-sends
    ; its ZFILE while it waits and a short timeout would race a slow reader.
    ; It lives HERE, on the UI task, because every write to [tz_req] belongs on
    ; this side of the fence - the worker kicks us for it (tz_poll).
    call OSAPI_GET_TICKS
    sub ax, [tz_dlgt]
    cmp ax, TZ_DLGTMO
    jb .glass
    call tz_dcancel
.glass:
    cmp byte [te_txm], 0
    jne .out                        ; the bracket owns the glass AND the lock
                                    ; (SPEC.md 53.6/70.8.8): te_tx_prog is what
                                    ; draws the progress in there, and a
                                    ; GFX_LOCK here would park the UI task on a
                                    ; byte that cannot change until `^]`
    ; --- **AND NOT OVER WHATEVER IS ON TOP OF US** (the w4 review's MAJOR 3).
    ; The gfx primitives take absolute screen coordinates, so without one of
    ; OSAPI_WM_OBSCURED or OSAPI_WM_CLIP_SET a painter paints over the window
    ; above it (apps/os88api.inc rule 5) - and te_show two files away obeys
    ; that, which is what made this a slip rather than a policy. The Save
    ; dialog is 300x170 on (90,60) and this window is 656x254 on (40,40), so
    ; the dialog is ENTIRELY inside our content area and tz_poll kicks this
    ; handler twice a second precisely while one is up: the panel scribbled
    ; over the dialog the user has to read in order to answer it.
    cmp byte [tz_dlg], 0
    jne .out                        ; our own dialog is up: it is on top of us
                                    ; by construction and nothing here may draw
    mov bx, [te_win]
    call OSAPI_WM_OBSCURED
    jc .out                         ; covered by somebody else's window: the
                                    ; next W_PAINT owes it (SPEC.md 11.3)
    call OSAPI_GFX_LOCK             ; ...and it may NOT draw before it has
    mov si, [te_win]
    call te_layout
    cmp byte [tz_pan], 0
    je .st
    call tz_panel
.st:
    call te_status
    call OSAPI_GFX_UNLOCK
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_dlgopen - DFREE, the chunk, and the Save dialog (SPEC.md 70.11.3/70.11.4)
;
; **DFREE IS ASKED ONCE PER TRANSFER AND NOT PER CHUNK.** It does no disk I/O
; and that is not the same as cheap: it is answered from the resident FAT
; snapshot by counting every entry in it, about 105 ms on a 20 MB hard disk at
; 4.77 MHz. SPEC.md 77.40 is the precedent - FTPD asked it per chunk and spent
; 44% of an upload inside it.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; tz_chunk_set - DFREE, and the chunk from the DESTINATION's cluster
; out: CF=1 the cluster is over 8,192 and the transfer is refused
;
; **IT IS ASKED AFTER THE DIALOG ANSWERS, NOT BEFORE IT** (the w4 review's
; MAJOR 2). The Save dialog has a Drive button (SPEC.md 38.11), and
; `fdlg_home_save` records the volume the user landed on into the instance
; before the completion proc runs - which is exactly what makes tz_commit's
; bare-name write land there (SPEC.md 38.10). Sized before the question, the
; chunk describes the volume the terminal was LAUNCHED from: a 4,096 chunk
; against a partition with 8 KB clusters creates the file and then answers
; FERR_NAME on the first OSAPI_FILE_APPEND, and the user is shown `Disk error`
; and a 4 KB stump of a 40 KB file with an innocent disk. The over-8,192
; refusal was never reached through that door at all.
;
; The worker stages nothing until it reads [tz_rst] (tz_poll's `.chosen`), so
; the chunk is still fixed before the first byte lands. This runs on the UI
; task with the gfx lock held and DFREE does no disk I/O, so it is the same
; 105 ms it always was, just asked one question later.
; -----------------------------------------------------------------------------
tz_chunk_set:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_FILE_DFREE           ; BX = SECTORS per cluster
    jc .assume
    or bl, bl
    jnz .have
.assume:
    mov bx, 1                       ; every floppy, and the safe assumption for
.have:                              ; a volume nothing has asked about: too
    xor bh, bh                      ; SMALL a chunk is merely more commits
    mov ax, bx
    mov cx, 512
    mul cx                          ; DX:AX = the cluster in bytes
    or dx, dx
    jnz .big                        ; a cluster over 64KB is not a thing this
    mov bx, ax                      ; kernel mounts (SPEC.md 18.7)
    mov [tz_clus], bx               ; ...and the FACT a refusal names (SPEC.md 47)
    cmp bx, TZ_HALF
    ja .single
    mov ax, TZ_HALF
    xor dx, dx
    div bx                          ; whole clusters in a half...
    mul bx                          ; ...and back to bytes, which is 4,096 for
    mov [tz_chunk], ax              ; every power-of-two cluster at or below it
    mov byte [tz_dbl], 1
    jmp short .ok
.single:
    cmp bx, TZ_STGSZ
    ja .big
    mov [tz_chunk], bx              ; 4,097..8,192: the whole area, SINGLE-
    mov byte [tz_dbl], 0            ; buffered - the worker fills it, then waits
.ok:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.big:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

tz_dlgopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
.open:
    cmp byte [tz_dlg], 0
    jne .out                        ; **OUR OWN DIALOG IS ALREADY UP**, so this
                                    ; request is not servable yet and the kick
                                    ; that brought us here asks again. Without
                                    ; the guard the retry below re-baselined
                                    ; [tz_nw] against a desktop that ALREADY
                                    ; held the dialog, and the very next
                                    ; comparison then read "the count is back"
                                    ; and inferred a cancel on a dialog nobody
                                    ; had touched
    or byte [tz_diag], 1
    call tz_wmap                    ; ...BEFORE it exists, so the bit that
    mov [tz_base], ax               ; appears is the slot it took. BANKED rather
                                    ; than stored: a REFUSED open must not move
                                    ; the baseline
    mov si, tz_name
    mov di, tz_dlgdone
    mov bx, [te_win]
    mov al, FDLG_SAVE
    call OSAPI_GFX_LOCK             ; **fdlg_open's OWN CONTRACT**: "THE CALLER
    call OSAPI_FILE_DLG             ; HOLDS THE GFX LOCK - every legal caller is
    pushf                           ; a window callback or an AM_ONCMD handler,
    call OSAPI_GFX_UNLOCK           ; which is what lets this create and show the
    popf                            ; window inline instead of posting a launch"
                                    ; (kernel/fdlg.inc). OSAPI_WM_ONWAKE is the
                                    ; one callback that runs WITHOUT it, so it is
                                    ; this handler's to take - and cw_wm_show at
                                    ; the foot of fdlg_open raises and repaints
                                    ; under it
    jnc .up                         ; it does NOT block: the completion proc
    or byte [tz_diag], 2            ; runs much later, and a CANCEL calls
                                    ; nothing at all (SPEC.md 38.6)
    ; --- **A REFUSAL IS TRANSIENT AND IS RETRIED, NOT READ AS A CANCEL** ----
    ; fdlg_open answers CF=1 while ANOTHER dialog is up, and the one this
    ; instance just had is only reaped on a later UI pass (SPEC.md 38.1.1) - so
    ; a batch whose files arrive faster than the reap had its second request
    ; refused, its third, and all the rest. Twelve files, one dialog, eleven
    ; silent ZSKIPs. [tz_req] simply STAYS SET and the next kick asks again.
    inc byte [tz_dtry]
    cmp byte [tz_dtry], TZ_DTRY
    jb .out                         ; ...bounded, so a dialog somebody else
    mov byte [tz_dtry], 0           ; leaves up for ever cannot hang the
                                    ; transfer either - **AND THE COUNTER GOES
                                    ; BACK HERE.** It used to be cleared only by
                                    ; a SUCCESSFUL open, so one exhausted retry
                                    ; made every later file give up on its first
                                    ; refused poll: one stuck dialog turned a
                                    ; twelve-file batch into one slow skip and
                                    ; ten instant ones
    mov byte [tz_rst], TZR_CANCEL
    mov byte [tz_req], TZ_NONE
    jmp short .out
.up:
    or byte [tz_diag], 16
    mov byte [tz_dtry], 0
    mov byte [tz_dslot], 0xFF       ; ...and WHICH slot it took, on the one path
    call tz_wmap                    ; where a window was actually made
    xor ax, [tz_base]
    jz .noslot                      ; no new bit: fall back to the backstop
    xor cx, cx
.bit:
    test ax, 1
    jnz .gotslot
    shr ax, 1
    inc cl
    cmp cl, MAX_WIN
    jb .bit
    jmp short .noslot
.gotslot:
    mov [tz_dslot], cl
.noslot:
    mov byte [tz_pcan], 0
    mov byte [tz_dlg], 1
    call OSAPI_GET_TICKS
    mov [tz_dlgt], ax
    mov [tz_kick], ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_dlgdone - the dialog's completion proc
; in:  AL = the mode, SI = our window, ES:DI = the chosen name, DX:CX its size;
;      the UI task, GFX LOCK HELD, after the dialog window is gone
;
; **THE NAME IS ALL THAT COMES BACK** (SPEC.md 38.6/19.2.1): there is no path
; and no drive, because the dialog opened on this instance's own folder and
; fdlg_home_save recorded the folder and the drive into the instance BEFORE
; this runs - so a plain OSAPI_FILE_WRITE on that bare name lands where the
; user chose.
; -----------------------------------------------------------------------------
tz_dlgdone:
    push ax
    push bx
    push cx
    push si
    push di
    or byte [tz_diag], 8
    cmp byte [tz_dlg], 0
    je .out                         ; a stale callback: the session was reset,
    mov byte [tz_dlg], 0            ; or the backstop already answered
    mov byte [tz_pcan], 0           ; ...and the paint's suspicion with it: a
                                    ; COMMIT is the one thing that knows
    mov bx, tz_name
    mov cx, 12
.cp:
    mov al, [es:di]
    mov [bx], al
    inc di
    inc bx
    or al, al
    jz .term
    loop .cp
.term:
    mov byte [bx], 0
    mov byte [tz_rst], TZR_OK
    call tz_chunk_set               ; **THE DESTINATION's cluster, and the
    jnc .sized                      ; question is asked HERE because the Drive
    mov byte [tz_rst], TZR_BIG      ; button may have moved it (the w4 review's
    call tz_bigmsg                  ; MAJOR 2) - and the refusal's sentence is
.sized:                             ; composed here too, so tz_dig keeps its
                                    ; one-task property
    mov byte [tz_req], TZ_NONE      ; ...LAST, and BEFORE the paint below, which
                                    ; is the whole of the cancel inference: on a
                                    ; COMMIT this runs before the UI loop
                                    ; dispatches the paint the destroyed dialog
                                    ; owes, so that paint finds [tz_dlg] clear
    mov si, [te_win]
    call te_paint                   ; ...and we MUST repaint our own content:
                                    ; the kernel does not repaint for a
                                    ; completion proc (SPEC.md 38.6)
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_cancelck - A W_PAINT ARRIVING WHILE [tz_dlg] IS SET IS THE CANCEL
;
; SPEC.md 70.11.4. A cancelled dialog calls nothing back at all (SPEC.md 38.6),
; so the cancel has to be inferred. The dialog is modal and covers this window,
; so no paint reaches us while it is up; destroying it - by either exit -
; exposes what was under it and owes this window a repaint. On a COMMIT the
; completion proc runs BEFORE the UI loop dispatches that paint and clears
; [tz_dlg]; on a CANCEL nothing clears it, and the paint finds it set.
; -----------------------------------------------------------------------------
tz_cancelck:
    push ax
    cmp byte [tz_dlg], 0
    je .no
    call OSAPI_GET_TICKS            ; **ARMED, AND NOT ON THE OPEN'S OWN
    sub ax, [tz_dlgt]               ; PAINT.** fdlg_open's cw_wm_show repaints
    cmp ax, TZ_DLGARM               ; the desktop when the dialog GOES UP, and
    jb .no                          ; the dialog does not cover this window -
                                    ; so that paint arrives milliseconds after
                                    ; the open and is not a cancel. A paint a
                                    ; SECOND later is, because nothing else
                                    ; repaints a desktop with a modal dialog on
                                    ; it: no other window can be dragged,
                                    ; raised or closed while one is up
    ; **AND IT ONLY BANKS A SUSPICION** (the w4 review's MAJOR 4). This proc
    ; used to decide, on the argument that a commit's completion proc runs
    ; before the paint - and that argument is wrong: `fdlg_commit` calls
    ; `fdlg_close` FIRST, `wm_destroy` ends in `wm_paint_dmg` over the rect the
    ; dialog vacated, and `wm_draw_win` calls our W_PAINT **inline** unless the
    ; RAISE CACHE happens to hit. The cache is a PURGEABLE claim (SPEC.md 50.6)
    ; given back the instant anything else needs the room, so on a tight
    ; machine every Save was read as a cancel and every download skipped. The
    ; gate could not see it: an idle desktop always has the cache.
    ;
    ; So the paint writes one byte and tz_wake's next pass decides, by which
    ; time tz_dlgdone has cleared BOTH if a commit really happened. The next
    ; kick is at most TZ_KICK ticks away, so the promptness is unchanged.
    or byte [tz_diag], 4
    mov byte [tz_pcan], 1
.no:
    pop ax
    ret

; --- tz_dcancel - the cancel, taken on the UI task and nowhere else ---------
; Every write to [tz_req] belongs on this side of the fence (SPEC.md 70.11.3),
; and all three of SPEC.md 70.11.4's routes to a cancel land here.
tz_dcancel:
    mov byte [tz_dlg], 0
    mov byte [tz_pcan], 0
    mov byte [tz_dslot], 0xFF
    mov byte [tz_rst], TZR_CANCEL
    mov byte [tz_req], TZ_NONE
    ret

; -----------------------------------------------------------------------------
; tz_commit - [tz_un] bytes of half [tz_uh] onto the disk
;
; The FIRST chunk is OSAPI_FILE_WRITE, which creates or replaces; every later
; one is OSAPI_FILE_APPEND, whose precondition is that the file's current size
; is a whole number of clusters - which is exactly what tz_dlgopen's chunk
; arithmetic keeps true (SPEC.md 18.4.4). The last chunk is whatever is left,
; which is that rule's own exception.
; -----------------------------------------------------------------------------
tz_commit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, [tz_un]
    jcxz .ok                        ; an empty tail needs no append at all - and
                                    ; APPEND's own contract wants CX >= 1
    mov bx, tz_stg
    cmp byte [tz_uh], 0
    je .h0
    add bx, TZ_HALF
.h0:
    mov si, tz_name
    cmp byte [tz_made], 0
    jne .append
    xor dx, dx
    call OSAPI_FILE_WRITE           ; DX:CX = the count, and it CREATES or
    jc .err                         ; REPLACES, which is what a Save dialog
    mov byte [tz_made], 1           ; means everywhere else on this system
    jmp short .ok
.append:
    call OSAPI_FILE_APPEND
    jc .err
.ok:
    mov ax, [tz_un]
    mov bx, tz_pos
    call tz_add32                   ; ...and THIS is what a ZRPOS may name
    mov byte [te_dirty], 1          ; the progress moves per COMMITTED CHUNK and
    jmp short .out                  ; not per subpacket (SPEC.md 70.11.5)
.err:
    mov [tz_ferr], al               ; ...WHICH refusal, because "Disk error" is
                                    ; what every one of them looks like from the
                                    ; outside and FERR_NAME, FERR_FULL and
                                    ; FERR_PROT are three different things to go
                                    ; and do something about (SPEC.md 54.4.1)
    mov byte [tz_rst], TZR_ERR
.out:
    mov word [tz_un], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; PROGRESS (SPEC.md 70.11.5)
;
; Windowed, the status line carries `NAME  12,345 / 98,765` and the terminal's
; content is replaced by a progress panel - SPEC.md 70.3's About-panel shape, a
; takeover the next paint puts back - so nothing has to be redrawn to show a
; number changing. Full screen, row 25 is overwritten with the same line and
; re-emitted from the buffer when the transfer ends.
; =============================================================================

; --- tz_text - `NAME  12,345 / 98,765` into tz_msg ---------------------------
tz_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tz_msg
    mov si, tz_name
    cmp byte [si], 0
    jne .nm
    mov si, tz_s_wait
.nm:
    mov cx, 12
.n:
    mov al, [si]
    or al, al
    jz .nd
    mov [di], al
    inc si
    inc di
    loop .n
.nd:
    mov byte [di], ' '
    mov byte [di+1], ' '
    add di, 2
    mov ax, [tz_pos]
    mov dx, [tz_pos+2]
    call tz_num
    mov byte [di], ' '
    mov byte [di+1], '/'
    mov byte [di+2], ' '
    add di, 3
    mov ax, [tz_fsz]
    mov dx, [tz_fsz+2]
    call tz_num
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_bigmsg - out SI = `Cluster 16,384 too big` (SPEC.md 47) --------------
; **THE FACT AND NOT THE WORD.** SPEC.md 70.11.3 says the status line names the
; cluster size, and SPEC.md 47's rule is that a refusal states a fact a person
; can act on - "too large" is a judgement and 16,384 is the number they can
; take to a different disk.
tz_bigmsg:
    push ax
    push di
    push dx
    mov di, tz_bmsg                 ; **ITS OWN BUFFER.** tz_msg is the progress
    mov si, tz_s_big                ; line and te_status rewrites it on every
                                    ; draw, so a refusal composed there is a
                                    ; refusal nobody ever reads
.c:
    mov al, [si]
    or al, al
    jz .num
    mov [di], al
    inc si
    inc di
    jmp short .c
.num:
    mov ax, [tz_clus]
    xor dx, dx
    call tz_num
    mov si, tz_s_big2
.d:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .d
    mov si, tz_bmsg
    pop dx
    pop di
    pop ax
    ret

; --- tz_num - DX:AX as decimal with thousands separators, at DI --------------
tz_num:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, tz_dig + 11
    mov byte [si], 0
    xor bx, bx
.d:
    call tz_div10                   ; DX:AX = the quotient, CX = the remainder
    dec si
    add cl, '0'
    mov [si], cl
    inc bx
    mov cx, ax
    or cx, dx
    jnz .d
.emit:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec bx
    jz .out
    mov ax, bx
    xor dx, dx
    mov cx, 3
    div cx
    or dx, dx
    jnz .emit
    mov byte [di], ','
    inc di
    jmp short .emit
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_div10 - DX:AX / 10 -> DX:AX, remainder in CX -------------------------
tz_div10:
    push bx
    mov bx, 10
    mov cx, ax
    mov ax, dx
    xor dx, dx
    div bx                          ; AX = the high quotient, DX = into the low
    push ax
    mov ax, cx
    div bx
    mov cx, dx
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tz_panel - the takeover, on the terminal's own area
; in:  te_layout run, THE GFX LOCK HELD
; -----------------------------------------------------------------------------
tz_panel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tz_text
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call te_wpx
    mov cx, [te_px]
    add cx, ax
    dec cx
    mov ax, [te_px]
    mov bx, [te_oy]
    add bx, TE_TOPY
    mov dx, [te_oy]
    add dx, [te_chh]
    sub dx, TE_STATH + 1            ; ...and NOT over the status line, which
    call OSAPI_GFX_FILL             ; carries the same figures
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, tz_s_hdr
    mov dx, [te_oy]
    add dx, TE_TOPY + 12
    mov cx, [te_px]
    add cx, 16
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov si, tz_msg
    mov dx, [te_oy]
    add dx, TE_TOPY + 28
    mov cx, [te_px]
    add cx, 16
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    call tz_bar
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- tz_bar - the outline, and the share of it that has landed ---------------
tz_bar:
    push ax
    push bx
    push cx
    push dx
    call te_wpx
    sub ax, 32
    jbe .out                        ; a window too narrow for a bar draws none
    mov [tz_bw], ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [te_px]
    add ax, 16
    mov bx, [te_oy]
    add bx, TE_TOPY + 48
    mov cx, ax
    add cx, [tz_bw]
    mov dx, bx
    add dx, 11
    call OSAPI_GFX_FRAME
    call tz_frac                    ; AX = the filled width in pixels
    or ax, ax
    jz .out
    mov [tz_bf], ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [te_px]
    add ax, 18
    mov bx, [te_oy]
    add bx, TE_TOPY + 50
    mov cx, ax
    add cx, [tz_bf]
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tz_frac - out AX = how much of the bar the committed bytes have filled
;
; Both figures are shifted right together until the SIZE fits sixteen bits, so
; the multiply below cannot overflow: pos <= size, so pos*(bw-4) fits DX:AX and
; the quotient is at most the width.
; -----------------------------------------------------------------------------
tz_frac:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [tz_fsz]
    mov dx, [tz_fsz+2]
    mov si, [tz_pos]
    mov di, [tz_pos+2]
.red:
    or dx, dx
    jz .fit
    shr dx, 1
    rcr ax, 1
    shr di, 1
    rcr si, 1
    jmp short .red
.fit:
    ; **NOTHING ENFORCED THE PREMISE, AND `div` RAISES #DE.** The header above
    ; says "pos <= size, so the quotient is at most the width" - and [tz_fsz] is
    ; a decimal field the SENDER wrote into the ZFILE info block while [tz_pos]
    ; is bytes this end has committed. They are independent numbers. A ZFILE
    ; declaring size 1 makes the first commit's quotient 2,539,520, which does
    ; not fit AX, and this kernel installs no `int 0` handler - so a one-line
    ; lie on the wire was an uncontrolled far jump. **Never divide by anything
    ; the wire said without a check.**
    or di, di
    jnz .full                       ; the position's high word survived the
                                    ; shift loop, which only runs while the
                                    ; SIZE's does: a 16-bit size against a
                                    ; 17-bit position, so SI is the low word of
                                    ; a number it is not
    or ax, ax
    jz .none                        ; a sender that declared no size at all
    mov bx, ax                      ; BX = the size, sixteen bits
    mov ax, si
    cmp ax, bx
    jae .full                       ; pos >= size: the bar is FULL, and a sender
    mov cx, [tz_bw]                 ; that lied is not worth an #DE
    sub cx, 4
    mul cx                          ; DX:AX = pos * the inner width
    div bx
    jmp short .out
.full:
    mov ax, [tz_bw]
    sub ax, 4
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; te_tx_prog - the same line on row 25 of a FULL-SCREEN session
;
; It costs one row of the board's screen for the duration and nothing after it:
; tz_end marks row 24 and te_tx_owed re-emits it from the buffer.
; -----------------------------------------------------------------------------
te_tx_prog:
    push ax
    push cx
    push si
    push di
    push es
    call tz_text
    mov es, [te_tseg]
    mov di, (TET_ROWS - 1) * TET_COLS * 2
    mov si, tz_msg
    mov ah, TET_SATTR
    mov cx, TET_COLS - 12           ; ...and never over the leave hint's own
    cld                             ; twelve cells at the end of the row
.h:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    stosw
    loop .h
    jmp short .out
.pad:
    mov al, ' '
    rep stosw
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- tz_label - Receive File is greyed with no session (SPEC.md 47) ----------
tz_label:
    push ax
    mov ax, tz_it_rx0
    cmp byte [te_state], TS_UP
    jne .set
    mov ax, tz_it_rx
.set:
    mov [te_i_sess + 4*2], ax
    pop ax
    ret

; --- tz_ask - Session > Receive File (Zmodem): ASKED, not done ---------------
; The wire belongs to the worker (SPEC.md 70.2) and te_pnd has exactly one
; writer, so this sets a byte and the worker's next pass starts the transfer.
tz_ask:
    mov byte [tz_want], 1
    ret

tz_s_hdr:   db 'Receiving by Zmodem', 0
tz_s_wait:  db '(waiting)', 0
tz_s_big:   db 'Cluster ', 0
tz_s_big2:  db ' too big', 0
tz_s_derr:  db 'Disk error', 0
tz_s_dfull: db 'Folder full', 0
tz_s_full:  db 'Disk full', 0
tz_s_wprot: db 'Disk is write-protected', 0
tz_s_prot:  db 'That name is protected', 0
tz_s_tmo:   db 'Sender stopped', 0
tz_s_scan:  db 'Cancelled by the host', 0
; --- ONE STRING AND TWO LABELS, which is what MENU_DIS's shape allows ------
; The greyed spelling of a menu item is the plain one with a single prefix
; byte in front of it (os88api.inc), so tz_it_rx0 is tz_it_rx MINUS ONE and a
; second copy of the caption is twenty-five bytes of a package whose 360KB
; floppy has two clusters left (SPEC.md 24.3.1). te_it_ice/te_it_icy next door
; are two DIFFERENT captions and cannot do this; these two are one caption.
tz_it_rx0:  db MENU_DIS
tz_it_rx:   db 'Receive File (Zmodem)...', 0
