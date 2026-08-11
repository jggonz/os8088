; =============================================================================
; os8088 - NET.DRV
;
; A LapLink parallel cable to a DOS machine, as a BLOCK VOLUME: the far side
; serves 512-byte sectors out of an image file and os8088 mounts them as an
; ordinary FAT12/16 drive. Stage 1 of docs/NET-PLAN.md.
;
; WHY BLOCK MODE FIRST, when the ask was "browse the remote machine's files".
; Because everything above dsk_xfer already works: the BPB validator, the FAT
; window, the directory walker, the whole write path with its commit ordering,
; the Disk window, the Standard File dialog, the copy engine, the loader and
; the association cache. A block volume costs the KERNEL 172 bytes and this
; driver, and it proves the wire, the framing, the Control Panel page and the
; desktop zone with no new failure mode anywhere above the transport. Stage 2
; (NET-PLAN 2.2) is the file redirector that supersedes it, and it is ~400
; more bytes of kernel across twelve branch sites - worth doing on a transport
; somebody has already used in anger.
;
; THE ONE KERNEL CHANGE IT NEEDED is DV_CLASS (SPEC.md 18.7): dsk_xfer used to
; dispatch every driver-backed sector through the DISK class's published
; DSV_BLK, hard-coded, which is right while one class serves blocks and
; silently wrong the moment two do. The machine this project is calibrated
; against has an ST-225 in it, so "a network drive that cannot coexist with
; the hard-disk driver" was a real collision and not a hypothetical.
;
; WHAT IT COSTS A MACHINE THAT DOES NOT USE IT: one drv_tab row and a file on
; the floppy. DRVR_WANT is 0 like every other row (SPEC.md 51.3), and this one
; has a particular reason to stay that way - it WRITES to parallel ports to
; find its cable, and a port with a printer on it is a real machine.
;
; SPEED, MEASURED: 3,741 bytes/second (PERFORMANCE.md Part 9 Set 39), which is
; 5.7x slower than the 5150's own floppy. That is the honest figure and the
; feature is not about it: what the cable buys is that a file crosses it
; without the seven-step path in docs/FIELD-MACHINES.md. A 512-byte sector is
; ~137 ms, so a mount is a second or two - the same order as the floppy it
; sits beside, because a floppy pays revolutions where this pays nibbles.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Network', DRVC_NET, net_entry

; --- the wire protocol, master side ------------------------------------------
; os8088 is always the master and never receives unsolicited data (NET-PLAN
; 1.3), so every exchange below is request-then-response and the multiplexer
; NET-PLAN 5.3 will need for sockets is a busy flag and nothing more.
;
;   'I'  -> reply: status, sectors word, flags byte (bit 0 = read-only)
;   'R'  -> LBA word, count byte; reply: status, then count*512 bytes
;   'W'  -> LBA word, count byte, then count*512 bytes; reply: status
;   'X'  -> the far side may go back to listening
;
; `status` is shaped like an int 13h one because that is what DSV_BLK answers
; with (SPEC.md 51.8): 0 = ok, 03h = write protected, 04h = sector not found.
NC_INFO     equ 'I'
NC_READ     equ 'R'
NC_WRITE    equ 'W'
NC_BYE      equ 'X'

NST_OK      equ 0x00
NST_WPROT   equ 0x03
NST_NOSEC   equ 0x04
NST_IO      equ 0x20            ; the link went away mid-transfer

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). DSV_BLK is the whole of what the kernel
; dispatches; the three CP cells are the page it owns while it is attached.
; -----------------------------------------------------------------------------
net_svc:
    dw 0                        ; DSV_CAPS    - sound's, not ours
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw net_name                 ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw net_blk                  ; DSV_BLK     - every sector comes through here
    dw net_cpname               ; DSV_CPNAME  - the page exists while we do
    dw net_cp_paint             ; DSV_CPPAINT
    dw net_cp_click             ; DSV_CPCLICK
    dw 0                        ; DSV_CPCLOSE

