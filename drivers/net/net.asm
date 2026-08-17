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

    OS88_DRIVER 'os88net', DRVC_FILE, net_entry

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
; READ is 'G' and WRITE will be 'U' because 'R' and 'W' ARE ALREADY TAKEN, by
; NC_READ and NC_WRITE four lines above - the same one-byte space, and on the
; DOS side the same command loop. SPEC.md 62.10.1 was pinned from the verb
; names alone and carried both collisions until the read path was built.
NF_LIST     equ 'L'             ; handle -> status, count, count x 32 bytes
NF_CHDIR    equ 'C'             ; handle -> status, PARENT handle
NF_DFREE    equ 'F'             ; -> status, free dword, granule word
NF_STAT     equ 'S'             ; handle, 13-byte name -> status, handle,
                                ;                    size dword, attribute
NF_READ     equ 'G'             ; handle, cap dword -> status, len dword, bytes
NF_READAT   equ 'A'             ; handle, off dword, cap word
                                ;              -> status, len word, len bytes
NF_WRITE    equ 'U'             ; folder, name, len dword, bytes -> status
NF_APPEND   equ 'P'             ; folder, name, len WORD, bytes -> status
NF_DELETE   equ 'D'             ; folder, name -> status
NF_RENAME   equ 'N'             ; folder, old name, new name -> status
NF_MKDIR    equ 'M'             ; folder, name -> status
NF_RMDIR    equ 'K'             ; folder, name -> status
NF_COPY     equ 'Y'             ; src folder, dst folder, name -> status. 'Y'
                                ; because C, D, F, G, K, L, M, N, P, S, U and
                                ; A are taken above and R, W, I, X are block
                                ; mode's - the one-byte space is shared, which
                                ; SPEC.md 62.10.1 learned the hard way
NF_ENUM     equ 'E'             ; folder, ordinal -> status, ONE 32-byte entry
NF_RMTREE   equ 'T'             ; folder, name -> status. RMDIR's frame exactly,
                                ; and a different verb because it means
                                ; something a caller must ask for on purpose

NET_PCHUNK  equ 64              ; bytes between OSAPI_FS_PROG reports, and
                                ; between destination re-normalisations. One
                                ; number for both because both want "often
                                ; enough to be smooth, rarely enough to be
                                ; free" and a second constant is a second
                                ; thing to keep in step. At 3,741 B/s a chunk
                                ; is ~17 ms, so the bar moves ~59 times a
                                ; second on a wire that cannot outrun it

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
    dw net_stat                 ; FSV_STAT
    dw net_read                 ; FSV_READ
    dw net_write                ; FSV_WRITE
    dw net_append               ; FSV_APPEND
    dw net_readat               ; FSV_READAT
    dw net_delete               ; FSV_DELETE
    dw net_rename               ; FSV_RENAME
    dw net_mkdir                ; FSV_MKDIR
    dw net_rmdir                ; FSV_RMDIR
    dw net_dfree                ; FSV_DFREE
    dw net_enum                 ; FSV_ENUM (SPEC.md 62.9.7/62.10.6) - the verb
                                ; a FOLDER copy walks its source with. This
                                ; carried 0 for two milestones, and the effect
                                ; was not a broken copy but a REFUSED one:
                                ; fcp_mkroot probes the cell with drv_fs_has
                                ; and answers FERR_PROT, so dragging a folder
                                ; onto or off the Link volume reported
                                ; `Protected` and moved nothing
    dw net_copy                 ; FSV_COPY (SPEC.md 62.9.8)
    dw net_rmtree               ; FSV_RMTREE (SPEC.md 62.10.7) - the far side
                                ; recurses with its own int 21h, so a tree
                                ; costs one command frame instead of an
                                ; FSV_ENUM and an FSV_DELETE per file
net_fsv_end:
%if net_fsv_end - net_fsv != FSV_SIZE
  %error "net: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

