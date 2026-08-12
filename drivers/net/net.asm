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
;
; ...AND A RUN IS ONE COMMAND, which is the other 37%. Set 40 measured a
; document open at ~10 s and found 3.73 s of it was `lp_turn` - a whole system
; tick per direction reversal, twice per SECTOR, because this asked for one
; sector at a time. The protocol always carried a count byte and dsk_xfer
; always handed over a coalesced run; the driver threw it away. Batched, a
; 34-sector open is 2 reversals instead of 68: 3.73 s -> ~0.1 s. It is
; SPEC.md 18.91's floppy batching in a new place, and the shape is worth
; recognising - a cost model built for streaming, met by a caller that does
; not stream.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Network', DRVC_FILE, net_entry

; --- the wire protocol, master side ------------------------------------------
; os8088 is always the master and never receives unsolicited data (NET-PLAN
; 1.3), so every exchange below is request-then-response and the multiplexer
; NET-PLAN 5.3 will need for sockets is a busy flag and nothing more.
;
;   'I'  -> reply: status, sectors word, flags byte (bit 0 = read-only)
;   'R'  -> LBA word, count byte; reply: count x {status, 512 bytes}
;   'W'  -> LBA word, count byte, count x 512 bytes; reply: count status bytes
;   'X'  -> the far side may go back to listening
;
; `status` is shaped like an int 13h one because that is what DSV_BLK answers
; with (SPEC.md 51.8): 0 = ok, 03h = write protected, 04h = sector not found.
;
; **A RUN IS ONE COMMAND, AND EVERY FRAME IN IT IS A FIXED SIZE WHATEVER THE
; STATUS SAYS.** Both halves of that matter. One command per run is the point
; of the batching - lp_turn spends a whole system tick per direction reversal,
; so a per-sector command was two ticks of turnaround on top of 137 ms of data
; (PERFORMANCE.md Set 40). And the fixed frame is what makes a REFUSAL safe:
; a refused sector still carries its 512 bytes, so neither end can be left
; talking into one that has stopped listening. The master remembers the first
; refusal, consumes the rest of the run, and answers with it - the link stays
; up, and only the transfer failed.
NC_INFO     equ 'I'
NC_READ     equ 'R'
NC_WRITE    equ 'W'
NC_BYE      equ 'X'

; --- and file mode's, SPEC.md 62.10.1 ----------------------------------------
; One wire command per FSV_* verb, so no command means two things. Phase 1 is
; the three that MOUNT AND LIST; the rest are pinned in 62.10.1 and land with
; their milestones, exactly as the RAM disk's did.
NF_LIST     equ 'L'             ; handle -> status, count, count x 32 bytes
NF_CHDIR    equ 'C'             ; handle -> status, PARENT handle
NF_DFREE    equ 'F'             ; -> status, free dword, granule word

NST_OK      equ 0x00
NST_WPROT   equ 0x03
NST_NOSEC   equ 0x04
NST_IO      equ 0x20            ; the link went away mid-transfer

NET_RUN     equ 64              ; sectors per command - see net_runlen

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
    dw 0                        ; DSV_BLK     - NO SECTORS: this volume is
                                ;               served by answering questions
                                ;               about FILES (SPEC.md 62.10)
    dw net_cpname               ; DSV_CPNAME  - the page exists while we do
    dw net_cp_paint             ; DSV_CPPAINT
    dw net_cp_click             ; DSV_CPCLICK
    dw 0                        ; DSV_CPCLOSE
    dw net_fsv                  ; DSV_FS      - ...through this table

; The FSV_* verbs (SPEC.md 62.9.1). A cell of 0 is "not implemented", which
; drv_fs_call answers as an ordinary refusal rather than a fault - so a phase
; lands its own verbs and the ones after it decline cleanly meanwhile.
net_fsv:
    dw net_list                 ; FSV_LIST
    dw net_chdir                ; FSV_CHDIR
    dw 0                        ; FSV_STAT
    dw 0                        ; FSV_READ
    dw 0                        ; FSV_WRITE
    dw 0                        ; FSV_APPEND
    dw 0                        ; FSV_READAT
    dw 0                        ; FSV_DELETE
    dw 0                        ; FSV_RENAME
    dw 0                        ; FSV_MKDIR
    dw 0                        ; FSV_RMDIR
    dw net_dfree                ; FSV_DFREE
    dw 0                        ; FSV_ENUM