net_name:   db 'Parallel Link', 0
net_cpname: db 'Network', 0

; =============================================================================
; ENTRY
; =============================================================================
net_entry:
    cmp al, DRVV_ATTACH
    je  net_attach
    cmp al, DRVV_DETACH
    je  net_detach
    cmp al, DRVV_READY
    je  net_ready
    clc                         ; a verb we do not implement is not an error
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find a parallel port, and hook NOTHING
; out: CF=0 and SI = the service table; CF=1 = no hardware
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1), and easy to honour here because there
; is nothing to hook: no interrupt, no vector, no heap. The scan writes two
; values to each candidate's data register and puts back what it found, and a
; port that answers neither is left alone entirely.
;
; It does NOT connect. Finding a port is a fact about the machine; finding a
; PARTNER is a fact about the moment, and it belongs where the user can see it
; fail - the Control Panel page, and DRVV_READY's one attempt below.
; -----------------------------------------------------------------------------
net_attach:
    call lpl_scan
    call net_pick               ; CF=0 and [lp_base] set if any port answered
    jc  .none
    mov byte [net_state], NS_PORT
    mov si, net_svc
    clc
    ret
.none:
    mov byte [net_state], NS_NOPORT
    stc                         ; DRVE_NOHW: the Drivers page reports it
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the earliest point a fence keyed on our publication will answer
;              (SPEC.md 51.2.2), so this is where a volume may be added
;
; One attempt at the cable. It is allowed to fail and says so on the page
; rather than in a notice window: a driver whose far end is not switched on
; yet is an ordinary state, not an error, and the user has a Connect button.
; -----------------------------------------------------------------------------
net_ready:
    cmp byte [net_state], NS_PORT
    jne .out
    call net_connect            ; CF=1 = nobody answered; the page says so
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
;
; The kernel drops our volumes itself before it frees the image - and since
; DV_CLASS it drops OURS and not the hard disk's - but this says goodbye to
; the far side first so it goes back to listening rather than waiting out a
; command that will never come, and puts the port back exactly as found.
; -----------------------------------------------------------------------------
net_detach:
    cmp byte [net_state], NS_LINKED
    jne .port
    mov al, NC_BYE
    call lp_sbyte               ; best effort: if it fails, it fails
.port:
    cmp byte [net_state], NS_NOPORT
    je  .done
    call lp_restore             ; the control byte back as we found it, which
.done:                          ; is NET-PLAN 1.4.3's whole point
    mov byte [net_state], NS_NOPORT
    mov byte [net_vol], 0xFF
    ret

; =============================================================================
; CONNECTING
; =============================================================================

; -----------------------------------------------------------------------------
; net_pick - choose a port out of the scan: the first that answered a latch
; out: CF=0 and lp_setport done; CF=1 = none answered
; -----------------------------------------------------------------------------
net_pick:
    push ax
    push bx
    push cx
    push di
    mov cl, [ncand]
    or  cl, cl
    jz  .none
    xor ch, ch
    xor di, di
.next:
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .skip
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_setport
    clc
    jmp short .out
.skip:
    inc di
    loop .next
.none:
    stc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_connect - say hello, ask what is over there, and mount it
; out: CF=0 linked and mounted; CF=1 = no partner (the state byte says which)
;
; THE SWEEP IS THE SCAN'S ORDER, and it stops at the first port that answers
; the magic. NET-PLAN 1.4.4: a port with nothing on it, and a port with a
; PRINTER on it, both read as a constant status byte - only a live partner
; reads as whatever we ask it to, which is what the magic exchange tests.
; -----------------------------------------------------------------------------
net_connect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [ncand]
    or  cl, cl
    jz  .nolink
    xor ch, ch
    xor di, di
.try:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call mst_hello              ; the magic, and the partner's version byte
    jc  .nope
    pop cx
    jmp short .linked
.nope:
    call lp_restore