net_name:   db 'os88net', 0
net_cpname: db 'os88net', 0

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
    mov byte [lp_turnw], TURN_RX    ; ...AND SHORT AGAIN FOR THE GOODBYE.
                                ; 62.10.4.8 made lp_snib patient, which is
                                ; right for a far side that has gone to its
                                ; disk and wrong for one that is not there at
                                ; all: unticking the driver with the cable out
                                ; would spend REPLY_TMO - ten seconds of
                                ; frozen UI, under the gfx lock - on a byte
                                ; whose own comment says it is best effort.
                                ; Nothing after this needs the long one; the
                                ; next connect's lp_init sets it anyway
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
%ifndef NET_TURN1               ; ...`make NETTURN1=1` is the A/B, and leaving
                                ; the store out is the WHOLE of the old
                                ; behaviour: lp_init already set TURN_RX
    mov byte [lp_turnw], REPLY_TMO  ; ...AND THE WAIT GETS LONGER NOW. Until
                                ; this point every reversal was a port being
                                ; TRIED and a quick refusal was the whole
                                ; point; from here a reversal is the far side
                                ; going to its disk, which is seconds. See
                                ; REPLY_TMO in lplink.inc for the field bug
%endif
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
; out: CF=0; CF=1 = the far side refused, or the link went away
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
    jnz .no
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
.no:
    call lp_rword               ; A REFUSED LISTING IS STILL A FULL FRAME: the
    jc  .bad                    ; count follows the status whatever it said
    stc                         ; (62.10.1), so it is read and thrown away
    jmp short .out              ; rather than left on a wire the far side is
                                ; still driving - which ends the SESSION and
                                ; not the command. A far side saying `no such
                                ; folder` is not the link failing, so NO
                                ; net_lost: only .bad is transport
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
; FSV_ENUM - the Nth entry of a folder that is NOT the one on screen
;            (SPEC.md 62.9.7/62.10.6)
; in:  AX = the folder's handle, CX = the ordinal, DX:BX = a 32-byte buffer
;      (DX:BX and not ES:BX, because ES is KERNEL_SEG on entry to anything the
;      kernel far-calls - SPEC.md 51.8)
; out: CF=0 and the entry is in the caller's buffer;
;      CF=1 with AX = 0 = PAST THE LAST ENTRY, which is a normal end and not
;      an error; CF=1 with AX = FERR_* = this folder could not be walked
;
; This is what a FOLDER copy walks its source with, and it is NOT FSV_LIST:
; that appends into the kernel's one global listing and is about the folder
; the driver is STANDING in, where a recursive copy walks a folder several
; levels below it and must not disturb the mount. So the folder is an
; argument here and [net_cwd] is untouched.
;
; THE THREE STATUSES ARE THE WHOLE CONTRACT. `CF=1, AX=0` is letter for letter
; what the END of a folder answers, so a far side that cannot walk a folder at
; all must NOT say that - fcp_scan would read it as an empty subdirectory,
; report FCPS_DONE, and the paste would look like a success over a subtree it
; never copied. FERR_NOENT is the end; anything else is FERR_IO.
;
; The 32 bytes are on the wire whatever the status said (62.10.1's fixed
; frame), so they are READ before the status is acted on - a refusal that
; short-changed the frame would leave the far side driving nibbles at an end
; that had stopped listening, which ends the SESSION and not the command.
; -----------------------------------------------------------------------------
net_enum:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [net_earg], cx          ; the ordinal, across net_fcmd_h's send of AX
    mov [net_ebuf], bx          ; ...and the caller's buffer, both halves,
    mov [net_eseg], dx          ; because the reply spends BX, CX and DX
    mov bl, NF_ENUM
    call net_fcmd_h             ; the command and the FOLDER it is about
    jc  .bad
    mov ax, [net_earg]
    call lp_sword               ; ...and how far into it
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    mov [net_est], al
    mov cx, DSK_DE_SIZE
    mov si, net_ent
