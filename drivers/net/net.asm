; =============================================================================
; os8088 - NET.DRV
;
; A LapLink parallel cable to a DOS machine, as a BLOCK VOLUME: the far side
; serves 512-byte sectors out of an image file and os8088 mounts them as an
; ordinary FAT12/16 drive. Stage 1 of docs/plans/completed/NET-PLAN.md.
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
%define NETPKG_DRIVER           ; the constants, not the client half
%include "netpkg.inc"           ; the SOCKET ABI (SPEC.md 62.11) - the same
                                ; file a package includes, so the two ends
                                ; cannot drift

    OS88_DRIVER 'os88net', DRVC_FILE, net_entry

%define WZ_CLASS DRVC_FILE      ; the Wire's desktop icon (SPEC.md 26.7), and
%include "wirezone.inc"         ; every byte of it is the DRIVER's: the
                                ; kernel's zone is generic and carries no
                                ; glyph, so a machine with neither driver pays
                                ; for none of it. Shared with ETHER.DRV as
                                ; SOURCE and never as a copy - whichever
                                ; attaches first has the zone, and both reach
                                ; the same Wire.
                                ;
                                ; UP HERE, above every call site, so wz_paint
                                ; and wz_register are BACKWARD references:
                                ; with the include at the foot of the file
                                ; nasm sized `je nsk_wzpaint` on pass 1 and
                                ; resized it on pass 2, which is
                                ; `label changed during code generation`
                                ; on three labels and no build


; --- the wire protocol, master side ------------------------------------------
; os8088 is always the master and never receives unsolicited data (NET-PLAN
; 1.3), so every exchange below is request-then-response and the multiplexer
; NET-PLAN 5.3 will need for sockets is a busy flag and nothing more.
;
;   'I'  -> reply: status, sectors word, flags byte (bit 0 = read-only)
;   'X'  -> the far side may go back to listening
;
; 'R' and 'W' were BLOCK mode's sector commands (SPEC.md 62's first stage).
; The far side still serves them, so the letters stay taken, but this end
; publishes no DSV_BLK since it became the redirector (62.10) and the master
; half of them is gone from the image: it could not be reached. The rule this
; whole wire keeps is the one they taught it - EVERY FRAME IS A FIXED SIZE
; WHATEVER THE STATUS SAYS, so a refusal still carries its bytes and neither
; end is left talking into one that has stopped listening.
NC_INFO     equ 'I'
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
    dw 0                        ; DSV_CPKEY   - this page takes no keys
    dw net_cp_up                ; DSV_CPUP    - Connect fires on the RELEASE
    dw net_cp_drag              ; DSV_CPDRAG  - ...and follows the pointer
                                ;               between the two edges
                                ;               (SPEC.md 13.8.4)
    dw net_pkg                  ; DSV_PKGCALL - AND SOCKETS (SPEC.md 62.11,
                                ; drivers/net/netpkg.inc). One of the three
                                ; drivers in the tree that open the package
                                ; door, and the reason the door exists: a
                                ; browser needs a connection and os8088 has no
                                ; networking code at all (SPEC.md 20.11)
    times DSV_SIZE - ($ - net_svc) db 0
                                ; drv_publish copies DSV_SIZE bytes
                                ; whatever this table's length is, so
                                ; one that stops short publishes
                                ; whatever FOLLOWS it - see
                                ; ramdisk.asm, which did. This one is
                                ; exact today and the pad is what
                                ; keeps it exact when DSV_SIZE grows

; The FSV_* verbs (SPEC.md 62.9.1). A cell of 0 is "not implemented", which
; drv_fs_call answers as an ordinary refusal rather than a fault - so a phase
; lands its own verbs and the ones after it decline cleanly meanwhile.
;
; **EVERY CELL POINTS AT A GATE AND NOT AT THE VERB** (SPEC.md 62.11): the
; wire is shared with the socket verbs now, which a package's WORKER may be
; inside, so a file verb has to own it for its whole run. net_fgate is that
; ownership, and each nfs_<verb> is a `call net_fgate` sitting directly in
; front of its body - three bytes a cell. Adding a verb here without one is
; not a thing that can be forgotten quietly: the cell has nothing to name
; until one exists.
net_fsv:
    dw nfs_list                 ; FSV_LIST
    dw nfs_chdir                ; FSV_CHDIR
    dw nfs_stat                 ; FSV_STAT
    dw nfs_read                 ; FSV_READ
    dw nfs_write                ; FSV_WRITE
    dw nfs_append               ; FSV_APPEND
    dw nfs_readat               ; FSV_READAT
    dw nfs_delete               ; FSV_DELETE
    dw nfs_rename               ; FSV_RENAME
    dw nfs_mkdir                ; FSV_MKDIR
    dw nfs_rmdir                ; FSV_RMDIR
    dw nfs_dfree                ; FSV_DFREE
    dw nfs_enum                 ; FSV_ENUM (SPEC.md 62.9.7/62.10.6) - the verb
                                ; a FOLDER copy walks its source with. This
                                ; carried 0 for two milestones, and the effect
                                ; was not a broken copy but a REFUSED one:
                                ; fcp_mkroot probes the cell with drv_fs_has
                                ; and answers FERR_PROT, so dragging a folder
                                ; onto or off the Link volume reported
                                ; `Protected` and moved nothing
    dw nfs_copy                 ; FSV_COPY (SPEC.md 62.9.8)
    dw nfs_rmtree               ; FSV_RMTREE (SPEC.md 62.10.7) - the far side
                                ; recurses with its own int 21h, so a tree
                                ; costs one command frame instead of an
                                ; FSV_ENUM and an FSV_DELETE per file