net_fsv_end:
%if net_fsv_end - net_fsv != FSV_SIZE
  %error "net: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

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
    mov al, NC_BYE              ; SHAKE OFF A STALE SESSION FIRST. The slave's
    call lp_sbyte               ; command wait is unbounded (lplslv.inc), so a
                                ; partner still serving a link we have since
                                ; lost - an unplugged cable, a driver reloaded,
                                ; a Disconnect that did not reach it - is
                                ; sitting in lp_rbyte_w and would read the
                                ; magic below as commands and discard it. A
                                ; bye costs one byte, is ignored by a slave
                                ; that is already hunting, and is what makes
                                ; Connect work TWICE. Its result is deliberately
                                ; not tested: there may be nothing there at all,
                                ; which is the ordinary case
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
    mov byte [net_lost_f], 0    ; a fresh link is not the old one's failure
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
; FSV_LIST - the folder we are STANDING IN, one entry at a time
;            (SPEC.md 62.9.1/62.10.1)
; in:  nothing - the driver holds its own cwd, put there by FSV_CHDIR
; out: CF=0; CF=1 = the link went away
;
; The wire hands back a COUNT and then that many 32-byte SPEC.md 19.1 entries,
; and each is passed to OSAPI_FS_ENT unreshaped - the far side's findfirst
; builds the entry the Disk window's row is drawn from, and nothing in between
; touches it. The kernel still SORTS (19.4) and still synthesizes '..' (19.5),
; so this must do neither.
;
; It takes NO argument, which is the ABI's own decision and worth stating
; because the first draft passed a handle: the kernel calls FSV_LIST at the
; end of a mount, about the folder FSV_CHDIR last stood in, so the handle
; would be a second opinion about where we are - and two places deciding that
; is how a listing stops describing the folder the hit-test resolves against.
; [net_cwd] is the one opinion; the wire still carries it, because the FAR
; side has a cwd of its own and the two have to agree.
;
; A refused entry ends the APPEND but NOT the read: the count is on the wire
; before the entries are, so the run is consumed whatever the listing does
; with it, and neither end is left talking into one that stopped listening.
; -----------------------------------------------------------------------------
net_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [net_full], 0      ; a fresh listing is not the last one's cap
    mov bl, NF_LIST
    mov ax, [net_cwd]
    call net_fcmd_h             ; the command and the folder it is about
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .bad
    call lp_rword               ; how many entries follow
    jc  .bad
    mov di, ax
.each:
    or  di, di
    jz  .done
    dec di
    mov cx, DSK_DE_SIZE         ; ...each of them a staged 19.1 entry
    mov si, net_ent
.byte:
    call lp_rbyte
    jc  .bad
    mov [si], al
    inc si
    loop .byte
    cmp byte [net_full], 0      ; the listing filled up: keep READING - the
    jne .each                   ; run is fixed and the far side is mid-send
    mov si, net_ent
    call OSAPI_FS_ENT           ; ES is our own DS, set by the X stub
    jnc .each
    mov byte [net_full], 1
    jmp short .each
.done:
    clc                         ; ...AND NO net_bye. See net_fcmd's header:
    jmp short .out              ; NC_BYE ends the SESSION, not the command
.bad:
    call net_lost
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
; FSV_CHDIR - stand in a folder, and say what is above it (SPEC.md 62.9.1)
; in:  AX = a handle out of an entry, 0 = the root
; out: CF=0 and DX = THE PARENT'S HANDLE; CF=1 refused
;
; The parent comes back from the far side because the handle is ITS to assign
; and opaque here (62.9.1) - the kernel synthesizes '..' and has no directory
; sector to read one out of.
; -----------------------------------------------------------------------------
net_chdir:
    push bx
    push cx
    push si
    push di
    mov [net_arg2], ax          ; ...banked, because the reply overwrites AX
    mov bl, NF_CHDIR
    call net_fcmd_h
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .bad
    call lp_rword               ; the parent's handle
    jc  .bad
    mov [net_up], ax
    mov ax, [net_arg2]          ; ONLY NOW is the move real: a refused chdir
    mov [net_cwd], ax           ; must leave us standing where we were, or
    mov dx, [net_up]            ; FSV_LIST lists a folder we never reached
    clc
    jmp short .out
.bad:
    call net_lost
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_DFREE - free space, in BYTES (SPEC.md 62.9.1)
; out: CF=0 with DX:AX = free bytes and BX = the granule; CF=1 refused
; -----------------------------------------------------------------------------
net_dfree:
    push cx
    push si
    push di
    mov bl, NF_DFREE
    call net_fcmd               ; no argument on this one
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .bad
    call lp_rword
    jc  .bad
    mov [net_up], ax            ; low half, banked across the next two reads
    call lp_rword
    jc  .bad
    mov dx, ax
    call lp_rword               ; the granule
    jc  .bad
    mov bx, ax
    mov ax, [net_up]
    clc
    jmp short .out