.byte:
    call lp_rbyte
    jc  .bad
    mov [si], al
    inc si
    loop .byte
    cmp byte [net_est], NST_OK
    jne .no
    mov es, [net_eseg]          ; ...and out to DX:BX. net_ent is staged first
    mov di, [net_ebuf]          ; rather than read straight into the caller's
    mov si, net_ent             ; buffer because a torn read must not leave
    mov cx, DSK_DE_SIZE         ; half an entry in it
    cld
    rep movsb
    clc
    jmp short .out
.no:
    xor ax, ax                  ; the end of the folder: CF=1, AX=0
    cmp byte [net_est], FERR_NOENT
    je  .end
    mov ax, FERR_IO             ; ...and a folder we could not walk is NOT
.end:                           ; that, however alike they read here
    stc
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
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
; FSV_CHDIR - stand in a folder, and say what is above it (SPEC.md 62.9.1)
; in:  AX = a handle out of an entry, 0 = the root
; out: CF=0 and DX = THE PARENT'S HANDLE; CF=1 and AX = FERR_*
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
    jnz .no
    call lp_rword               ; the parent's handle
    jc  .bad
    mov [net_up], ax
    mov ax, [net_arg2]          ; ONLY NOW is the move real: a refused chdir
    mov [net_cwd], ax           ; must leave us standing where we were, or
    mov dx, [net_up]            ; FSV_LIST lists a folder we never reached
    clc
    jmp short .out
.no:
    mov ax, FERR_NOENT          ; the far side's own refusal - status only, and
    stc                         ; a folder that is not there is not the cable
    jmp short .out              ; coming out. net_stat's two exits, here too
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
; out: CF=0 with DX:AX = free bytes and BX = the granule; CF=1 and AX = FERR_*
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
    jnz .no
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
.no:
    mov ax, FERR_NOENT          ; ...and the same two exits here: a refusal is
    stc                         ; status-only on the wire, so nothing is left
    jmp short .out              ; unread and nothing is lost
.bad:
    call net_lost
    stc
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - a name in the current folder -> a handle (SPEC.md 62.9.1)
; in:  SI -> a NUL 8.3 name, IN KERNEL_SEG - so ES, which drv_fs_call sets
;      from the kernel's own DS
; out: CF=0, AX = the handle, DX:CX = the size, BL = a FAT attribute byte
;      (0x10 = directory); CF=1 and AX = FERR_*
;
; THE NAME CROSSES AS A FIXED 13 BYTES, padded with NULs, because the frame
; has to be a fixed size for the same reason every other one here does: the
; far side must know when the argument ends without a length preceding it,
; and 8.3 plus a dot plus a terminator IS 13. A longer name cannot reach here
; - dskw_stat's callers all resolve 8.3 - and one that somehow did would be
; truncated rather than desynchronising the wire.
; -----------------------------------------------------------------------------
; IT PRESERVES SI AND DI AND NOTHING ELSE, which is not tidiness: AX, BX, CX
; and DX are all OUTPUTS here, and the first version pushed BX/CX/DX at the top
; and popped them at the bottom - throwing the handle's size and attribute
; away a few instructions after reading them off the wire. The verb would have
; answered a stale register triple with CF=0, which is the shape that fails
; convincingly: the link works, the file is found, and the kernel is told it
; is some other size.
net_stat:
    push si
    push di
    mov bl, NF_STAT
    mov ax, [net_cwd]           ; ...WHICH FOLDER, and not the far side's own
    call net_fcmd_h             ; memory of the last chdir - see net_list
    jc  .bad
    call net_sname              ; ES:SI -> 13 bytes on the wire
    jc  .bad
    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .no
    call lp_rword               ; the handle
    jc  .bad
    mov [net_hnd], ax
    call lp_rword               ; size, low
    jc  .bad
    mov [net_sz], ax
    call lp_rword               ; size, high
    jc  .bad
    mov [net_sz+2], ax
    call lp_rbyte               ; the attribute byte
    jc  .bad
    mov [net_att], al

    mov ax, [net_hnd]
    mov cx, [net_sz]
    mov dx, [net_sz+2]
    xor bx, bx
    mov bl, [net_att]
    clc
    jmp short .out