net_fsv_end:
%if net_fsv_end - net_fsv != FSV_SIZE
  %error "net: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

net_name:                       ; DSV_NAME and DSV_CPNAME are the same
net_cpname: db 'os88net', 0     ; word, so they are the same bytes

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
    call wz_register            ; the Wire's desktop icon (SPEC.md 26.7), and
                                ; ABOVE the cable test: a driver whose far end
                                ; is not switched on yet is an ordinary state
                                ; and the Wire's own window is what says so.
                                ; A refusal here means ETHER.DRV already has
                                ; the zone, which is fine - there is one, and
                                ; both drivers reach the same Wire
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
    call wz_withdraw            ; **THE DESKTOP ICON FIRST** (SPEC.md 26.7):
                                ; its paint verb lives in the image the kernel
                                ; is about to free. A withdraw of a zone we do
                                ; not hold is refused and costs nothing
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
    call nsk_drop               ; every socket goes with the session. NC_BYE
                                ; above is what tells the far side so; this is
                                ; what stops a package's stale handle naming
                                ; one the NEXT link hands out
    mov byte [net_busy], 0      ; ...and the wire is nobody's. A detach that
                                ; left it claimed would refuse every file verb
                                ; of the next attach with FERR_IO
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
    call nsk_drop               ; ...and a fresh link has NO SOCKETS. The far
                                ; side dropped its own when the session ended,
                                ; so a handle from the last link would name
                                ; whatever this one hands out next
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
; out: CF=0 and [net_flags] set; CF=1 = the link went away. The sector count
; is block mode's and a redirected volume has none (62.9), so it is read off
; the wire - the frame is fixed - and not kept
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
    call lp_rword               ; sectors, consumed and not kept
    jc  .bad
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

; =============================================================================
; THE FILE VERBS (SPEC.md 62.9.1 / 62.10.1)
;
; **EVERY VERB IS ENTERED THROUGH ITS GATE AND LEFT THROUGH A TAIL**, and that
; is what keeps each of them down to its wire frame. `nfs_<verb>` - the label
; the FSV_* cell names - is a `call net_fgate` sitting directly in front of the
; body, so a verb cannot be reached ungated: the cell has nothing else to
; point at. The gate (netsock.inc) takes the wire, refuses a dead link, saves
; BX, CX, DX, SI and DI in an ABORT FRAME and jumps into the body; the body
; leaves through one of the tails below, which drop the frame, give the
; registers back and release the wire. So a verb owes no prologue and no
; epilogue, and spends any register it likes - only its OUTPUTS are its own,
; and the ones the caller gets in BX, CX or DX are written into the frame
; (net_fokd; FSV_STAT and FSV_DFREE write BX and CX themselves).
;
; **AND A DEAD WIRE IS NOT A BRANCH IN ANY VERB.** nrb / nrw / nsb / nsw are
; lp_rbyte / lp_rword / lp_sbyte / lp_sword that do not come back when the
; transport fails: they jump to net_abort, which puts SP back on the frame and
; runs the gate's failure tail - net_lost, AX = FERR_IO, CF = 1, every
; register as the caller had it. That is the fourteen `.bad` exits this file
; used to carry, written once. The BYTE LOOPS - a listing entry, a file's
; body, a name - call lp_* themselves and `jc net_abort`, because a helper
; frame per byte is ~10 us of a ~267 us byte (PERFORMANCE.md Set 39), and that
; is the transfer speed rather than a rounding error.
;
; A REFUSAL IS NOT A DEAD LINK, and the tails keep the two apart exactly as
; the verbs did: the far side saying `no such file` is net_fnoent / net_wst,
; which pass the answer through and leave the link up; only the transport
; failing reaches net_lost.
; =============================================================================