.next:
    inc di
    pop cx
    loop .try
.nolink:
    mov byte [net_state], NS_PORT
    stc
    jmp short .out

.linked:
    mov byte [net_state], NS_LINKED
    call net_info               ; how big is it, and may we write to it?
    jc  .lost
    call net_mount              ; osapi_vol_add + osapi_vol_mount
    jc  .lost
    clc
    jmp short .out
.lost:
    call lp_restore
    mov byte [net_state], NS_PORT
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_info - the 'I' command: sector count and the read-only flag
; out: CF=0 and [net_secs] / [net_flags] set; CF=1 = the link went away
; -----------------------------------------------------------------------------
net_info:
    push ax
    mov al, NC_INFO
    call lp_sbyte
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .bad
    call lp_rword               ; sectors
    jc  .bad
    mov [net_secs], ax
    call lp_rbyte               ; flags: bit 0 = read-only
    jc  .bad
    mov [net_flags], al
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; net_mount - register the volume and mount it
; out: CF=0 mounted; CF=1 refused
;
; The kernel names it (SPEC.md 26.4: 'N:' falls out of the volume index, like
; every other drive), so SI = 0 and DV_LBL stays empty. DX = 0 donates no
; listing claim, which means the .lowbss floor and a 32-entry listing - Stage 1
; is a 32MB image and a claim is Stage 2's problem if it is ever one.
; -----------------------------------------------------------------------------
net_mount:
    push bx
    push cx
    push dx
    push si
    push di
    mov al, 0                   ; our own volume handle: there is one link, so
    mov cx, [net_secs]          ; one volume, and the handle is decoration
    xor dx, dx
    xor si, si
    call OSAPI_VOL_ADD
    jc  .bad
    mov [net_vol], al
    call OSAPI_VOL_MOUNT        ; AL survives: it is the volume index
    jc  .bad
    clc
    jmp short .out
.bad:
    mov byte [net_vol], 0xFF
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; net_drop - take the volume away and go back to "a port, no partner"
; -----------------------------------------------------------------------------
net_drop:
    push ax
    cmp byte [net_vol], 0xFF
    je  .novol
    mov al, [net_vol]
    call OSAPI_VOL_DEL
    mov byte [net_vol], 0xFF
.novol:
    cmp byte [net_state], NS_LINKED
    jne .noling
    mov al, NC_BYE
    call lp_sbyte
    call lp_restore
    mov byte [net_state], NS_PORT
.noling:
    pop ax
    ret

; =============================================================================
; DSV_BLK - every sector of the volume comes through here (SPEC.md 51.8)
; in:  AL = 0 read / 1 write, AH = our volume handle, SI = volume-relative LBA,
;      CX = sector count, DX:BX = the buffer
; out: CF=0 done; CF=1 and AL = an int 13h status byte
;
; It runs with [sch_lock] raised and the gfx lock held, exactly as an int 13h
; does - so the machine is frozen for the duration and every wait inside the
; transport is bounded in ticks, which is NET-PLAN 1.3 rule 1 and is why a
; cable pulled out mid-sector reports an error instead of hanging the machine.
;
; DX:BX AND NOT ES:BX, which is the ABI's own decision (SPEC.md 51.8): the
; buffer is in LOW_SEG, the FAT window or a heap claim and never in this
; driver's segment or the kernel's, so ES cannot carry the meaning it carries
; everywhere else. `mov es, dx` is ours to do.
; =============================================================================
net_blk:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    cmp byte [net_state], NS_LINKED
    jne .nolink
    or  cx, cx
    jz  .ok                     ; a zero-sector transfer moves nothing
    cld                         ; the read path is a stosb loop and the driver
                                ; ABI promises nothing about DF

    mov es, dx                  ; the buffer's segment is ours to install
    mov di, bx
    mov bp, cx                  ; sectors remaining
    mov dx, si                  ; the LBA walks

    or  al, al
    jnz .write