.no:
    mov ax, FERR_NOENT          ; a status the far side chose: the file is not
    stc                         ; there, which is not the link failing
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop di
    pop si
    ret

; net_sname - ES:SI's NUL name -> 13 bytes on the wire, NUL-padded
net_sname:
    push ax
    push cx
    push si
    mov cx, 13
    xor ah, ah                  ; AH = 1 once the NUL has been passed, so the
.b:                             ; tail is padded rather than reading on into
    or  ah, ah                  ; whatever follows the caller's string
    jnz .pad
    mov al, [es:si]
    inc si
    or  al, al
    jnz .snd
    mov ah, 1
.pad:
    xor al, al
.snd:
    call lp_sbyte
    jc  .bad
    loop .b
    pop si
    pop cx
    pop ax
    clc
    ret
.bad:
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_READ - the whole file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, DI:CX = its 32-bit capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = FERR_*
;
; THE CAPACITY GOES OUT WITH THE COMMAND, which is what keeps a refusal cheap:
; the far side answers min(size, cap), so an oversized file is short at the
; source rather than being sent and thrown away here. At 3,741 bytes/second a
; discarded 116KB module is half a minute of wire.
;
; It is still checked on arrival. A far end that ignores the cap is a far end
; writing past the end of somebody else's heap claim, and "the other machine
; is well behaved" is not a thing this side can know.
; -----------------------------------------------------------------------------
net_read:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov [net_hnd], ax
    mov [net_cap], cx           ; the capacity, banked for the check below
    mov [net_cap+2], di
    mov [net_bseg], dx
    mov [net_boff], bx

    mov bl, NF_READ
    call net_fcmd
    jc  .bad
    mov ax, [net_hnd]
    call lp_sword
    jc  .bad
    mov ax, [net_cap]           ; ...and the cap, low word first
    call lp_sword
    jc  .bad
    mov ax, [net_cap+2]
    call lp_sword
    jc  .bad

    call lp_rbyte               ; status
    jc  .bad
    or  al, al
    jnz .no
    call lp_rword               ; the length, low
    jc  .bad
    mov [net_len], ax
    call lp_rword               ; ...and high
    jc  .bad
    mov [net_len+2], ax

    mov ax, [net_len+2]         ; longer than we asked for? consume it and
    cmp ax, [net_cap+2]         ; refuse, rather than write past the claim -
    ja  .over                   ; the frame is fixed, so the bytes are coming
    jb  .fits                   ; whatever we do with them
    mov ax, [net_len]
    cmp ax, [net_cap]
    ja  .over
.fits:
    call net_rdrun              ; ...and take them
    jc  .bad
    mov ax, [net_len]
    mov dx, [net_len+2]
    clc
    jmp short .out
.over:
    call net_rdsink
    mov ax, FERR_BIG
    stc
    jmp short .out
.no:
    mov ax, FERR_NOENT
    stc
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_READAT - a window of a file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, CX = capacity, DI:SI = the offset
; out: CF=0 and DX:AX = the bytes delivered (0 = at or past the end)
;
; The length is a WORD here and a dword in FSV_READ, which is the frame
; following the contract rather than a second format: a windowed read is
; capped at 64KB by its own CX.
; -----------------------------------------------------------------------------
net_readat:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov [net_hnd], ax
    mov [net_cap], cx
    mov word [net_cap+2], 0
    mov [net_bseg], dx
    mov [net_boff], bx
    mov [net_off], si
    mov [net_off+2], di

    mov bl, NF_READAT
    call net_fcmd
    jc  .bad
    mov ax, [net_hnd]
    call lp_sword
    jc  .bad
    mov ax, [net_off]
    call lp_sword
    jc  .bad
    mov ax, [net_off+2]
    call lp_sword
    jc  .bad
    mov ax, [net_cap]
    call lp_sword
    jc  .bad

    call lp_rbyte
    jc  .bad
    or  al, al
    jnz .no
    call lp_rword               ; the length, one word
    jc  .bad
    mov [net_len], ax
    mov word [net_len+2], 0
    cmp ax, [net_cap]
    ja  .over
    call net_rdrun
    jc  .bad
    mov ax, [net_len]
    xor dx, dx
    clc
    jmp short .out