; -----------------------------------------------------------------------------
; FSV_LIST - the folder we are STANDING IN, one entry at a time
; in:  nothing - the driver holds its own cwd, put there by FSV_CHDIR
; out: CF=0; CF=1 = the far side refused, or the link went away
;
; The wire hands back a COUNT and then that many 32-byte SPEC.md 19.1 entries,
; and each is passed to OSAPI_FS_ENT unreshaped. The kernel still SORTS (19.4)
; and still synthesizes '..' (19.5), so this must do neither. It takes NO
; argument: [net_cwd] is the one opinion about where we stand, and the wire
; carries it because the FAR side has a cwd of its own.
;
; A refused entry ends the APPEND but NOT the read, and A REFUSED LISTING IS
; STILL A FULL FRAME - the count follows the status whatever it said
; (62.10.1), so it is read before the status is acted on.
; -----------------------------------------------------------------------------
nfs_list:
    call net_fgate
    mov byte [net_full], 0      ; a fresh listing is not the last one's cap
    mov bl, NF_LIST
    mov ax, [net_cwd]
    call net_fcmd_h             ; the command and the folder it is about
    call nrb                    ; status
    push ax
    call nrw                    ; ...and how many entries follow, ALWAYS
    xchg ax, di
    pop ax
    or  al, al
    jnz .no                     ; refused: the count was the rest of the frame
.each:
    or  di, di                  ; (CF = 0)
    jz  .out
    dec di
    call net_rdent              ; ...each of them a staged 19.1 entry
    cmp byte [net_full], 0      ; the listing filled up: keep READING - the
    jne .each                   ; run is fixed and the far side is mid-send
    mov si, net_ent
    call OSAPI_FS_ENT           ; ES is our own DS, set by the X stub
    jnc .each
    mov byte [net_full], 1
    jmp short .each
.no:
    stc                         ; ...AND NO NC_BYE. See net_fcmd's header:
.out:                           ; NC_BYE ends the SESSION, not the command
    jmp net_fok

; -----------------------------------------------------------------------------
; FSV_ENUM - the Nth entry of a folder that is NOT the one on screen
;            (SPEC.md 62.9.7/62.10.6)
; in:  AX = the folder's handle, CX = the ordinal, DX:BX = a 32-byte buffer
;      (DX:BX and not ES:BX, because ES is KERNEL_SEG on entry - SPEC.md 51.8)
; out: CF=0 and the entry is in the caller's buffer;
;      CF=1 with AX = 0 = PAST THE LAST ENTRY, a normal end and not an error;
;      CF=1 with AX = FERR_* = this folder could not be walked
;
; What a FOLDER copy walks its source with, and NOT FSV_LIST: that appends
; into the kernel's one global listing about the folder we STAND in, so here
; the folder is an argument and [net_cwd] is untouched.
;
; THE THREE STATUSES ARE THE WHOLE CONTRACT. `CF=1, AX=0` is letter for letter
; the END of a folder, so a far side that cannot walk one must NOT say that -
; fcp_scan would read it as an empty subdirectory and the paste would look
; like a success over a subtree it never copied. FERR_NOENT is the end;
; anything else is FERR_IO. The 32 bytes are on the wire whatever the status
; said, so they are READ first, and STAGED - a torn read must not leave half
; an entry in the caller's buffer.
; -----------------------------------------------------------------------------
nfs_enum:
    call net_fgate
    mov es, dx                  ; the caller's buffer, ES:DI - both are ours
    mov di, bx                  ; to spend, and the gate gives them back
    mov bl, NF_ENUM
    call net_fcmd_h             ; the command and the FOLDER it is about
    mov ax, cx
    call nsw                    ; ...and how far into it
    call nrb                    ; status
    push ax
    call net_rdent
    pop ax
    cmp al, NST_OK
    jne .no
    mov si, net_ent             ; ...and out to DX:BX
    mov cx, DSK_DE_SIZE
    cld
    rep movsb
    clc
    jmp net_fok
.no:
    cmp al, FERR_NOENT
    mov ax, FERR_IO             ; a folder we could not walk is NOT the end,
    jne .e                      ; however alike they read here
    xor ax, ax                  ; the end of the folder: CF=1, AX=0
.e:
    jmp net_ferr

; -----------------------------------------------------------------------------
; FSV_CHDIR - stand in a folder, and say what is above it (SPEC.md 62.9.1)
; in:  AX = a handle out of an entry, 0 = the root
; out: CF=0, AX = that handle and DX = THE PARENT'S; CF=1 and AX = FERR_*
;
; The parent comes back from the far side because the handle is ITS to assign
; and opaque here (62.9.1). A refused chdir must leave us standing where we
; were, or FSV_LIST lists a folder we never reached - so [net_cwd] moves only
; once the whole answer is in.
; -----------------------------------------------------------------------------
nfs_chdir:
    call net_fgate
    push ax                     ; ...banked, because the reply overwrites AX
    mov bl, NF_CHDIR
    call net_fcmd_h
    call net_fst                ; status: a refusal is FERR_NOENT, link up
    call nrw                    ; the parent's handle
    xchg ax, dx
    pop ax
    mov [net_cwd], ax           ; ONLY NOW is the move real
    clc
    jmp net_fokd