.bad:
    call net_lost
    stc
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; net_fcmd / net_fcmd_h - open a FILE-mode command, with and without an
;                         argument word
; in:  BL = the NF_* letter;  net_fcmd_h also takes AX = the handle
; out: CF=1 = the link went away
;
; `net_cmd` is the BLOCK side's and takes AL + DX + [net_rlen]; these are not
; that, and the two must not share a name however alike they read - one wire
; command per verb is 62.10.1's rule, and one routine per command shape is the
; same rule in the driver.
;
; **AND NO COMMAND ENDS WITH NC_BYE.** It reads like a frame terminator and it
; is not one: `serve` on the far side LEAVES its command loop on NC_BYE and
; goes back to hunting for the magic (lplslv.inc), so a bye after every verb
; tore the session down and the next command arrived at a slave that was no
; longer listening for one. The gap between commands is the user's THINKING
; TIME and has no upper bound - which is exactly what lp_rbyte_w's unbounded
; wait was built for, and this would have defeated it. NC_BYE is net_drop's
; and net_connect's alone.
;
; It survived a whole scripted session against tests/lptlink/partner.py, whose
; server read a bye as "carry on" - a harness MORE FORGIVING than the thing it
; stands in for hides exactly the bugs it exists to find, so partner.py returns
; on one now, as the real far end does.
; -----------------------------------------------------------------------------
net_fcmd_h:
    push ax
    mov [net_arg], ax
    mov al, bl
    call lp_sbyte
    jc  .bad
    mov ax, [net_arg]
    call lp_sword
    jc  .bad
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

net_fcmd:
    push ax
    mov al, bl
    call lp_sbyte
    pop ax
    ret

; net_bye - let the far side go back to listening. Best effort by contract:
; the answer is already in hand and a failed goodbye is the next command's
; problem, which net_connect's own leading NC_BYE is there to clear up.
net_bye:
    push ax
    mov al, NC_BYE
    call lp_sbyte
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_mount - register the volume and mount it
; out: CF=0 mounted; CF=1 refused
;
; The mount ITSELF is what proves the far side is serving files rather than
; sectors: disk_mount branches on DVK_FILE and ends in FSV_LIST (SPEC.md
; 62.9.1), so a partner that answered the magic but cannot list is refused
; here rather than at the first double-click.
; -----------------------------------------------------------------------------
net_mount:
    push bx
    push cx
    push dx
    push si
    push di
    mov word [net_cwd], 0       ; a fresh link stands in the far side's root,
                                ; whatever the last one was standing in
    mov al, 0                   ; our own volume handle: there is one link, so
    xor cx, cx                  ; one volume, and the handle is decoration.
                                ; NO SECTOR COUNT - a DVK_FILE volume has no
                                ; sectors at all and dsk_xfer refuses it
                                ; (SPEC.md 62.9); the kind follows the CLASS,
                                ; so changing DRVC_NET to DRVC_FILE is the
                                ; whole of what makes this a file volume
    xor dx, dx                  ; no donated listing claim: the .lowbss floor
    mov si, net_label
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

    mov byte [net_st], 0        ; the first refusal of the whole transfer

; --- read ---------------------------------------------------------------------
; ONE COMMAND FOR THE RUN, then `count` frames of {status, 512 bytes} coming
; the other way. Two direction reversals for the whole run instead of two per
; sector, which is what this change is for (PERFORMANCE.md Set 40).
.rrun:
    call net_runlen             ; [net_rlen] = min(BP, NET_RUN), AL = it
    mov al, NC_READ
    call net_cmd
    jc  .lost
    mov al, [net_rlen]
    mov [net_rcnt], al
.rsec:
    call lp_rbyte               ; this sector's status
    jc  .lost
    or  al, al
    jz  .rok
    cmp byte [net_st], 0        ; remember the FIRST one and KEEP CONSUMING:
    jne .rok                    ; the frame is fixed at 512 bytes whatever the
    mov [net_st], al            ; far side thinks of the sector, so bailing
.rok:                           ; out here would leave it sending into a
    mov cx, 512                 ; master that has stopped listening - a desync
.rbyte:                         ; rather than an error
    call lp_rbyte
    jc  .lost
    stosb                       ; ES:DI, which is why cld matters above
    loop .rbyte
    inc dx
    dec bp
    dec byte [net_rcnt]
    jnz .rsec
    or  bp, bp
    jnz .rrun
    jmp short .done