.over:
    call net_rdsink
    mov ax, FERR_BIG
    stc
    jmp short .out
.no:
    mov ax, FERR_NOENT
    stc
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; net_rdrun - take [net_len] bytes off the wire into [net_bseg]:[net_boff]
; out: CF=1 = the link went away
;
; TWO THINGS HAPPEN EVERY NET_PCHUNK BYTES and they are the same test because
; they want the same cadence. The destination is RE-NORMALISED - the paragraph
; part of the offset folded into the segment, dskw_norm's arithmetic inside a
; driver - so a read longer than 64KB cannot carry off the end of a segment;
; and OSAPI_FS_PROG is stepped, which is the only way SPEC.md 12.8's bar moves
; at all here (the kernel is one far call deep and blind until this returns).
;
; The progress report is BYTES SINCE THE LAST ONE and not a running total,
; which is the slot's contract: a total would advance the bar by the whole
; file every call.
; -----------------------------------------------------------------------------
net_rdrun:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [net_bseg]
    mov di, [net_boff]
    call net_rdnorm             ; ...once up front, because the caller's
                                ; offset can be anything at all
    mov cx, [net_len]
    mov dx, [net_len+2]
    xor bx, bx                  ; bytes since the last report
.byte:
    mov ax, cx
    or  ax, dx
    jz  .done
    call lp_rbyte
    jc  .bad
    mov [es:di], al
    inc di
    inc bx
    sub cx, 1
    sbb dx, 0
    cmp bx, NET_PCHUNK
    jb  .byte
    call net_rdmark             ; report and re-normalise together
    xor bx, bx
    jmp short .byte
.done:
    or  bx, bx                  ; the tail, which is almost never a whole
    jz  .out                    ; chunk
    call net_rdmark
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; net_rdmark - BX bytes have landed: tell the kernel and tidy ES:DI
net_rdmark:
    push ax
    mov ax, bx
    call OSAPI_FS_PROG
    pop ax
    call net_rdnorm
    ret

; net_rdnorm - fold DI's paragraph part into ES, leaving DI at 0..15
net_rdnorm:
    push ax
    push cx
    mov ax, di
    mov cl, 4
    shr ax, cl
    and di, 0x000F
    mov cx, es
    add cx, ax
    mov es, cx
    pop cx
    pop ax
    ret

; net_rdsink - swallow [net_len] bytes and store none of them
;
; The frame is a fixed size whatever this end does with it (SPEC.md 62.10.1),
; so a refusal still has to consume the run - the alternative is a wire with
; a file's worth of bytes on it and nobody listening, which is the link dead
; rather than one operation failed.
net_rdsink:
    push ax
    push cx
    push dx
    mov cx, [net_len]
    mov dx, [net_len+2]
.b:
    mov ax, cx
    or  ax, dx
    jz  .out
    call lp_rbyte
    jc  .out                    ; ...and if it died mid-sink, it died
    sub cx, 1
    sbb dx, 0
    jmp short .b
.out:
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; THE WRITE PATH (SPEC.md 62.10.4.5)
;
; Six verbs, one shape: the folder, the name, whatever the verb carries, and
; a single status byte back. They are short because everything hard about a
; write is on the OTHER side of the cable - there is no commit ordering here,
; no FAT to flush, no rollback (SPEC.md 18.4's three rules are the FAT path's
; and a redirected volume has none of it). What this end owes is the frame.
;
; A STATUS IS A FERR_*, and it is passed through untouched: the far side knows
; why a write failed - full disk, read-only medium, a name that is taken - and
; the kernel's callers already handle every one of them. Translating here
; would be a second opinion about somebody else's filesystem.
; =============================================================================