; -----------------------------------------------------------------------------
; FSV_DFREE - free space, in BYTES (SPEC.md 62.9.1)
; out: CF=0 with DX:AX = free bytes and BX = the granule; CF=1 and AX = FERR_*
; -----------------------------------------------------------------------------
nfs_dfree:
    call net_fgate
    mov bl, NF_DFREE
    call net_fcmd               ; no argument on this one
    call net_fst
    call nrw
    push ax                     ; the low half
    call nrw
    xchg ax, dx                 ; ...the high
    call nrw                    ; ...and the granule, which is the caller's BX
    mov bp, [net_sp]
    mov [bp+NFR_BX], ax
    pop ax
    clc
    jmp net_fokd

; -----------------------------------------------------------------------------
; FSV_STAT - a name in the current folder -> a handle (SPEC.md 62.9.1)
; in:  SI -> a NUL 8.3 name, IN KERNEL_SEG - so ES, which drv_fs_call sets
; out: CF=0, AX = the handle, DX:CX = the size, BL = a FAT attribute byte
;      (0x10 = directory), BH = 0; CF=1 and AX = FERR_*
;
; AX, BX, CX AND DX ARE ALL OUTPUTS, so the three the frame would otherwise
; give back are written INTO it. The first version of this verb pushed
; BX/CX/DX at the top and popped them at the bottom - throwing the size and
; attribute away a few instructions after reading them, and answering a stale
; triple with CF=0: the link works, the file is found, and the kernel is told
; it is some other size.
; -----------------------------------------------------------------------------
nfs_stat:
    call net_fgate
    mov bl, NF_STAT
    call net_fhdr               ; the folder - [net_cwd], and not the far
                                ; side's own memory of the last chdir - and
                                ; the name
    call net_fst
    call nrw                    ; the handle
    push ax
    call nrw                    ; size, low
    xchg ax, cx
    call nrw                    ; size, high
    xchg ax, dx
    call nrb                    ; the attribute byte
    xor ah, ah
    mov bp, [net_sp]
    mov [bp+NFR_BX], ax
    mov [bp+NFR_CX], cx
    pop ax
    clc
    jmp net_fokd

; -----------------------------------------------------------------------------
; FSV_READ - the whole file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, DI:CX = its 32-bit capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = FERR_*
;
; THE CAPACITY GOES OUT WITH THE COMMAND, which is what keeps a refusal cheap:
; the far side answers min(size, cap). It is still checked on arrival - a far
; end that ignores the cap is writing past the end of somebody else's heap
; claim, and "the other machine is well behaved" is not a thing this side can
; know. Over the cap the bytes are CONSUMED and refused (net_fbig): the frame
; is fixed, so they are coming whatever we do with them.
; -----------------------------------------------------------------------------
nfs_read:
    call net_fgate
    mov [net_bseg], dx
    mov [net_boff], bx
    mov bl, NF_READ
    call net_fcmd_h             ; ...and the handle
    mov ax, cx
    call nsw                    ; ...and the cap, low word first
    mov ax, di
    call nsw
    call net_fst
    call nrw                    ; the length, low
    mov [net_len], ax
    call nrw                    ; ...and high
    mov [net_len+2], ax
    cmp ax, di                  ; longer than we asked for?
    ja  .over
    jb  .fits
    cmp [net_len], cx
    ja  .over
.fits:
    jmp short net_rdlen         ; ...and take them
.over:
    jmp net_fbig

; -----------------------------------------------------------------------------
; FSV_READAT - a window of a file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, CX = capacity, DI:SI = the offset
; out: CF=0 and DX:AX = the bytes delivered (0 = at or past the end)
;
; The length is a WORD here and a dword in FSV_READ, which is the frame
; following the contract: a windowed read is capped at 64KB by its own CX.
; -----------------------------------------------------------------------------
nfs_readat:
    call net_fgate
    mov [net_bseg], dx
    mov [net_boff], bx
    mov bl, NF_READAT
    call net_fcmd_h             ; ...and the handle
    mov ax, si
    call nsw                    ; the offset
    mov ax, di
    call nsw
    mov ax, cx
    call nsw                    ; the cap
    call net_fst
    call nrw                    ; the length, one word
    mov [net_len], ax
    mov word [net_len+2], 0
    cmp ax, cx
    ja  nfs_read.over
                                ; ...and in: one walker, both reads