; --- write --------------------------------------------------------------------
; The mirror: one command, then `count` x 512 bytes out, then `count` status
; bytes back. The statuses cannot come per sector without a reversal per
; sector, which is the cost this is removing.
.write:
    test byte [net_flags], 1
    jnz .wprot
    mov byte [net_st], 0
.wrun:
    call net_runlen
    mov al, NC_WRITE
    call net_cmd
    jc  .lost
    mov al, [net_rlen]
    mov [net_rcnt], al
.wsec:
    mov cx, 512
.wbyte:
    mov al, [es:di]
    inc di
    call lp_sbyte
    jc  .lost
    loop .wbyte
    inc dx
    dec bp
    dec byte [net_rcnt]
    jnz .wsec
    mov al, [net_rlen]          ; ...and now the run's verdicts, one per sector
    mov [net_rcnt], al
.wst:
    call lp_rbyte
    jc  .lost
    or  al, al
    jz  .wok
    cmp byte [net_st], 0
    jne .wok
    mov [net_st], al
.wok:
    dec byte [net_rcnt]
    jnz .wst
    or  bp, bp
    jnz .wrun

.done:
    mov al, [net_st]            ; a refusal anywhere in the transfer is the
    or  al, al                  ; transfer's answer, and the LINK is still up
    jnz .status
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
; net_runlen - how many sectors the next command carries
; in:  BP = sectors still wanted
; out: [net_rlen] = min(BP, NET_RUN); AX preserved
;
; NET_RUN caps it for two reasons and neither is the wire. The count crosses
; as ONE BYTE, so 255 is the protocol's own ceiling; and dsk_xfer hands a
; driver volume the WHOLE request uncapped (SPEC.md 18.7 - a driver has no
; revolution to save and splits its own runs), so a big file read would
; otherwise arrive as one enormous frame. 64 sectors is 32KB, which is two
; reversals per 8.8 s of data: the turnaround is already down in the noise
; there and a shorter frame loses less when a cable is pulled mid-run.
; -----------------------------------------------------------------------------
net_runlen:
    push ax
    mov al, NET_RUN
    cmp bp, NET_RUN
    jae .cap
    mov ax, bp                  ; BP < NET_RUN, so its low byte IS the count
.cap:
    mov [net_rlen], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_cmd - AL = the command, DX = the LBA, [net_rlen] = the count.
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
    mov al, [net_rlen]          ; ...and how many sectors this run carries
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
    mov [net_lastlba], dx       ; WHERE it died, for the page to report.
    mov byte [net_lost_f], 1    ; A link that drops on a real cable drops for
    ret                         ; a reason no emulator here can reproduce, so
                                ; the page has to carry the evidence out - the
                                ; SPEC.md 18.94 discipline, one layer up. DX is
                                ; net_blk's walking LBA at every one of the
                                ; four sites that reach here

%include "netui.inc"            ; the Control Panel page (SPEC.md 31.9)
%include "lplink.inc"           ; ...and the transport, shared with tests/lptlink
%include "os88ui.inc"           ; ...and the standard control (SPEC.md 20.5.1)

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
net_lost_f: db 0                ; 1 = the last link ended by dying, not by us
net_lastlba: dw 0               ; ...and the sector it was on when it did
net_rlen:   db 0                ; sectors in the command just sent
net_rcnt:   db 0                ; ...and how many of them are still to go
net_st:     db 0                ; the FIRST refusal in this transfer
net_px:     dw 0
net_py:     dw 0

; --- file mode's (SPEC.md 62.10) ---------------------------------------------
; net_label is the volume's name and NOT the drive letter: the kernel assigns
; the letter from the volume index (SPEC.md 26.4), exactly as it does for
; `HDD C`, so this is what the desktop zone and the Disk window's header say.
net_label:  db 'Link', 0
net_cwd:    dw 0                ; the far side's handle for where we STAND.
                                ; Ours as well as theirs: FSV_LIST takes no
                                ; argument, so this is the kernel's only way
                                ; back to the folder it just chdir'd into
net_arg:    dw 0                ; net_fcmd_h's argument, across the send
net_arg2:   dw 0                ; ...and net_chdir's, across the whole reply
net_up:     dw 0                ; the parent handle the far side answered with
net_full:   db 0                ; the kernel's listing filled: keep READING the
                                ; run, stop APPENDING to it
net_ent:    times DSK_DE_SIZE db 0  ; one staged 19.1 entry, off the wire

    OS88_DRV_END