; -----------------------------------------------------------------------------
; FSV_WRITE - create or replace, whole (SPEC.md 62.9.1)
; in:  SI = a NUL 8.3 name in KERNEL_SEG, DX:BX = the bytes, DI:CX = how many
; out: CF=0; CF=1 and AX = FERR_*
; -----------------------------------------------------------------------------
net_write:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [net_cap], cx           ; the LENGTH, in the same pair a read uses for
    mov [net_cap+2], di         ; its capacity - one walker, one meaning per
    mov [net_bseg], dx          ; direction
    mov [net_boff], bx
    mov bl, NF_WRITE
    mov ax, [net_cwd]
    call net_fcmd_h
    jc  .bad
    call net_sname
    jc  .bad
    mov ax, [net_cap]
    call lp_sword
    jc  .bad
    mov ax, [net_cap+2]
    call lp_sword
    jc  .bad
    call net_wrrun              ; ...and the bytes
    jc  .bad
    call net_wstat
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
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
; FSV_APPEND - add to the end of an existing file (SPEC.md 18.4.4)
; in:  SI = the name, DX:BX = the bytes, CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; THE LENGTH IS ONE WORD HERE and a dword in FSV_WRITE, which is the frame
; following the cell rather than a second format: this is the CHUNKED half of
; the pair, so a copy streams through it a claim at a time and CX is all there
; has ever been.
; -----------------------------------------------------------------------------
net_append:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [net_cap], cx
    mov word [net_cap+2], 0
    mov [net_bseg], dx
    mov [net_boff], bx
    mov bl, NF_APPEND
    mov ax, [net_cwd]
    call net_fcmd_h
    jc  .bad
    call net_sname
    jc  .bad
    mov ax, [net_cap]
    call lp_sword
    jc  .bad
    call net_wrrun
    jc  .bad
    call net_wstat
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
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
; FSV_DELETE / FSV_MKDIR / FSV_RMDIR / FSV_RMTREE - one name, one status
; in:  SI = a NUL 8.3 name in KERNEL_SEG
;
; FSV_RMTREE is the same FRAME as FSV_RMDIR and a different COMMAND, which is
; 62.10.1's rule rather than a preference: `no command means two things`. It
; also happens to be the safe way round - a far side built before this verb
; existed ignores an unknown letter, where a mode flag on RMDIR would have
; made an old far side empty one folder and answer OK.
;
; NONE OF THESE CAN ANSWER `CF=1 WITH AX=0`, and dskw_rtbody depends on it:
; that word is what drv_fs_call gives for a verb a driver does not publish, so
; it is the fallback's trigger. net_wstat passes through a NON-ZERO FERR_* and
; every transport failure here is FERR_IO.
; -----------------------------------------------------------------------------
net_delete:
    mov bl, NF_DELETE
    jmp short net_name1
net_mkdir:
    mov bl, NF_MKDIR
    jmp short net_name1
net_rmtree:
    mov bl, NF_RMTREE
    jmp short net_name1
net_rmdir:
    mov bl, NF_RMDIR