; net_rdlen - take [net_len] bytes into the caller's buffer and answer with
; their count, DX:AX
net_rdlen:
    call net_rdrun
    mov ax, [net_len]
    mov dx, [net_len+2]
    clc
    jmp net_fokd

; =============================================================================
; THE WRITE PATH (SPEC.md 62.10.4.5)
;
; One shape: the folder, the name, whatever the verb carries, and a single
; status byte back (net_wst). Everything hard about a write is on the OTHER
; side of the cable; what this end owes is the frame. A STATUS IS A FERR_*,
; and it is passed through untouched - the far side knows why a write failed,
; and translating here would be a second opinion about somebody else's
; filesystem.
;
; NONE OF THESE CAN ANSWER `CF=1 WITH AX=0`, and dskw_rtbody depends on it:
; that word is what drv_fs_call gives for a verb a driver does not publish, so
; it is the fallback's trigger. net_wst passes through a NON-ZERO FERR_* and
; every transport failure is FERR_IO.
; =============================================================================

; -----------------------------------------------------------------------------
; FSV_WRITE  - create or replace, whole: SI = a NUL 8.3 name in KERNEL_SEG,
;              DX:BX = the bytes, DI:CX = how many
; FSV_APPEND - add to the end (SPEC.md 18.4.4): SI, DX:BX, CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; ONE BODY: the length is a dword for WRITE and a WORD for APPEND, which is
; the frame following the cell rather than a second format - APPEND is the
; CHUNKED half of the pair, so a copy streams through it a claim at a time and
; CX is all there has ever been.
; -----------------------------------------------------------------------------
nfs_append:
    call net_fgate
    xor di, di                  ; a word: the high half is not on the wire
    mov al, NF_APPEND
    jmp short net_wcom
nfs_write:
    call net_fgate
    mov al, NF_WRITE
net_wcom:
    mov [net_cap], cx           ; the LENGTH, in the pair net_wrrun walks
    mov [net_cap+2], di
    mov [net_bseg], dx
    mov [net_boff], bx
    mov bl, al
    call net_fhdr               ; the folder and the name
    mov ax, [net_cap]
    call nsw
    cmp bl, NF_APPEND
    je  .body
    mov ax, [net_cap+2]
    call nsw
.body:
    call net_wrrun              ; ...and the bytes
    jmp short net_wst

; -----------------------------------------------------------------------------
; FSV_DELETE / FSV_MKDIR / FSV_RMDIR / FSV_RMTREE - one name, one status
; in:  SI = a NUL 8.3 name in KERNEL_SEG
;
; FSV_RMTREE is RMDIR's FRAME and a different COMMAND, which is 62.10.1's rule:
; `no command means two things`. It is also the safe way round - a far side
; built before the verb ignores an unknown letter, where a mode flag on RMDIR
; would have made an old far side empty one folder and answer OK.
; -----------------------------------------------------------------------------
nfs_delete:
    call net_fgate
    mov bl, NF_DELETE
    jmp short net_name1
nfs_mkdir:
    call net_fgate
    mov bl, NF_MKDIR
    jmp short net_name1
nfs_rmtree:
    call net_fgate
    mov bl, NF_RMTREE
    jmp short net_name1
nfs_rmdir:
    call net_fgate
    mov bl, NF_RMDIR
net_name1:
    call net_fhdr
    jmp short net_wst

; -----------------------------------------------------------------------------
; FSV_RENAME - SI = the old name, DI = the new one, both in KERNEL_SEG
;
; Two 13-byte fields and no length between them, which is what the fixed frame
; buys: the far side knows where the first ends because it is always 13.
; -----------------------------------------------------------------------------
nfs_rename:
    call net_fgate
    mov bl, NF_RENAME
    call net_fhdr               ; the old name
    mov si, di
    call net_sname              ; ...and the new
    jmp short net_wst

; -----------------------------------------------------------------------------
; FSV_COPY - AX = source folder, DX = destination folder, SI = the name
;            (SPEC.md 62.9.8)
;
; BOTH ENDS ARE THE FAR SIDE'S, so not one byte of the file crosses the cable:
; command, src, dst, name out, one status back.
; -----------------------------------------------------------------------------
nfs_copy:
    call net_fgate
    mov bl, NF_COPY
    call net_fcmd_h             ; ...which sends AX, the SOURCE folder
    mov ax, dx
    call nsw                    ; the destination
    call net_sname              ; ...and the name LAST
                                ; (falls into net_wst)

; -----------------------------------------------------------------------------
; net_wst - the one status byte every write verb ends with, and their TAIL
; out: CF=0 and AX = 0; CF=1 and AX = the far side's FERR_*
;
; A NON-ZERO STATUS IS NOT A DEAD LINK: `the disk is full` and `the cable came
; out` both arrive as a failure at the call site, and only one of them means
; the volume should be dropped. nrb's abort is for the second; this is the
; first.
; -----------------------------------------------------------------------------
net_wst:
    call nrb
    xor ah, ah                  ; the far side's FERR_*, passed through
    cmp ah, al                  ; CF = 1 exactly when it is not 0
    jmp short net_fok