; --- read ---------------------------------------------------------------------
.rloop:
    mov al, NC_READ
    call net_cmd                ; command + LBA + a count of ONE
    jc  .lost
    call lp_rbyte               ; status
    jc  .lost
    or  al, al
    jnz .status
    mov cx, 512
.rbyte:
    call lp_rbyte
    jc  .lost
    stosb                       ; ES:DI, which is why cld matters below
    loop .rbyte
    inc dx
    dec bp
    jnz .rloop
    jmp short .ok

; --- write --------------------------------------------------------------------
.write:
    test byte [net_flags], 1
    jnz .wprot
.wloop:
    mov al, NC_WRITE
    call net_cmd
    jc  .lost
    mov cx, 512
.wbyte:
    mov al, [es:di]
    inc di
    call lp_sbyte
    jc  .lost
    loop .wbyte
    call lp_rbyte               ; status, after the data
    jc  .lost
    or  al, al
    jnz .status
    inc dx
    dec bp
    jnz .wloop

.ok:
    xor al, al
    clc
    jmp short .out
.status:                        ; the far side refused this sector and said why
    stc
    jmp short .out
.wprot:
    mov al, NST_WPROT
    stc
    jmp short .out
.nolink:
    mov al, NST_NOSEC           ; no cable: every sector is "not found", which
    stc                         ; a mount reads as "not a FAT volume"
    jmp short .out
.lost:
    call net_lost               ; the link died under us - drop the volume
    mov al, NST_IO
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; net_cmd - AL = the command, DX = the LBA. One sector per command.
; out: CF=1 = the link went away
;
; ONE SECTOR AT A TIME, deliberately, and it is not the obvious choice. The
; floppy batches a run into one int 13h because a CALL there costs a whole
; revolution whatever it moves (PERFORMANCE.md Part 2) - the cable has no
; revolution, so a command costs its four bytes and nothing else: about 1 ms
; against the 137 ms of the sector it introduces, under 1%. What batching
; WOULD buy is one turnaround per run instead of per sector, and a turnaround
; is lp_turn's whole tick - so it is worth having and it is Stage 1's to leave
; alone, because a multi-sector reply also needs a resync story for a link
; that dies halfway through one.
; -----------------------------------------------------------------------------
net_cmd:
    push ax
    call lp_sbyte
    jc  .bad
    mov ax, dx
    call lp_sword               ; the LBA, low byte first
    jc  .bad
    mov al, 1                   ; ...and a count of one
    call lp_sbyte
    jc  .bad
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; net_lost - the link went away mid-transfer
;
; It CANNOT call osapi_vol_del from here: DSV_BLK is dispatched from inside
; dsk_xfer with [sch_lock] raised, and dropping a volume repaints the desktop
; (SPEC.md 26.3 defers that, but the volume table moving under a transfer in
; progress is a different question). So it records the state and lets the
; Control Panel page - or the next Connect - clean up. The kernel meanwhile
; sees CF=1 and a status byte, which is exactly what a floppy with the door
; open looks like to it.
; -----------------------------------------------------------------------------
net_lost:
    mov byte [net_state], NS_PORT
    ret

%include "netui.inc"            ; the Control Panel page (SPEC.md 31.9)
%include "lplink.inc"           ; ...and the transport, shared with tests/lptlink

; -----------------------------------------------------------------------------
; lpl_ticks - lplink.inc's one requirement of its includer (see its header)
; out: AX = the system tick counter; everything else preserved
; -----------------------------------------------------------------------------
lpl_ticks:
    call OSAPI_GET_TICKS
    ret

; --- state -------------------------------------------------------------------
net_state:  db NS_NOPORT
net_vol:    db 0xFF             ; the volume index we registered, FF = none
net_secs:   dw 0                ; what the far side says its image holds
net_flags:  db 0                ; bit 0 = it will not take writes
net_px:     dw 0
net_py:     dw 0

    OS88_DRV_END