net_name1:
    push cx
    push dx
    push si
    push di
    mov ax, [net_cwd]
    call net_fcmd_h
    jc  .bad
    call net_sname
    jc  .bad
    call net_wstat
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; FSV_COPY - AX = source folder, DX = destination folder, SI = the name
;            (SPEC.md 62.9.8)
;
; BOTH ENDS ARE THE FAR SIDE'S, so not one byte of the file crosses the cable:
; the frame is two handles and a name out, one status back. A remote-to-remote
; copy of a large file used to stream every byte over at 3,741 B/s and push it
; straight back.
;
; The two folders go out in the order the kernel hands them over, and the name
; LAST - so the far side reads command, src, dst, name, which is wr_arg's
; shape with one extra word in front of it.
; -----------------------------------------------------------------------------
net_copy:
    push bx
    push cx
    push dx
    push si
    push di
    mov [net_arg], dx           ; the destination folder, across net_fcmd_h
    mov bl, NF_COPY
    call net_fcmd_h             ; ...which sends AX, the SOURCE folder
    jc  .bad
    mov ax, [net_arg]
    call lp_sword
    jc  .bad
    call net_sname
    jc  .bad
    call net_wstat
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_RENAME - SI = the old name, DI = the new one, both in KERNEL_SEG
;
; Two 13-byte fields and no length between them, which is what the fixed frame
; buys: the far side knows where the first ends because it is always 13.
; -----------------------------------------------------------------------------
net_rename:
    push cx
    push dx
    push si
    push di
    mov [net_arg2], di          ; ...banked, because net_sname walks SI and the
                                ; second name has to survive the first
    mov bl, NF_RENAME
    mov ax, [net_cwd]
    call net_fcmd_h
    jc  .bad
    call net_sname              ; the old name
    jc  .bad
    mov si, [net_arg2]
    call net_sname              ; ...and the new
    jc  .bad
    call net_wstat
    jmp short .out
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; net_wstat - the one status byte every write verb ends with
; out: CF=0 and AX = 0; CF=1 and AX = the far side's FERR_*
;
; A NON-ZERO STATUS IS NOT A DEAD LINK. That distinction is the whole of this
; routine: `the disk is full` and `the cable came out` both arrive as a
; failure at the call site, and only one of them means the volume should be
; dropped. net_lost is for the second; this is the first.
; -----------------------------------------------------------------------------
net_wstat:
    call lp_rbyte
    jc  .bad
    or  al, al
    jnz .no
    xor ax, ax
    clc
    ret
.no:
    xor ah, ah                  ; the far side's FERR_*, passed through
    stc
    ret
.bad:
    call net_lost
    mov ax, FERR_IO
    stc
    ret

; -----------------------------------------------------------------------------
; net_wrrun - put [net_cap] bytes from [net_bseg]:[net_boff] on the wire
; out: CF=1 = the link went away
;
; net_rdrun backwards, and deliberately the same shape: the destination is
; re-normalised and OSAPI_FS_PROG stepped on one test every NET_PCHUNK bytes,
; because a save over a cable is exactly as long as a load and SPEC.md 12.8's
; bar has the same nothing to report without it.
; -----------------------------------------------------------------------------
; IT WALKS THE SOURCE THROUGH ES AND NOT DS, and that is not a style choice.
; lplink.inc addresses [lp_base], [lp_lastop] and [lp_dlset] through DS with
; no segment override anywhere in the file - so a routine that repoints DS at
; the caller's buffer hands the transport a garbage port number and a garbage
; turnaround flag. The first draft did exactly that, with a comment asserting
; the opposite; the assertion cost nothing to check and would have cost a
; session to debug, because the wire would simply have stopped.
net_wrrun:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [net_bseg]
    mov di, [net_boff]
    call net_wrnorm
    mov cx, [net_cap]
    mov dx, [net_cap+2]
    xor bx, bx                  ; bytes since the last report
.byte:
    mov ax, cx
    or  ax, dx
    jz  .done
    mov al, [es:di]
    inc di
    call lp_sbyte
    jc  .bad
    inc bx
    sub cx, 1
    sbb dx, 0
    cmp bx, NET_PCHUNK
    jb  .byte
    call net_wrmark
    xor bx, bx
    jmp short .byte
.done:
    or  bx, bx
    jz  .out
    call net_wrmark
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; net_wrmark - BX bytes have gone: report, then tidy ES:DI
net_wrmark:
    push ax
    mov ax, bx
    call OSAPI_FS_PROG
    pop ax
    call net_wrnorm
    ret

; net_wrnorm - fold DI's paragraph part into ES, leaving DI at 0..15.
; net_rdnorm's twin, and the two are separate rather than shared because the
; read walks a DESTINATION it writes and this walks a SOURCE it reads; one
; routine taking a direction flag would be a branch per byte.
net_wrnorm:
    push ax
    push cx
    mov ax, di
    mov cl, 4
    shr ax, cl
    and di, 0x000F
    mov cx, es
    add cx, ax
    mov es, cx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_fcmd / net_fcmd_h - open a FILE-mode command, with and without an