; -----------------------------------------------------------------------------
; THE TAILS - every verb leaves through one of these
; -----------------------------------------------------------------------------
; net_fst - a status byte, and if it is a refusal LEAVE THE VERB with
; FERR_NOENT: the far side's own answer, status-only on the wire, so nothing
; is left unread and nothing is lost. A folder that is not there is not the
; cable coming out.
net_fst:
    ; STKBALANCE-OK: a refusal LEAVES THE VERB from inside this call - net_fok puts SP back on the gate's frame
    call nrb
    or  al, al
    jnz net_fnoent
    ret

; net_fbig - a reply longer than the caller can take: consumed, then refused
net_fbig:
    call net_rdsink
    mov ax, FERR_BIG
    jmp short net_ferr
net_fnoent:
    mov ax, FERR_NOENT
net_ferr:                       ; AX = the refusal
    stc
    jmp short net_fok
; net_ffail - net_abort's tail on the file side: the link went away
net_ffail:
    ; STKBALANCE-OK: entered by net_abort with SP already put back on the gate's frame, never by a call
    call net_lost
    mov ax, FERR_IO
    jmp short net_ferr
; net_fokd - DX joins AX and CF as the answer
net_fokd:
    mov bp, [net_sp]
    mov [bp+NFR_DX], dx
; net_fok - AX and CF are the answer: drop the frame, every other register
; back as the caller had it, and the wire is nobody's. No instruction from
; here on writes a flag.
net_fok:
    mov sp, [net_sp]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    mov byte [net_busy], 0
    ret

; -----------------------------------------------------------------------------
; THE WIRE, for a verb - see this section's header
; -----------------------------------------------------------------------------
; net_fhdr - BL = the letter: it, [net_cwd], and SI's name
net_fhdr:
    mov ax, [net_cwd]
    call net_fcmd_h
    jmp short net_sname

; net_fcmd_h / net_fcmd - open a FILE-mode command, with and without an
; argument word. BL = the NF_* letter; net_fcmd_h sends AX after it. AX is
; preserved.
;
; THE NS_LINKED TEST IS NOT HERE ANY MORE: it is the gate's, one test for
; every verb, which is where it always belonged - a verb that reached the wire
; on a link already known to be dead paid a full REPLY_TMO to find out.
;
; **AND NO COMMAND ENDS WITH NC_BYE.** It reads like a frame terminator and it
; is not one: `serve` on the far side LEAVES its command loop on NC_BYE and
; goes back to hunting for the magic (lplslv.inc), so a bye after every verb
; tore the session down. The gap between commands is the user's THINKING
; TIME, which is exactly what lp_rbyte_w's unbounded wait was built for.
; NC_BYE is net_drop's and net_connect's alone. It survived a whole scripted
; session against tests/lptlink/partner.py, whose server read a bye as "carry
; on" - so partner.py returns on one now, as the real far end does.
net_fcmd_h:
    call net_fcmd
    jmp short nsw
net_fcmd:
    mov [net_cmdip], bl         ; ...so a dead link can say what it died on
    push ax
    mov al, bl
    call nsb
    pop ax
    ret

; nrb / nrw / nsb / nsw - lp_rbyte / lp_rword / lp_sbyte / lp_sword that do
; not return when the wire has gone: they abort the verb (net_abort). Only
; inside a gate's frame.
nrb:
    call lp_rbyte
    jc  net_abort
    ret
nrw:
    call lp_rword
    jc  net_abort
    ret
nsb:
    call lp_sbyte
    jc  net_abort
    ret
nsw:
    call lp_sword
    jc  net_abort
    ret

; net_abort - the transport failed inside a gate: back to the frame, and the
; gate's own failure tail (net_ffail or nsk_fail). Whatever the verb had
; pushed, called or half-sent goes with the stack it was on.
net_abort:
    ; STKBALANCE-OK: whatever depth a dead wire is found at is discarded - SP goes back on the gate's frame
    mov sp, [net_sp]
    jmp [net_abfn]

; net_sname - ES:SI's NUL name -> 13 bytes on the wire, NUL-padded
; clobbers AX, CX, SI
;
; THE NAME CROSSES AS A FIXED 13 BYTES because the frame has to be a fixed
; size: the far side must know where the argument ends without a length in
; front of it, and 8.3 plus a dot plus a terminator IS 13. SI STOPS ON THE NUL,
; so every byte after it is the NUL again - the tail is padded rather than
; read on into whatever follows the caller's string - and a longer name is
; truncated rather than desynchronising the wire.
net_sname:
    mov cx, 13