;                         argument word
; in:  BL = the NF_* letter;  net_fcmd_h also takes AX = the handle
; out: CF=1 = there is no link, or it went away; AX = FERR_IO for the first
;
; **THE NS_LINKED GATE IS HERE AND NOT IN FOURTEEN VERB BODIES.** net_lost
; cannot take the volume down from where it runs (see its header), so the
; volume stays on the desktop after a cable comes out and every click on it is
; another file verb - each one going to a wire we already know is dead, and
; each paying a full REPLY_TMO before saying so. Every verb already treats a
; CF=1 from here as its dead-link path, so gating the two prologues costs no
; verb a line and a verb added later inherits it - wr_gate's argument, on this
; side of the cable.
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
    cmp byte [net_state], NS_LINKED
    jne .nolink
    push ax
    mov [net_arg], ax
    mov [net_cmdip], bl         ; ...so a dead link can say what it died on
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
.nolink:
    mov ax, FERR_IO             ; ...and NOT net_cmdip: the page reports the
    stc                         ; command that DIED, not the ones refused
    ret                         ; after it

net_fcmd:
    cmp byte [net_state], NS_LINKED
    jne net_fcmd_h.nolink
    push ax
    mov [net_cmdip], bl
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
    mov byte [lp_turnw], TURN_RX    ; net_detach's reason exactly: a goodbye
                                ; is best effort, and a patient one costs the
                                ; user ten frozen seconds when the far end is
                                ; simply not there (SPEC.md 62.10.4.8)
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
    mov byte [lp_turnw], TURN_RX    ; net_detach's reason on the failure path:
                                ; anything that still reaches the wire after
                                ; this fails in 440 ms rather than in ten
                                ; frozen seconds. net_connect raises it back
                                ; to REPLY_TMO on the next link
    push ax
    mov al, [net_cmdip]         ; WHICH COMMAND was in flight, for the page.
    mov [net_lastcmd], al       ; An LBA meant something in block mode and
    pop ax                      ; means nothing here (netui's note)
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
net_cmdip:  db 0                ; the NF_* letter in flight
net_lastcmd: db 0               ; ...and the one the last failure died on
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
net_earg:   dw 0                ; FSV_ENUM's ordinal, its caller's buffer and
net_ebuf:   dw 0                ; the status it was answered with - all three
net_eseg:   dw 0                ; live across a reply that spends BX, CX and DX
net_est:    db 0                ; and they are their own words rather than
                                ; net_arg2 reused, because the ordinal has to
                                ; survive net_fcmd_h AND the buffer has to
                                ; survive the whole 32-byte read
net_up:     dw 0                ; the parent handle the far side answered with
net_full:   db 0                ; the kernel's listing filled: keep READING the
                                ; run, stop APPENDING to it
net_ent:    times DSK_DE_SIZE db 0  ; one staged 19.1 entry, off the wire

; --- and the read path's (SPEC.md 62.10.4.3) -----------------------------------
; All of it is MEMORY rather than registers because a read has more live state
; than an 8086 has registers: a handle, a 32-bit capacity, a 32-bit length, a
; 32-bit offset and a destination that walks - and every one of them has to
; survive a call into lp_rbyte.
net_hnd:    dw 0                ; the handle the operation in hand is about
net_sz:     dd 0                ; FSV_STAT's answer
net_att:    db 0                ; ...and its attribute byte
net_cap:    dd 0                ; what we told the far side we could take
net_len:    dd 0                ; ...and what it says it is sending
net_off:    dd 0                ; FSV_READAT's window
net_bseg:   dw 0                ; the destination, which walks a segment at a
net_boff:   dw 0                ; time - see net_rdnorm

    OS88_DRV_END