.b:
    mov al, [es:si]
    or  al, al
    jz  .s
    inc si
.s:
    call lp_sbyte
    jc  net_abort
    loop .b
    ret

; net_rdent - one 32-byte SPEC.md 19.1 entry off the wire into net_ent
; clobbers AL, CX, SI
net_rdent:
    mov cx, DSK_DE_SIZE
    mov si, net_ent
.b:
    call lp_rbyte
    jc  net_abort
    mov [si], al
    inc si
    loop .b
    ret

; -----------------------------------------------------------------------------
; net_rdrun - take [net_len] bytes off the wire into [net_bseg]:[net_boff]
; net_wrrun - put [net_cap] bytes from [net_bseg]:[net_boff] on the wire
; clobber: AX, BX, CX, DX, DI, ES. A dead wire aborts the verb.
;
; TWO THINGS HAPPEN EVERY NET_PCHUNK BYTES and they are the same test because
; they want the same cadence. The pointer is RE-NORMALISED - the paragraph
; part of the offset folded into the segment, dskw_norm's arithmetic inside a
; driver - so a transfer longer than 64KB cannot carry off the end of a
; segment; and OSAPI_FS_PROG is stepped, which is the only way SPEC.md 12.8's
; bar moves at all here. The report is BYTES SINCE THE LAST ONE, the slot's
; contract.
;
; The write side WALKS THE SOURCE THROUGH ES AND NOT DS, and that is not a
; style choice: lplink.inc addresses [lp_base], [lp_lastop] and [lp_dlset]
; through DS with no override anywhere, so a routine that repointed DS at the
; caller's buffer would hand the transport a garbage port number.
; -----------------------------------------------------------------------------
net_rdrun:
    mov dx, [net_len+2]
    mov cx, [net_len]
    call net_xfset
.byte:
    mov ax, cx
    or  ax, dx
    jz  net_xfend
    call lp_rbyte
    jc  net_abort
    mov [es:di], al
    inc di
    inc bx
    sub cx, 1
    sbb dx, 0
    cmp bx, NET_PCHUNK
    jb  .byte
    call net_mark               ; report and re-normalise together
    jmp short .byte

net_wrrun:
    mov dx, [net_cap+2]
    mov cx, [net_cap]
    call net_xfset
.byte:
    mov ax, cx
    or  ax, dx
    jz  net_xfend
    mov al, [es:di]
    inc di
    call lp_sbyte
    jc  net_abort
    inc bx
    sub cx, 1
    sbb dx, 0
    cmp bx, NET_PCHUNK
    jb  .byte
    call net_mark
    jmp short .byte

; net_xfset - ES:DI = the caller's buffer, normalised once up front because
; the caller's offset can be anything at all; BX = 0 bytes since a report
net_xfset:
    mov es, [net_bseg]
    mov di, [net_boff]
    xor bx, bx
    jmp short net_norm

; net_xfend - the tail of a run, which is almost never a whole chunk
net_xfend:
    or  bx, bx
    jz  net_norm.r
                                ; (falls into net_mark)
; net_mark - BX bytes have moved: tell the kernel, tidy ES:DI, and start the
; next chunk's count
net_mark:
    mov ax, bx
    call OSAPI_FS_PROG
    xor bx, bx
; net_norm - fold DI's paragraph part into ES, leaving DI at 0..15. ONE body
; for both directions: the read walks a destination and the write a source,
; and the arithmetic does not know which
net_norm:
    push cx
    mov ax, di
    mov cl, 4
    shr ax, cl
    mov cx, es
    add cx, ax
    mov es, cx
    pop cx
    and di, 0x000F
.r:
    ret

; net_rdsink - swallow [net_len] bytes and store none of them
;
; The frame is a fixed size whatever this end does with it (SPEC.md 62.10.1),
; so a refusal still has to consume the run. It is NOT an abort when the wire
; dies under it: the verb is answering FERR_BIG either way, and a sink that
; died mid-run simply stops.
net_rdsink:
    mov cx, [net_len]
    mov dx, [net_len+2]
.b:
    mov ax, cx
    or  ax, dx
    jz  .out
    call lp_rbyte
    jc  .out
    sub cx, 1
    sbb dx, 0
    jmp short .b
.out:
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
    mov byte [net_lost_f], 1    ; A link that drops on a real cable drops for
    ret                         ; a reason no emulator here can reproduce, so
                                ; the page has to carry the evidence out - the
                                ; SPEC.md 18.94 discipline, one layer up

%include "netui.inc"            ; the Control Panel page (SPEC.md 31.9)
%include "netsock.inc"          ; ...the SOCKET half (SPEC.md 62.11)
%include "lplink.inc"           ; ...and the transport, shared with tests/lptlink
%define OS88UI_ARM              ; os88ui_arm/fire/armed: the press/release
%define OS88UI_NOGEST           ; the page drives its one button through the
                                ; panel's own press/drag/release cells and
                                ; os88ui_arm/fire/armed, never the install side
%define OS88UI_NOGLYPH          ; ...and draws no check box and no radio
%define OS88UI_NOBFIND          ; hit-tests its own rects: no os88ui_bfind (SPEC.md 20.5.1.3.4)
%include "os88ui.inc"           ; ...and the standard control (SPEC.md 20.5.1)

; -----------------------------------------------------------------------------
; lpl_ticks - lplink.inc's one requirement of its includer (see its header)
; out: AX = the system tick counter; everything else preserved
; -----------------------------------------------------------------------------
lpl_ticks:
    call OSAPI_GET_TICKS
    ret

; --- state -------------------------------------------------------------------
; THE ONE NON-ZERO ITEM FIRST: os88drv.py takes the TRAILING run of zeros off
; the file (drivers/os88drv.inc), so everything after it costs RAM and no
; disk.
;
; net_label - the volume's name, netui.inc's 'Link' - is the volume's name and
; NOT the drive letter: the kernel assigns the letter from the volume index
; (SPEC.md 26.4), exactly as it does for `HDD C`, so this is what the desktop
; zone and the Disk window's header say.
net_vol:    db 0xFF             ; the volume index we registered, FF = none
net_state:  db NS_NOPORT
net_flags:  db 0                ; bit 0 = it will not take writes
net_lost_f: db 0                ; 1 = the last link ended by dying, not by us
net_cmdip:  db 0                ; the NF_* letter in flight
net_lastcmd: db 0               ; ...and the one the last failure died on
net_px:     dw 0
net_py:     dw 0

; --- file mode's (SPEC.md 62.10) ---------------------------------------------
net_cwd:    dw 0                ; the far side's handle for where we STAND.
                                ; Ours as well as theirs: FSV_LIST takes no
                                ; argument, so this is the kernel's only way
                                ; back to the folder it just chdir'd into
net_full:   db 0                ; the kernel's listing filled: keep READING the
                                ; run, stop APPENDING to it
net_ent:    times DSK_DE_SIZE db 0  ; one staged 19.1 entry, off the wire

; --- and the transfer's (SPEC.md 62.10.4.3) ----------------------------------
; MEMORY rather than registers because a transfer has more live state than an
; 8086 has registers - a 32-bit capacity, a 32-bit length and a destination
; that walks - and every one of them has to survive a call into lp_rbyte.
net_cap:    dd 0                ; what we told the far side we could take - or,
                                ; writing, how much we are sending
net_len:    dd 0                ; ...and what it says it is sending
net_bseg:   dw 0                ; the buffer, which walks a segment at a time
net_boff:   dw 0                ; - see net_norm

; --- THE ABORT FRAME (net_fgate / net_pkg, netsock.inc) -----------------------
; One of each, because only the holder of [net_busy] is ever inside a gate.
net_sp:     dw 0                ; SP with the caller's registers saved on it
net_abfn:   dw 0                ; ...and the tail an abort runs: net_ffail for
                                ; a file verb, nsk_fail for a socket verb
NFR_DI      equ 0               ; the file gate's frame, from [net_sp] up -
NFR_SI      equ 2               ; a verb whose answer is in BX, CX or DX
NFR_DX      equ 4               ; writes it here (SS-relative, through BP),
NFR_CX      equ 6               ; and the tail pops it into the register
NFR_BX      equ 8

; --- the socket half's state (SPEC.md 62.11, netsock.inc) --------------------
net_busy:   db 0                ; THE WIRE'S MUTEX. One byte, and the first
                                ; thing in this driver that two tasks touch:
                                ; the file verbs are the UI task's and the
                                ; socket verbs are a package worker's
net_sk:     times NET_SOCKS db NSK_FREE     ; per handle, the state we last
                                ; saw. NSK_FREE is 0 and is never a live
                                ; state, so this array is both the allocator
                                ; and the mirror - which is what stops
                                ; NETV_STATE's free count and nsk_hchk being
                                ; two opinions about the same thing
net_sst:    db 0                ; the far side's status byte
net_sstate: db 0                ; the state nsk_reply_h means to record, IN
                                ; MEMORY because it lives across an lp_rbyte,
                                ; whose contract is about AL and says nothing
                                ; about the high half
net_slen:   dw 0                ; bytes in flight
net_addr:   times 4 db 0        ; NETV_ADDR's four bytes, STAGED. They arrive
                                ; one at a time and the frame is fixed, so a
                                ; refusal delivers four of them too - copying
                                ; straight into the caller's buffer would put
                                ; a failed answer's bytes there and then
                                ; report the failure

    OS88_DRV_END
