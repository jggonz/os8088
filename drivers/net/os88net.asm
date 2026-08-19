; =============================================================================
; OS88NET.COM - the DOS end of os8088's parallel Network drive
;
; Serves this machine's FILES over a LapLink cable, so an os8088 machine with
; NET.DRV loaded browses them as an ordinary drive. Stage 2 of
; docs/NET-PLAN.md; NET.DRV is the other end.
;
;   OS88NET [folder] [/W] [/P:378] [/RO] [/I:image]
;
;   folder  what to serve, default THE CURRENT DIRECTORY - which is the answer
;           that needs no explaining: cd to the thing you want to send and run
;           this. A relative path, '..' and a bare drive letter all work,
;           because the folder is ENTERED rather than remembered and DOS does
;           the resolving.
;   /W      serve the WHOLE MACHINE instead: the root lists every live drive.
;   /P:     pin the port. WITHOUT IT THIS SCANS - the BIOS's own table plus
;           3BC/378/278, deduped and latch-probed - because the two ends of
;           this cable are at different addresses on the two machines it was
;           written for (NET-PLAN 1.4.1: the 5150's GB101 is at 3BC and the
;           DOS box's DIO-500 at 378).
;   /RO     refuse every write. THE DEFAULT FOR A FIRST RUN, and the honest
;           setting for anything you would mind losing. Phase 1 serves no
;           write verb at all, so it is presently the only behaviour.
;   /I:     BLOCK MODE, which the redirector superseded: serve 512-byte
;           sectors out of an image file instead, and os8088 mounts it as a
;           FAT volume. Kept because it is the same wire and the same
;           transport - see SPEC.md 62.10 for why it stopped being the
;           default. Its size decides the volume's: os8088 caps a volume at
;           65,535 sectors (31.99MB) by BPB rule 8, so a bigger file is served
;           truncated and says so. Nothing in os8088 ASKS for sectors any more
;           (NET.DRV publishes no DSV_BLK), so this is a source kept live, not
;           a mode you can reach from the other end.
;
; NOTHING HERE WRITES, in phase 1. The three verbs it serves - list, chdir,
; free space - are the ones that MOUNT AND LIST, so a run of this can be
; pointed at anything on the machine without a second thought.
;
; IT IS NOT A TSR. A TSR sharing the port with DOS's own printing, and later
; with mTCP's packet driver, is a support burden with no upside; the machine
; this runs on is a transfer station, not somebody's desktop.
;
; THE SLAVE DWELLS AND THE MASTER SWEEPS (NET-PLAN 1.4.5), and the dwell is
; deliberately longer than the master's whole sweep: two scans running at
; similar rates can miss each other indefinitely, each arriving just after the
; other has left, and it presents as an intermittent cable fault rather than
; as a timing bug. This side is the one with a whole machine to spend, so this
; side is the patient one.
;
; Assemble: nasm -f bin -w+error -I drivers/net/ -o os88net.com os88net.asm
; =============================================================================

    cpu 8086                    ; it may be serving from an XT
    org 0x100

; DOS ENTERS A .COM AT OFFSET 0x100, WHICH IS THE FIRST BYTE OF THE FILE -
; not at whichever label the author thinks of as the start. This jump is
; therefore load-bearing and must stay the first thing emitted, because the
; two %includes below put ~700 bytes of transport in front of `start`.
;
; It shipped without it. DOS ran `lp_latch` (lplink.inc's first routine) with
; AX = whatever it had left there, so the program probed a garbage I/O port,
; hit that routine's `ret`, popped the word DOS pushes at PSP:0000, landed on
; the int 20h sitting there and terminated - **instantly, with nothing
; printed, whatever arguments it was given**, because not one instruction of
; this file's own code ran. The Makefile checks the first byte for exactly
; this now (`comchk`), since the failure is silent, total, and looks like a
; program that decided it had nothing to do.
;
; tests/lptlink/lptlink.asm has the same includes and did NOT have the bug,
; because it puts them at the END of the file - which is the other way to be
; safe and is not better: it depends on nobody adding a third include above.
; A jump at the entry depends on nothing.
com_entry:
    times ($$ - com_entry) db 0 ; ASSERTS that this is offset 0x100 and emits
                                ; NOTHING. If anything is ever put above it,
                                ; nasm says `TIMES value -N is negative` and
                                ; N is how many bytes got in front of the
                                ; entry. A cryptic message that fails the
                                ; BUILD beats a silent one that fails on a
                                ; machine you have to walk to
    jmp start

%include "lplink.inc"           ; the transport, shared with NET.DRV and with
%include "lplslv.inc"           ; tests/lptlink - one body, no drift

; =============================================================================
; THE TCP/IP STACK, WHICH IS os8088's OWN (SPEC.md 62.11.1)
;
; drivers/ether's stack talks to its card through five routines and knows
; nothing else about it, so pktdrv.inc puts a PACKET DRIVER underneath it and
; the whole of inet/tcp/dns runs here unchanged. ethsock.inc is the socket
; verbs themselves, the same bodies ETHER.DRV serves - so a bug fixed in one
; end is fixed in both, which two implementations could never promise.
;
; The stack asks the KERNEL for exactly two things and neither is a kernel
; question: the tick counter and a random word. On DOS they are the BIOS
; counter and a small LCG, named so the shared code needs no %ifdef.
; =============================================================================
%define ETH_NOEMIT 1            ; **THE BUFFERS DO NOT GO IN THE FILE.** See the
                                ; block at the end of this program and the note
                                ; in ethstate.inc: they were 16,776 bytes of
                                ; literal zeros on the floppy, 59% of the whole
                                ; .COM, and a .COM already owns the memory past
                                ; its image
OSAPI_GET_TICKS equ dos_ticks   ; ...NEAR calls here, where a package's are FAR
OSAPI_RAND      equ dos_rand

; **THE ORDER IS ether.asm's AND IS NOT FREE**: ethstate.inc sizes its socket
; arrays with constants tcp.inc defines, so the state comes LAST - which is
; exactly where the driver keeps it, for exactly this reason.
%include "nwire.inc"            ; the wire alphabet, SHARED (62.11.1)
%include "netpkg.inc"           ; the NETV_/NSK_/NETE_ numbering, which is the
                                ; SAME on both ends by construction
PKGV_IDENT  equ 0               ; apps/os88api.inc's, which a .COM does not
                                ; include - netpkg.inc names it
%include "ethsock.inc"
%include "ethusr.inc"
%include "pktdrv.inc"           ; ...where ne2000.inc would be
%include "inet.inc"
%include "tcp.inc"
%include "dns.inc"
%include "ethstate.inc"
%include "nwslv.inc"            ; ...and the socket commands, served



; --- dos_ticks - the BIOS tick counter, as OSAPI_GET_TICKS answers it --------
; out: AX = the low word of 0040:006C. The stack only ever SUBTRACTS two of
; these, so the low word alone is right and the midnight wrap is somebody
; else's problem in both directions (SPEC.md 45.15's `js`, one layer down).
dos_ticks:
    push ds
    push bx
    mov bx, 0x40
    mov ds, bx
    mov ax, [0x6C]
    pop bx
    pop ds
    ret

; --- dos_rand - OSAPI_RAND's shape, and it is used for the same two things ---
; A sequence number and a DNS transaction id: what they need is to be hard to
; guess by accident, not by an attacker, so an LCG seeded from the clock is
; the same bargain the kernel's own makes.
dos_rand:
    push dx
    mov ax, [dos_seed]
    mov dx, 25173
    mul dx
    add ax, 13849
    mov [dos_seed], ax
    pop dx
    ret
dos_seed:   dw 0

NC_INFO     equ 'I'
NC_READ     equ 'R'
NC_WRITE    equ 'W'
NC_BYE      equ 'X'

; --- and file mode's (SPEC.md 62.10.1) ---------------------------------------
; READ is 'G' and WRITE will be 'U' because 'R' and 'W' ARE ALREADY TAKEN by
; NC_READ and NC_WRITE above - the same one-byte space, and THE SAME COMMAND
; LOOP right here, which is what makes the collision real rather than
; theoretical (SPEC.md 62.10.1).
NF_LIST     equ 'L'             ; handle -> status, count, count x 32 bytes
NF_CHDIR    equ 'C'             ; handle -> status, the PARENT's handle
NF_DFREE    equ 'F'             ; -> status, free dword, granule word
NF_STAT     equ 'S'             ; handle, 13-byte name -> status, handle,
                                ;                         size dword, attr
NF_READ     equ 'G'             ; handle, cap dword -> status, len dword, bytes
NF_READAT   equ 'A'             ; handle, off dword, cap word
                                ;                 -> status, len word, bytes
NF_WRITE    equ 'U'             ; folder, name, len dword, bytes -> status
NF_APPEND   equ 'P'             ; folder, name, len WORD, bytes -> status
NF_DELETE   equ 'D'             ; folder, name -> status
NF_RENAME   equ 'N'             ; folder, old name, new name -> status
NF_MKDIR    equ 'M'             ; folder, name -> status
NF_RMDIR    equ 'K'             ; folder, name -> status
NF_COPY     equ 'Y'             ; src folder, dst folder, name -> status. Both
                                ; ends are OURS, so the file never crosses the
                                ; cable (SPEC.md 62.9.8)
NF_ENUM     equ 'E'             ; folder, ordinal -> status, ONE 32-byte entry.
                                ; NF_LIST is about the folder the master is
                                ; STANDING in; this is about any folder at all,
                                ; which is what a recursive COPY walks its
                                ; source with (SPEC.md 62.10.6)
NF_RMTREE   equ 'T'             ; folder, name -> status. NF_RMDIR's frame and
                                ; a DIFFERENT COMMAND, which is 62.10.1's rule
                                ; and also the safe way round: a far side built
                                ; before this verb ignores an unknown letter,
                                ; where a mode flag on RMDIR would have made an
                                ; old one empty a folder and answer OK

RT_DEPTH    equ 16              ; how deep the walk will go before refusing.
                                ; The REAL bound is rtpath overflowing - DOS's
                                ; own path limit gets there first - and this is
                                ; the belt to that brace
RT_PATHMAX  equ 95              ; ...and that bound, in characters. The longest
                                ; thing rt_build can produce is this + '\' +
                                ; a 12-character 8.3 name + a NUL, which is
                                ; what sizes the buffer below - so the WRITE
                                ; can never overrun and the length test is
                                ; about the RESULT rather than about the room
RT_BUFSZ    equ RT_PATHMAX + 1 + 12 + 1 + 3

; A FILE-MODE STATUS IS A FERR_* AND A BLOCK-MODE ONE IS AN INT 13H CODE, and
; they are NOT the same numbering however alike two small integers look. The
; file verbs spent phases 1 and 2 answering a 2 for "no such
; thing" - which is FERR_IO, "unrecoverable read/write error". It cost
; nothing then only because the driver mapped every non-zero status onto a
; code of its own; it stops being free the moment net_wstat passes one
; through, which is what the write path does.
FERR_OK     equ 0               ; ...apps/os88api.inc's numbering, verbatim
FERR_IO     equ 2
FERR_NAME   equ 3
FERR_NOENT  equ 4
FERR_EXIST  equ 5
FERR_FULL   equ 6
FERR_DIRFULL equ 7              ; ...and what srv_rmtree answers for a path it
                                ; cannot name (62.10.7.2)
FERR_PROT   equ 8
FERR_WPROT  equ 9

NST_OK      equ 0x00
NST_WPROT   equ 0x03
NST_NOSEC   equ 0x04

SEC_MAX     equ 0xFFFF          ; os8088's own cap: BPB rule 8 (SPEC.md 18.2)

; --- handles (SPEC.md 62.9.1) ------------------------------------------------
; A handle is OPAQUE TO THE KERNEL and this is what it opaquely is: a slot in
; an append-only table of (parent handle, 8.3 name), so a path is rebuilt by
; walking UP. Handle 0 is the root and is not in the table; slot i is handle
; i+1.
;
; A TABLE OF NAMES AND NOT A TABLE OF PATHS, which is 16 bytes a folder rather
; than 68 - and the parent word is then not overhead at all, because FSV_CHDIR
; has to answer with it (62.9.1) and a path table would have to parse one back
; out of a string. DOS has no inode to borrow, so something here has to hold
; the shape of the tree; this is the cheapest thing that does.
;
; It is DEDUPED on (parent, name), so re-listing a folder hands out the handle
; it had before. That is what makes a handle survive the kernel caching one in
; a Disk window's listing and using it minutes later.
HD_MAX      equ 64              ; folders named in one session
HD_PAR      equ 0               ; word: the parent's handle
HD_NAME     equ 2               ; 13 bytes: 8.3 and a NUL
HD_SZ       equ 16
HD_DEPTH    equ 16              ; ...and the deepest chain hd_path will walk.
                                ; DOS's own path limit is about this, and an
                                ; unbounded walk over a table this side wrote
                                ; is still a loop something could corrupt into
                                ; running forever

DE_SZ       equ 32              ; OSAPI_FS_ENT's staged entry
DE_TYPE     equ 16              ; word: 0 file, 1 package, 2 folder
DE_HAND     equ 18              ; word: ours
DE_SIZE     equ 20              ; dword

; --- the DTA, as int 21h 4Eh/4Fh fills it ------------------------------------
DTA_ATTR    equ 21
DTA_SIZE    equ 26
DTA_NAME    equ 30

start:
    cld
    mov si, s_banner
    call puts

    call parse_args
    jc  .bye
    cmp byte [helpflag], 0      ; /? : say what the switches are and stop,
    je  .nohelp                 ; before anything touches a drive or a port
    mov si, s_help
    call puts
    jmp .bye
.nohelp:

    mov ax, 0x2524              ; FAIL every critical error, before anything
    mov dx, crit24              ; touches a drive - the /W scan is the first
    int 0x21                    ; thing that can meet an empty one, and DOS
                                ; restores this vector itself at 4Ch
    call setup_root
    jc  .bye
    call open_image             ; ...only if /I: named one
    jc  .bye

    call net_bss_clear          ; ...the reserved buffers, BEFORE anything reads
                                ; one (see the block at the end of this file)
    call net_start              ; /N: the packet driver and an address, BEFORE
                                ; the cable - it is the slower of the two to
                                ; fail and the one with something to say

    call lpl_scan
    call report_ports
    call pin_or_scan
    jc  .noport

    mov si, s_ready
    call puts
.serve:
    call listen_once            ; one full sweep of the candidates
    cmp byte [slv_abort], 0     ; an ESC pressed INSIDE a wait is consumed
    jne .stop                   ; there, so the test below cannot see it
    call lpl_kbhit
    jnc .serve
    cmp al, 27
    jne .serve
.stop:
    mov si, s_stopped
    call puts
.bye:
    call ne_stop                ; release_type BEFORE DOS frees this block, or
                                ; the next frame the card sees far-calls
                                ; pk_recv's offset in whatever now lives here.
                                ; pk_handle guards it, not eth_up: a /N run
                                ; that registered and then lost DHCP has
                                ; eth_up 0 and a LIVE upcall
    mov ax, 0x4C00
    int 0x21
.noport:
    mov si, s_noport
    call puts
    jmp short .bye

; --- net_bss_clear - zero what the file no longer carries --------------------
; DOS zeroes nothing above a .COM's image, so this is what makes the reserved
; block behave exactly as the emitted one did. ~17KB of rep stosw, once.
net_bss_clear:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, net_bss0
    mov cx, (net_bss1 - net_bss0 + 1) / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_start - /N: bring the TCP/IP stack up and PROMISE SOCKETS
; in:  nothing; out: nothing - a refusal is reported and the run carries on
;
; **THE PROMISE IS [lp_myver] AND IT IS MADE HERE OR NOWHERE.** The version
; byte in the handshake is the whole of how os8088 learns whether this far end
; speaks the socket verbs (netsock.inc), so it goes to NET_VER_SOCK only when
; there is a stack behind it. A run that fails to find a packet driver, or
; fails to get a lease, therefore serves FILES exactly as it always did and
; the other end says `No partner` in the browser rather than hanging on an
; open that can never finish.
; -----------------------------------------------------------------------------
net_start:
    push ax
    push si
    cmp byte [netflag], 0
    je  .out
    mov si, s_nwtry
    call puts
    call nw_up
    jc  .no
    mov byte [lp_myver], NET_VER_SOCK
    mov si, s_nwok
    call puts
    mov si, eth_ip              ; ...and WHICH address, because a machine with
    call put_ip                 ; two networks on it is an ordinary thing
    mov si, s_crlf
    call puts
    jmp short .out
.no:
    call puts                   ; nw_up left the reason in SI
    mov si, s_nwfiles
    call puts
.out:
    pop si
    pop ax
    ret

; --- put_ip - the four bytes at DS:SI as a dotted quad -----------------------
put_ip:
    push ax
    push bx
    push cx
    push si
    mov cx, 4
.b:
    mov al, [si]
    inc si
    xor ah, ah
    call put_dec
    dec cx
    jz  .out
    mov al, '.'
    call putc
    jmp short .b
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- put_dec - AX as decimal, no padding -------------------------------------
put_dec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or  ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    call putc
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; LISTENING
; =============================================================================

; -----------------------------------------------------------------------------
; listen_once - dwell on each candidate in turn; serve if one calls
; -----------------------------------------------------------------------------
listen_once:
    push ax
    push bx
    push cx
    push di
    mov cl, [ncand]
    or  cl, cl
    jz  .out
    xor ch, ch
    xor di, di
.one:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next
    cmp byte [pinned], 0        ; /P: was given: only that port is listened on
    je  .listen
    shl bx, 1
    mov ax, [cand_base+bx]
    cmp ax, [pinbase]
    jne .next2
    shr bx, 1
.listen:
    mov bx, di
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call slv_hunt               ; the 8-nibble magic window
    jc  .quiet
    mov si, s_called
    call puts
    mov ax, [lp_base]
    call puthex16
    call crlf
    call slv_reply
    jc  .drop
    call serve
.drop:
    call lp_restore
    pop cx
    jmp short .out
.quiet:
    call lp_restore
    cmp byte [slv_abort], 0
    jne .stop
.next2:
.next:
    inc di
    pop cx
    loop .one
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret
.stop:
    pop cx
    jmp short .out

; -----------------------------------------------------------------------------
; serve - the command loop, until the master says goodbye or the link dies
; -----------------------------------------------------------------------------
serve:
    push ax
    push bx
    push cx
    push dx
    push si
.cmd:
    call lp_rbyte_w             ; a tick-bounded wait: the master may take its
    jc  .out                    ; time between commands
    cmp al, NC_INFO
    je  .info
    cmp al, NC_READ
    je  .read
    cmp al, NC_WRITE
    je  .write
    cmp al, NC_BYE
    je  .bye
    cmp al, NF_LIST
    je  .flist
    cmp al, NF_CHDIR
    je  .fchdir
    cmp al, NF_DFREE
    je  .fdfree
    cmp al, NF_STAT
    je  .fstat
    cmp al, NF_READ
    je  .fread
    cmp al, NF_READAT
    je  .freadat
    cmp al, NF_WRITE
    je  .fwrite
    cmp al, NF_APPEND
    je  .fappend
    cmp al, NF_DELETE
    je  .fdelete
    cmp al, NF_RENAME
    je  .frename
    cmp al, NF_MKDIR
    je  .fmkdir
    cmp al, NF_RMDIR
    je  .frmdir
    cmp al, NF_COPY
    je  .fcopy
    cmp al, NF_ENUM
    je  .fenum
    cmp al, NF_RMTREE
    je  .frmtree
    cmp al, NW_OPEN
    je  .nwopen
    cmp al, NW_LISTEN
    je  .nwlisten
    cmp al, NW_ACCEPT
    je  .nwaccept
    cmp al, NW_STAT
    je  .nwstat
    cmp al, NW_SEND
    je  .nwsend
    cmp al, NW_RECV
    je  .nwrecv
    cmp al, NW_CLOSE
    je  .nwclose
    jmp .cmd                    ; ...NEAR: the socket handlers below sit
                                ; between here and the top of the loop

.nwopen:
    call nw_s_open
    jmp .cmd
.nwlisten:
    call nw_s_listen
    jmp .cmd
.nwaccept:
    call nw_s_accept
    jmp .cmd
.nwstat:
    call nw_s_stat
    jmp .cmd
.nwsend:
    call nw_s_send
    jmp .cmd
.nwrecv:
    call nw_s_recv
    jmp .cmd
.nwclose:
    call nw_s_close
    jmp .cmd

.fwrite:
    call srv_write
    jc  .out
    jmp .cmd
.fappend:
    call srv_append
    jc  .out
    jmp .cmd
.fdelete:
    call srv_delete
    jc  .out
    jmp .cmd
.frename:
    call srv_rename
    jc  .out
    jmp .cmd
.fmkdir:
    call srv_mkdir
    jc  .out
    jmp .cmd
.frmdir:
    call srv_rmdir
    jc  .out
    jmp .cmd
.frmtree:
    call srv_rmtree
    jc  .out
    jmp .cmd
.fcopy:
    call srv_copy
    jc  .out
    jmp .cmd

.flist:
    call srv_list
    jc  .out
    jmp .cmd
.fenum:
    call srv_enum
    jc  .out
    jmp .cmd
.fchdir:
    call srv_chdir
    jc  .out
    jmp .cmd
.fdfree:
    call srv_dfree
    jc  .out
    jmp .cmd
.fstat:
    call srv_stat
    jc  .out
    jmp .cmd
.fread:
    call srv_read
    jc  .out
    jmp .cmd
.freadat:
    call srv_readat
    jc  .out
    jmp .cmd

.info:
    mov al, NST_OK
    call lp_sbyte
    jc  .out
    mov ax, [nsecs]
    call lp_sword
    jc  .out
    mov al, [roflag]
    call lp_sbyte
    jc  .out
    jmp .cmd

.read:
    ; ONE COMMAND, `count` FRAMES OF {status, 512 bytes}. Every frame is 512
    ; bytes whatever the status says, so a sector this side refuses cannot
    ; leave the master waiting for data that is not coming - see net.asm's
    ; protocol header.
    call get_req                ; DX = LBA, CL = count
    jc  .out
    mov [nrun], cl
    or  cl, cl
    jz  .cmd
.rsec:
    mov byte [rstat], NST_OK
    call seek_lba
    jc  .rbad
    push dx
    mov ah, 0x3F                ; DOS read
    mov bx, [fh]
    mov cx, 512
    mov dx, secbuf
    int 0x21
    pop dx
    jc  .rbad
    cmp ax, 512
    je  .rgo
.rbad:
    mov byte [rstat], NST_NOSEC
    call zero_buf               ; a refused sector still carries its 512 bytes
.rgo:
    mov al, [rstat]
    call lp_sbyte
    jc  .out
    mov si, secbuf
    mov cx, 512
.rsend:
    lodsb
    call lp_sbyte
    jc  .out
    loop .rsend
    inc dx
    dec byte [nrun]
    jnz .rsec
    call tick_dot               ; one dot per RUN now, not per sector: it is
    jmp .cmd                    ; console output in the middle of a transfer,
                                ; and the master is waiting through it

.write:
    ; The mirror: the whole run's data arrives first, then one status byte per
    ; sector goes back. The bytes are consumed whether or not they are kept,
    ; for the reason above.
    call get_req
    jc  .out
    mov [nrun], cl
    mov [nrun2], cl
    or  cl, cl
    jz  .cmd
    mov si, dx                  ; SI = the LBA, so DX is free for int 21h
.wsec:
    mov di, secbuf
    mov cx, 512
.wrecv:
    call lp_rbyte
    jc  .out
    mov [di], al
    inc di
    loop .wrecv
    mov byte [rstat], NST_OK
    cmp byte [roflag], 0
    jne .wprot
    mov dx, si
    call seek_lba
    jc  .wbad
    mov ah, 0x40                ; DOS write
    mov bx, [fh]
    mov cx, 512
    mov dx, secbuf
    int 0x21
    jc  .wbad
    cmp ax, 512
    je  .wnext
.wbad:
    mov byte [rstat], NST_NOSEC
    jmp short .wnext
.wprot:
    mov byte [rstat], NST_WPROT
.wnext:
    mov bl, [nrun2]             ; bank this sector's verdict; they all go back
    sub bl, [nrun]              ; together, after the data
    xor bh, bh
    add bx, wstat
    mov al, [rstat]
    mov [bx], al
    inc si
    dec byte [nrun]
    jnz .wsec
    mov cl, [nrun2]             ; ...and now the verdicts
    xor ch, ch
    mov si, wstat
.wsend:
    lodsb
    call lp_sbyte
    jc  .out
    loop .wsend
    call tick_dot
    jmp .cmd

.bye:
    mov si, s_bye
    call puts
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; get_req - read an LBA word and a count byte
; out: DX = LBA, CL = count; CF=1 = the link went away
; -----------------------------------------------------------------------------
get_req:
    call lp_rword
    jc  .bad
    mov dx, ax
    call lp_rbyte
    jc  .bad
    mov cl, al
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; seek_lba - put the file pointer at DX * 512
; out: CF=1 = past the end, or the seek failed
;
; DX*512 is a 32-bit byte offset and int 21h AH=42h wants it in CX:DX, which
; is exactly the shape a 16-bit LBA shifted left nine times takes: the high
; word is DX >> 7 and the low is DX << 9. No 32-bit arithmetic anywhere.
; -----------------------------------------------------------------------------
seek_lba:
    push ax
    push bx
    push cx
    push dx
    cmp dx, [nsecs]
    jae .bad                    ; past the end of what we told them we had
    mov ax, dx
    mov cx, dx
    mov bx, 9
.shl:
    shl ax, 1                   ; the low word
    dec bx
    jnz .shl
    mov bx, 7
.shr:
    shr cx, 1                   ; ...and the high word
    dec bx
    jnz .shr
    mov dx, ax
    mov ah, 0x42
    mov al, 0                   ; from the start of the file
    mov bx, [fh]
    int 0x21
    jc  .bad
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; SETUP
; =============================================================================

; -----------------------------------------------------------------------------
; parse_args - the command tail at 80h
; -----------------------------------------------------------------------------
parse_args:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, 0x81
    xor ch, ch
    mov cl, [0x80]
    or  cl, cl
    jz  .done
.next:
    call skip_sp
    jcxz .argdone
    jmp short .word
.argdone:                   ; a TRAMPOLINE, because `jcxz` has no near form on
    jmp .done               ; an 8086 and /I: put the end of this routine out
                            ; of one byte's reach. The same shape serves the
                            ; three `jcxz .done`s below, which are nearer
.word:
    mov al, [si]
    cmp al, '/'
    je  .opt
    cmp al, '-'
    je  .opt
    ; A BARE WORD IS THE FOLDER TO SERVE, which it did not used to be - it was
    ; the image file, back when this served sectors. The redirector is what the
    ; cable is for now (SPEC.md 62.10), so the ordinary argument is the
    ; ordinary thing, and block mode's image moved behind /I:.
    mov di, dirarg
.name:
    mov al, [si]
    cmp al, ' '
    jbe .namedone
    mov [di], al
    inc di
    inc si
    dec cx
    jnz .name
.namedone:
    mov byte [di], 0
    jmp short .next
.opt:
    inc si
    dec cx
    jcxz .argdone
    mov al, [si]
    or  al, 0x20
    cmp al, 'r'
    je  .ro
    cmp al, 'p'
    je  .port
    cmp al, 'w'
    je  .whole
    cmp al, 'i'
    je  .image
    cmp al, 'n'
    je  .net
    cmp al, '?'
    je  .help
    cmp al, 'h'
    je  .help
    jmp short .skipopt
.help:
    mov byte [helpflag], 1
    jmp short .skipopt
.net:
    mov byte [netflag], 1
    jmp short .skipopt
.ro:
    mov byte [roflag], 1
    jmp short .skipopt
.whole:
    mov byte [wholemc], 1
    jmp short .skipopt
.image:
    inc si                      ; past 'I'
    dec cx
    jcxz .argdone
    cmp byte [si], ':'
    jne .skipopt
    inc si
    dec cx
    mov di, imgname             ; ...block mode, which the redirector
.iname:                         ; superseded and which is kept because the
    jcxz .inamed                ; wire is the same one (SPEC.md 62.10)
    mov al, [si]
    cmp al, ' '
    jbe .inamed
    mov [di], al
    inc di
    inc si
    dec cx
    jmp short .iname
.inamed:
    mov byte [di], 0
    jmp .next
.port:
    inc si                      ; past 'P'
    dec cx
    jnz .phave                  ; **NOT jcxz**: it is short-only on an 8086 and
    jmp .argdone                ; /N pushed .argdone out of its reach. `dec cx`
.phave:                         ; has already set ZF the same way
    cmp byte [si], ':'
    jne .skipopt
    inc si
    dec cx
    call gethex                 ; AX = the base, SI/CX advanced
    mov [pinbase], ax
    mov byte [pinned], 1
    jmp .next
.skipopt:
    mov al, [si]
    cmp al, ' '
    jbe .next
    inc si
    dec cx
    jnz .skipopt
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

skip_sp:
    jcxz .out
.l:
    cmp byte [si], ' '
    ja  .out
    inc si
    dec cx
    jnz .l
.out:
    ret

gethex:                         ; SI/CX -> AX, stopping at the first non-digit
    push bx
    xor ax, ax
.d:
    jcxz .out
    mov bl, [si]
    cmp bl, ' '
    jbe .out
    or  bl, 0x20
    cmp bl, '9'
    jbe .num
    sub bl, 'a' - 10
    jmp short .add
.num:
    sub bl, '0'
.add:
    cmp bl, 15
    ja  .out
    push cx
    mov cl, 4
    shl ax, cl
    pop cx
    xor bh, bh
    add ax, bx
    inc si
    dec cx
    jmp short .d
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; open_image - open it and work out how many sectors it holds
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; setup_root - decide what this run is serving, and say so
; out: CF=1 = the folder named on the command line is not one
;
; A NAMED FOLDER IS ENTERED RATHER THAN REMEMBERED. `chdir` then `root_here`
; is two DOS calls where storing the string would be none, and it is worth
; them: DOS resolves '..', a bare drive letter and a relative path for us, and
; then hands the answer back FULLY QUALIFIED - which is what every path this
; program builds is rooted on. Storing the argument would put all three of
; those cases in here, badly.
; -----------------------------------------------------------------------------
setup_root:
    push ax
    push dx
    push si
    cmp byte [wholemc], 0
    je  .here
    mov si, s_wmc               ; /W: there is no root path at all - the first
    call puts                   ; level IS a drive (hd_path's .wroot)
    jmp short .ok
.here:
    cmp byte [dirarg], 0
    je  .cwd
    mov dx, dirarg              ; ...and a drive letter in it needs the drive
    cmp byte [dirarg+1], ':'    ; selected too, or chdir moves the CURRENT
    jne .cd                     ; drive's directory and 47h reports the other
    mov dl, [dirarg]            ; one's
    or  dl, 0x20
    sub dl, 'a'
    mov ah, 0x0E
    int 0x21
    mov dx, dirarg
.cd:
    mov ah, 0x3B                ; chdir
    int 0x21
    jc  .nodir
.cwd:
    call root_here
    mov si, s_root
    call puts
    mov si, rootpath
    call puts
    call crlf
.ok:
    ; --- WHAT THIS RUN IS ACTUALLY DOING, said once and in full -----------
    ; Every one of these is a choice the user made or a default they did not
    ; know they took, and the two ends of this cable are usually in different
    ; rooms: a run that prints only what it is serving leaves the port and the
    ; write permission to be inferred from behaviour.
    mov si, s_ronly             ; ...writes, first: it is the one that can
    cmp byte [roflag], 0        ; surprise somebody about their own disk
    jne .rosay
    mov si, s_rwok
.rosay:
    call puts
    mov si, s_portpin           ; the port, and WHY it is that port
    cmp byte [pinned], 0
    jne .portsay
    mov si, s_portscan
.portsay:
    call puts
    cmp byte [pinned], 0
    je  .noport2
    mov ax, [pinbase]
    call puthex16
.noport2:
    call crlf
    cmp byte [imgname], 0       ; block mode is the exception, so it is only
    je  .noimg                  ; mentioned when it is on
    mov si, s_imgsay
    call puts
    mov si, imgname
    call puts
    call crlf
.noimg:
    pop si
    pop dx
    pop ax
    clc
    ret
.nodir:
    mov si, s_nodir
    call puts
    pop si
    pop dx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; open_image - block mode's file, IF /I: named one (SPEC.md 62.10)
; -----------------------------------------------------------------------------
open_image:
    cmp byte [imgname], 0       ; the ordinary case now: no image, and the
    jne .named                  ; NC_* commands answer NST_NOSEC to a master
    clc                         ; that will never send them
    ret
.named:
    push bx
    push cx
    push dx
    mov si, s_serving
    call puts
    mov si, imgname
    call puts
    call crlf

    mov ax, 0x3D02              ; read/write
    cmp byte [roflag], 0
    je  .op
    mov ax, 0x3D00              ; ...or read-only, if /RO said so
.op:
    mov dx, imgname
    int 0x21
    jc  .bad
    mov [fh], ax

    mov bx, ax                  ; seek to the end: CX:DX = the size
    mov ax, 0x4202
    xor cx, cx
    xor dx, dx
    int 0x21
    jc  .bad
    ; DX:AX = size. sectors = size / 512, saturating at os8088's own cap.
    mov cl, 9
    shr ax, cl
    mov bx, dx
    mov cl, 7
    shl bx, cl
    or  ax, bx
    mov cl, 9
    shr dx, cl
    jnz .cap                    ; over 32MB: serve the first 32MB and say so
    or  ax, ax
    jz  .empty
    jmp short .have
.cap:
    mov ax, SEC_MAX
    mov si, s_capped
    call puts
.have:
    mov [nsecs], ax
    mov si, s_size
    call puts
    call putdec16
    mov si, s_secs
    call puts
    cmp byte [roflag], 0
    je  .rw
    mov si, s_roflag
    call puts
.rw:
    call crlf
    pop dx
    pop cx
    pop bx
    clc
    ret
.empty:
    mov si, s_tiny
    call puts
    jmp short .bad2
.bad:
    mov si, s_noopen
    call puts
.bad2:
    pop dx
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; SERVING FILES (SPEC.md 62.10.3)
;
; WHAT IS SERVED, and it is the user's choice rather than this program's: by
; default the CURRENT DIRECTORY and everything under it, which is the answer
; that needs no explaining - you cd to the thing you want to send and run
; this. `/W` widens it to the whole machine, whose root lists the DRIVES.
;
; Nothing here writes. Phase 1 is the three verbs that MOUNT AND LIST, so a
; run of this can be pointed at anything at all without a second thought, and
; the write verbs land with their own milestone.
; =============================================================================

; -----------------------------------------------------------------------------
; srv_list - NF_LIST: a handle, then status, a count, and that many entries
; out: CF=1 = the link went away
;
; THE COUNT IS SENT BEFORE THE ENTRIES AND DOS WILL NOT SAY HOW MANY THERE
; ARE, so the directory is walked TWICE - once to count, once to send. That is
; the price of a fixed frame, and the fixed frame is what makes a refusal safe
; (net.asm's protocol header): the master reads exactly `count` entries
; whatever happens in the middle, so neither end can be left talking into one
; that has stopped listening.
;
; The second walk is CLAMPED to the first's count in both directions - short
; is padded with a blank entry, long is cut - because a directory can change
; between two walks and a wire that has promised a number must deliver it.
; -----------------------------------------------------------------------------
srv_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword               ; the folder's handle
    jc  .lost
    mov [srv_h], ax

    ; THE WHOLE-MACHINE ROOT IS ANSWERED FIRST, and the order is the whole of
    ; it: `hd_path` REFUSES that handle on purpose (its `.wroot` has nothing to
    ; walk - under /W the first level IS a drive, so the root has no path of
    ; its own), so asking it first turned every /W listing into `no such
    ; handle` on a link that was working perfectly.
    cmp byte [wholemc], 0       ; the whole-machine root is a list of DRIVES
    je  .named                  ; and not a directory at all
    cmp word [srv_h], 0
    jne .named
    call srv_drives
    jmp short .out
.named:
    call hd_path                ; -> pathbuf, or CF=1 for a handle we never
    jc  .nodir                  ; issued

.files:
    call srv_count              ; walk 1: how many are there
    jc  .nodir
    mov [srv_n], ax
    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    mov ax, [srv_n]
    call lp_sword
    jc  .lost
    call srv_send               ; walk 2: send exactly that many
    jc  .lost
    jmp short .out

.nodir:
    mov al, FERR_NOENT           ; a count of zero still completes the frame,
    call lp_sbyte               ; so a refusal costs the master nothing but
    jc  .lost                   ; an empty folder
    xor ax, ax
    call lp_sword
    jc  .lost
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_enum - NF_ENUM: a handle and an ORDINAL -> status and ONE entry
; out: CF=1 = the link went away
;
; This is the verb a FOLDER copy walks its source with, and it is deliberately
; not NF_LIST with a count of one. LIST is about the folder the master is
; standing in and appends into its one global listing; a recursive copy walks
; a folder several levels below that and must disturb neither (SPEC.md
; 62.9.7). So the folder is an argument and nothing here touches the cwd.
;
; THREE STATUSES, AND THE THIRD IS THE POINT. `NST_OK` means an entry follows.
; `FERR_NOENT` means PAST THE LAST ENTRY, which is how every walk of every
; folder finishes and is not an error. `FERR_IO` means this folder could not
; be walked at all - a handle we never issued, or DOS refusing the path - and
; it must not be reported as the end, because the master's end-of-folder
; answer is the same `CF=1, AX=0` an absent verb gives: fcp_scan would take an
; unwalkable subdirectory for an empty one and report a paste DONE over a
; subtree it never copied.
;
; The 32 bytes go out whatever the status said (62.10.1's fixed frame), so a
; refusal costs the master one blank entry and never a resynchronisation.
;
; THE COUNTDOWN IS IN BX AND NOT CX, which is srv_count's own choice for the
; same reason: `srv_first` spends CX on findfirst's attribute mask, and BX is
; the register DOS's findfirst/findnext are already trusted to preserve here.
; -----------------------------------------------------------------------------
srv_enum:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword               ; the folder's handle
    jc  .lost
    mov [srv_h], ax
    call lp_rword               ; ...and how far into it
    jc  .lost
    mov [srv_ord], ax

    cmp byte [wholemc], 0       ; the whole-machine root is a list of DRIVES,
    je  .named                  ; exactly as srv_list answers it - and it is
    cmp word [srv_h], 0         ; answered FIRST, because hd_path refuses that
    jne .named                  ; handle on purpose (it has no path of its own)
    call srv_drv_nth
    jmp short .out

.named:
    call hd_path                ; -> pathbuf, or CF=1 for a handle we never
    jc  .nodir                  ; issued
    mov bx, [srv_ord]
    call srv_first
    jc  .none
.step:
    call srv_keep               ; CF=0 = one we would show, so the ordinal
    jc  .skip                   ; counts the same entries the LISTING does
    or  bx, bx
    jz  .found
    dec bx
.skip:
    call srv_more
    jnc .step

    ; NOTHING LEFT - BUT IS THAT AN EMPTY FOLDER OR A FOLDER THAT IS GONE?
    ; DOS answers findfirst with error 2 for BOTH, so the walk alone cannot
    ; tell them apart, and this verb's whole contract is that it must: the end
    ; of a folder is `FERR_NOENT` and a folder that cannot be walked is
    ; `FERR_IO`, because the master reads the first as `CF=1, AX=0` and would
    ; take a vanished subdirectory for an empty one - a paste reporting DONE
    ; over a subtree that is not there. So the folder itself is stat'd, and
    ; only HERE: once per walk rather than once per entry, and never on the
    ; ordinary path where an entry was found.
    ;
    ; The root is exempt because it always exists and because `findfirst` on a
    ; bare `C:\` is not a question DOS has a good answer to.
.none:
    cmp word [srv_h], 0
    je  .end
    call srv_isdir              ; pathbuf: still a directory? CF=1 = no
    jc  .nodir
    jmp short .end              ; ...it is there and it is empty

.found:
    call srv_ent                ; the DTA -> entbuf, registering a handle for
                                ; a folder or a package exactly as a listing
                                ; does - a copy DIVES on that handle

    ; A FOLDER THAT GOT HANDLE 0 IS A FULL TABLE, AND IT IS A REFUSAL HERE.
    ; `hd_get` answers 0 when `hdtab`'s 64 slots are gone, and 0 IS THE ROOT
    ; (62.9.1's convention) - so a copy engine handed it would `chdir` to the
    ; root and start copying the whole volume into a subfolder of itself,
    ; bounded only by the engine's depth limit and silent the whole way.
    ; `srv_stat` learned the same lesson at 62.10.4.7 and for the same reason:
    ; a success the caller cannot use is worse than a refusal. A LISTING
    ; cannot say this - it has one status for the whole frame - which is
    ; another thing the per-entry verb buys.
    cmp word [entbuf + DE_TYPE], 2
    jne .send
    cmp word [entbuf + DE_HAND], 0
    je  .nodir
.send:
    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    call srv_put
    jc  .lost
    jmp short .out

.end:
    mov al, FERR_NOENT          ; the ordinary end of a walk
    jmp short .blank
.nodir:
    mov al, FERR_IO             ; ...and NOT the end: see the header
.blank:
    call lp_sbyte
    jc  .lost
    call srv_blank
    call srv_put                ; the entry is owed whatever the status said
    jc  .lost
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_drv_nth - the whole-machine root's Nth live drive, as one ENUM reply
; out: CF=1 = the link went away
;
; srv_drives' scan with the send taken out of the loop. The two are not folded
; together because they answer different frames - a count and every entry
; against a status and one - and the shared part is `drive_live`, which is
; already a routine.
; -----------------------------------------------------------------------------
srv_drv_nth:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [srv_ord]
    mov cx, 26
    mov dl, 1
.scan:
    call drive_live             ; DL and CX preserved by contract
    jc  .next
    or  bx, bx
    jz  .found
    dec bx
.next:
    inc dl
    loop .scan
    mov al, FERR_NOENT          ; past the last drive: the end of the root
    call lp_sbyte
    jc  .lost
    call srv_blank
    call srv_put
    jc  .lost
    jmp short .out
.found:
    call srv_blank
    mov al, dl
    add al, 'A' - 1
    mov [entbuf], al            ; 'C:' - a NAME, and the kernel prints it as
    mov byte [entbuf+1], ':'    ; one (SPEC.md 26.4)
    mov byte [entbuf+2], 0
    mov word [entbuf + DE_TYPE], 2
    push dx
    xor ax, ax                  ; parent = the root
    mov si, entbuf
    call hd_get
    pop dx
    mov [entbuf + DE_HAND], ax
    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    call srv_put
    jc  .lost
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_count / srv_send - the two walks. Both start from pathbuf.
; out: srv_count: CF=0 and AX = entries; CF=1 = DOS refused the folder
;      srv_send:  CF=1 = the link went away
; -----------------------------------------------------------------------------
srv_count:
    push bx
    push cx
    push dx
    xor bx, bx                  ; the running count
    call srv_first
    jc  .none
.next:
    call srv_keep               ; CF=0 = this is one we would show
    jc  .skip
    inc bx
.skip:
    call srv_more
    jnc .next
    mov ax, bx
    pop dx
    pop cx
    pop bx
    clc
    ret
.none:
    xor ax, ax                  ; AN EMPTY FOLDER IS NOT A REFUSAL: DOS
    pop dx                      ; answers findfirst with 'no more files' for
    pop cx                      ; one, and the master wants a count of zero
    pop bx                      ; rather than FERR_NOENT
    clc
    ret

srv_send:
    push ax
    push bx
    push cx
    push dx
    mov bx, [srv_n]             ; ...how many we PROMISED
    or  bx, bx
    jz  .done
    call srv_first
    jc  .pad
.next:
    call srv_keep
    jc  .skip
    call srv_ent                ; the DTA -> entbuf, registering a handle if
    call srv_put                ; it is a folder
    jc  .lost
    dec bx
    jz  .done
.skip:
    call srv_more
    jnc .next
.pad:
    call srv_blank              ; the folder shrank under us: the frame is
.padl:                          ; still owed its entries
    call srv_put
    jc  .lost
    dec bx
    jnz .padl
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

srv_put:                        ; entbuf -> the wire, DE_SZ bytes
    push cx
    push si
    mov si, entbuf
    mov cx, DE_SZ
.b:
    mov al, [si]
    call lp_sbyte
    jc  .lost
    inc si
    loop .b
    pop si
    pop cx
    clc
    ret
.lost:
    pop si
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; srv_first / srv_more - findfirst/findnext over pathbuf + '\*.*'
; out: CF=1 = no more
; -----------------------------------------------------------------------------
srv_first:
    push ax
    push cx
    push dx
    mov dx, dtabuf              ; our own DTA, because DOS's default one is in
    mov ah, 0x1A                ; the PSP at 0x80 - where the COMMAND TAIL is,
    int 0x21                    ; which parse_args has already read but which
                                ; is worth not scribbling on regardless
    call path_wild              ; pathbuf -> wildbuf
    mov dx, wildbuf
    mov cx, 0x10                ; ...directories as well as ordinary files
    mov ah, 0x4E
    int 0x21
    jc  .no
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

srv_more:
    push ax
    mov ah, 0x4F
    int 0x21
    jc  .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_keep - is the entry in the DTA one we show? CF=1 = skip it
;
; SPEC.md 19's species filter, on this side of the cable: '.' and '..' go
; because the kernel synthesizes the parent link itself (19.5) and two places
; deciding a listing's shape is how a display index stops meaning what the
; hit-test resolves; volume labels, hidden and system go because they do on
; every other volume os8088 mounts.
; -----------------------------------------------------------------------------
srv_keep:
    push ax
    mov al, [dtabuf + DTA_ATTR]
    test al, 0x08               ; volume label
    jnz .no
    test al, 0x06               ; hidden | system
    jnz .no
    cmp byte [dtabuf + DTA_NAME], '.'
    je  .no                     ; '.' and '..' both start with it, and nothing
    pop ax                      ; else DOS returns ever does
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_ent - the DTA's entry -> entbuf, as OSAPI_FS_ENT wants it
;
; A FOLDER GETS A HANDLE AND A FILE DOES NOT, which is phase 1's shape rather
; than an omission: the kernel reaches a file BY NAME through FSV_STAT, so a
; handle per file would spend the table on rows nothing asks about - and a
; directory of 300 files would exhaust it before the first folder in it.
; -----------------------------------------------------------------------------
srv_ent:
    push ax
    push bx
    push cx
    push si
    push di
    call srv_blank
    mov si, dtabuf + DTA_NAME   ; @0: the display name, already ASCIIZ 8.3
    mov di, entbuf
    mov cx, 13
.nm:
    mov al, [si]
    mov [di], al
    or  al, al
    jz  .named
    inc si
    inc di
    loop .nm
.named:
    mov ax, [dtabuf + DTA_SIZE] ; @20: the size dword, which fm_measure sums
    mov [entbuf + DE_SIZE], ax
    mov ax, [dtabuf + DTA_SIZE + 2]
    mov [entbuf + DE_SIZE + 2], ax

    test byte [dtabuf + DTA_ATTR], 0x10
    jnz .dir
    call ent_ispkg              ; @16: 1 = a package, so the loader's icon and
    jnc .file                   ; the double-click both do the right thing
    mov word [entbuf + DE_TYPE], 1
    jmp short .hand             ; ...AND A PACKAGE NEEDS @18 TOO. See below
.file:
    mov word [entbuf + DE_TYPE], 0
    jmp short .out
.dir:
    mov word [entbuf + DE_TYPE], 2
.hand:
    ; @18: OUR OPAQUE HANDLE, for a FOLDER because dsk_chdir dives on it, and
    ; for a PACKAGE because loader_run reads one BY HANDLE out of the staged
    ; entry (SPEC.md 19.1's first-cluster word, disk.inc's DVK_FILE branch) -
    ; it is the one caller that arrives holding a handle rather than a name.
    ; This branch used to be the folder's alone, so every package on the wire
    ; was listed with handle 0, open_handle refused it as the root, and a
    ; double-click answered `Disk error` on a link that was working perfectly.
    ;
    ; A PLAIN FILE deliberately still gets 0, and the asymmetry is the handle
    ; TABLE: nothing reads @18 behind type 0 (dskw_read STATs by name and gets
    ; a fresh one), hdtab holds 64, and a real DOS folder can hold hundreds -
    ; so a slot per file would exhaust it and break the FOLDERS that work
    ; today. The RAM disk hands one out for everything because it has no such
    ; table to spend.
    mov ax, [srv_h]             ; ...its parent is the folder being listed
    mov si, dtabuf + DTA_NAME
    call hd_get                 ; -> AX = the handle, 0 if the table is full
    mov [entbuf + DE_HAND], ax
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

srv_blank:                      ; entbuf to DE_SZ zeros
    push ax
    push cx
    push di
    mov di, entbuf
    mov cx, DE_SZ
    xor al, al
    cld
    push ds
    pop es
    rep stosb
    pop di
    pop cx
    pop ax
    ret

; ent_ispkg - does the DTA's name end in .O88? CF=1 = yes
ent_ispkg:
    push ax
    push si
    mov si, dtabuf + DTA_NAME
.dot:
    mov al, [si]
    or  al, al
    jz  .no
    inc si
    cmp al, '.'
    jne .dot
    mov al, [si]
    or  al, 0x20
    cmp al, 'o'
    jne .no
    cmp byte [si+1], '8'
    jne .no
    cmp byte [si+2], '8'
    jne .no
    cmp byte [si+3], 0
    jne .no
    pop si
    pop ax
    stc
    ret
.no:
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; srv_drives - the whole-machine root: one folder entry per live drive
;
; int 21h 36h is the only test that does not need a drive to be READY, and it
; still touches the media - which is why crit24 is installed: an empty floppy
; would otherwise stop this machine on DOS's own Abort/Retry/Fail with nobody
; standing at it.
; -----------------------------------------------------------------------------
srv_drives:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx                  ; the count
    mov cx, 26
    mov dl, 1
.count:
    call drive_live
    jc  .cnext
    inc bx
.cnext:
    inc dl
    loop .count

    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    mov ax, bx
    call lp_sword
    jc  .lost

    mov cx, 26
    mov dl, 1
.send:
    call drive_live
    jc  .snext
    call srv_blank
    mov al, dl
    add al, 'A' - 1
    mov [entbuf], al            ; @0: 'C:' - a NAME, and the kernel prints it
    mov byte [entbuf+1], ':'    ; as one. The drive LETTER os8088 shows is its
    mov byte [entbuf+2], 0      ; own volume index (SPEC.md 26.4) and has
                                ; nothing to do with this one
    mov word [entbuf + DE_TYPE], 2
    push dx
    xor ax, ax                  ; parent = the root
    mov si, entbuf
    call hd_get
    pop dx
    mov [entbuf + DE_HAND], ax
    call srv_put
    jc  .lost
.snext:
    inc dl
    loop .send
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; drive_live - DL = 1..26. CF=0 = it answered. DL and CX preserved.
drive_live:
    push ax
    push bx
    push cx
    push dx
    mov ah, 0x36
    int 0x21
    cmp ax, 0xFFFF
    je  .no
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_chdir - NF_CHDIR: a handle -> status and THE PARENT'S HANDLE
;
; It answers the parent because the kernel has no directory sector to read one
; out of (SPEC.md 62.9.1) - the '..' it synthesizes needs somewhere to go, and
; only this side knows where.
;
; It does not chdir anything. Every command carries its own handle, so there
; is no cwd on this side to move; what it records is the drive, for NF_DFREE,
; which is the one verb with no handle of its own.
; -----------------------------------------------------------------------------
srv_chdir:
    push ax
    push bx
    push si
    call lp_rword
    jc  .lost
    mov [srv_h], ax
    or  ax, ax                  ; THE ROOT IS ALWAYS VALID and has no path to
    jz  .root                   ; check - under /W it has none at all, and
                                ; hd_path refuses it (srv_list's note). It is
                                ; also the FIRST thing the kernel asks for:
                                ; disk_mount chdirs to 0 before it lists, so
                                ; getting this wrong is a volume that will not
                                ; mount rather than a folder that will not open
    call hd_path                ; the only validation there is: can we name it
    jc  .no
    call srv_drv_note           ; ...and the DRIVE it landed on, for NF_DFREE
    jmp short .say
.root:
    mov byte [srv_cwd], 0       ; ...the default drive, which is the one this
                                ; program was started on
.say:
    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    mov ax, [srv_h]
    call hd_par                 ; -> AX = the parent, 0 for the root
    call lp_sword
    jc  .lost
    jmp short .out
.no:
    mov al, FERR_NOENT
    call lp_sbyte
    jc  .lost
.out:
    pop si
    pop bx
    pop ax
    clc
    ret
.lost:
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_drv_note - pathbuf's drive -> [srv_cwd], for the one verb with no handle
;
; It reads the PATH rather than being handed a number, because only hd_path
; knows which drive a handle is on: under /W the first level IS a drive, and
; without it every path is on the root's. 0 means "the default drive", which
; is int 21h 36h's own convention and exactly right for the non-/W case.
;
; THE LINE THIS REPLACES WAS `mov [srv_cwd], ax` AND IT WAS WRONG TWICE. It
; stored a HANDLE in a field NF_DFREE reads as a DRIVE - and `srv_cwd` is a
; `db`, so a word-sized store from AX also wrote the byte after it, which is
; `nhd`'s low half. One chdir therefore zeroed the handle count, and every
; LIST after it was refused with `no such handle` on handles this program had
; just issued. NASM cannot warn: the width comes from the register, so
; `mov [byte_var], ax` assembles as cleanly as the byte store that was meant.
; -----------------------------------------------------------------------------
srv_drv_note:
    push ax
    mov byte [srv_cwd], 0
    cmp byte [pathbuf+1], ':'
    jne .out
    mov al, [pathbuf]
    or  al, 0x20
    sub al, 'a' - 1             ; 'a' -> 1, which is 36h's numbering
    mov [srv_cwd], al
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; srv_dfree - NF_DFREE: status, free BYTES as a dword, and the granule
;
; BYTES and not clusters, because that is what FSV_DFREE answers with
; (SPEC.md 62.9.1) - a redirected volume has no cluster size for the kernel to
; multiply by. free = avail_clusters * sectors_per_cluster * bytes_per_sector,
; which overflows a dword above 4GB and is clamped rather than wrapped: a
; volume reporting 3MB free because it has 4GB is worse than one reporting
; 4,294,967,295.
; -----------------------------------------------------------------------------
srv_dfree:
    push ax
    push bx
    push cx
    push dx
    mov dl, [srv_cwd]           ; the drive the last chdir stood on
    mov ah, 0x36
    int 0x21
    cmp ax, 0xFFFF
    je  .no
    ; AX = sectors/cluster, BX = free clusters, CX = bytes/sector
    mul cx                      ; DX:AX = bytes per cluster. A cluster is at
    or  dx, dx                  ; most 64KB on anything DOS 3.3 formats, so
    jnz .huge                   ; this cannot carry - but a hostile BPB can
    mul bx                      ; say anything, and it is one compare
    jmp short .say
.huge:
    mov ax, 0xFFFF              ; ...and a clamp is the honest answer
    mov dx, 0xFFFF
    jmp short .say2
.say:
    ; DX:AX = free bytes. `mul` set CF/OF if DX is non-zero, which is not an
    ; overflow here - it is simply a big disk.
.say2:
    push ax
    push dx
    mov al, NST_OK
    call lp_sbyte
    pop dx
    pop ax
    jc  .lost
    push ax
    push dx
    call lp_sword               ; the low half
    pop dx
    pop ax
    jc  .lost
    mov ax, dx
    call lp_sword               ; ...and the high
    jc  .lost
    mov ax, 512                 ; the granule. os8088 rounds a size UP to it
    call lp_sword               ; in the Disk window's status line, and 512 is
    jc  .lost                   ; what every volume it can mount uses
    jmp short .out
.no:
    mov al, FERR_NOENT
    call lp_sbyte
    jc  .lost
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_stat - NF_STAT: a folder handle and a name -> a handle for the FILE
; out: CF=1 = the link went away
;
; The name is a fixed 13 bytes on the wire (SPEC.md 62.10.1) and the folder
; comes with it rather than being remembered from the last chdir, so this verb
; carries everything it is about.
;
; A FILE GETS A HANDLE HERE and only here - srv_ent hands them out to folders
; alone, because a directory of 300 files would exhaust the table on rows
; nothing ever asks about. This is the ask, so this is where a file earns one.
; -----------------------------------------------------------------------------
srv_stat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword               ; which folder
    jc  .lost
    mov [srv_h], ax
    call srv_rname              ; ...and the name, into namebuf
    jc  .lost

    call hd_path                ; the folder, as a path
    jc  .no
    call path_join              ; + '\' + namebuf -> wildbuf
    jc  .no                     ; ...not a name any entry can have (the fence)
    mov al, 'S'                 ; a STAT: what a launch or an open begins with
    call say_file
    call stat_file              ; -> the DTA, or CF=1
    jc  .no

    mov ax, [srv_h]             ; the handle is (this folder, this name), so a
    mov si, namebuf             ; second stat of the same file answers the
    call hd_get                 ; same handle - hd_get dedupes
    mov [srv_fh], ax
    or  ax, ax                  ; ...0 IS THE TABLE FULL, and 0 is the ROOT -
    jz  .no                     ; open_handle refuses it, so answering OK with
                                ; it is a success the caller cannot use. A
                                ; refusal here at least names the file that
                                ; could not be reached

    xor al, al
    call lp_sbyte
    jc  .lost
    mov ax, [srv_fh]
    call lp_sword
    jc  .lost
    mov ax, [dtabuf + DTA_SIZE]
    call lp_sword
    jc  .lost
    mov ax, [dtabuf + DTA_SIZE + 2]
    call lp_sword
    jc  .lost
    mov al, [dtabuf + DTA_ATTR]
    call lp_sbyte
    jc  .lost
    jmp short .out
.no:
    mov al, FERR_NOENT
    call lp_sbyte
    jc  .lost
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; srv_rname - 13 bytes off the wire -> namebuf, NUL-terminated
srv_rname:
    push ax
    push cx
    push di
    mov cx, 13
    mov di, namebuf
.b:
    call lp_rbyte
    jc  .bad
    mov [di], al
    inc di
    loop .b
    mov byte [namebuf+13], 0    ; ...whatever the far end sent, this ends
    pop di
    pop cx
    pop ax
    clc
    ret
.bad:
    pop di
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; say_file - name a file this run touched, one line, verb-tagged
; in:  AL = a one-character tag, wildbuf = the path DOS is about to be given
; out: nothing (all registers preserved)
;
; ASKED FOR OFF THE FIELD: the two ends of this cable are usually in different
; rooms, and the DOS end sat silent from `Listening.` to `Master finished.` -
; so "did it even ask for that file" was a question only os8088 could answer,
; and os8088 is the side under suspicion when something goes wrong.
;
; It logs at the point the PATH IS RESOLVED rather than per verb, which is one
; site instead of nine and cannot drift: wildbuf is what every one of them
; hands to DOS. The tag says which verb without a second string per verb -
; R read, W write, A append, D delete, N rename, M mkdir, K rmdir, Y copy,
; S stat. A LIST is not logged: it is one line per FILE IN THE FOLDER, which
; buries the thing being looked for.
; -----------------------------------------------------------------------------
say_file:
    push ax
    push si
    mov [sf_tag], al
    mov si, sf_pre
    call puts
    mov si, wildbuf
    call puts
    call crlf
    pop si
    pop ax
    ret

; stat_file - wildbuf names a file: findfirst it into the DTA. CF=1 = no.
; It uses findfirst rather than open-and-seek because the SIZE and the
; ATTRIBUTE both come out of one call, and a folder must answer with its
; attribute rather than failing to open.
stat_file:
    push ax
    push cx
    push dx
    mov dx, dtabuf
    mov ah, 0x1A
    int 0x21
    mov dx, wildbuf
    mov cx, 0x10
    mov ah, 0x4E
    int 0x21
    jc  .no
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

; path_join - pathbuf + '\' + namebuf -> wildbuf
; path_join - pathbuf + '\' + namebuf -> wildbuf
; out: CF=0 wildbuf built; CF=1 the NAME IS NOT ONE ENTRY OF THAT FOLDER, and
;      wildbuf is left empty
;
; THIS IS THE FENCE. Every verb that takes a name off the wire joins it here
; (stat, every write verb through wr_arg, both ends of a copy, a rename's new
; name), and the served folder is only "the subtree" if a name can only ever
; be one of its children. Thirteen raw bytes cannot promise that: `..\DOS`
; fits, DOS resolves the `..`, and NF_RMTREE would walk C:\DOS. So a name is
; refused when it holds a separator or a drive colon (`\` `/` `:`), a
; wildcard or another character DOS itself will not put in a name (`*` `?`
; `<` `>` `|` `"`), when it begins with `.` (`.` and `..` are the two entries
; every folder has that are NOT its children), or when it is empty (which
; would name the FOLDER). Every caller answers a refusal with FERR_NOENT,
; which is true: no entry of that folder has that name. A real master never
; sends one - its names come out of FAT directory entries - so nothing legit
; is turned away.
path_join:
    push ax
    push si
    push di
    mov si, namebuf
    mov al, [si]
    or  al, al
    jz  .bad                    ; empty: it would name the folder itself
    cmp al, ' '
    je  .bad
    cmp al, '.'
    je  .bad                    ; `.` and `..`, and nothing legit starts so
.chk:
    lodsb
    or  al, al
    jz  .okname
    cmp al, '\'
    je  .bad
    cmp al, '/'
    je  .bad
    cmp al, ':'
    je  .bad
    cmp al, '*'
    je  .bad
    cmp al, '?'
    je  .bad
    cmp al, '<'
    je  .bad
    cmp al, '>'
    je  .bad
    cmp al, '|'
    je  .bad
    cmp al, '"'
    je  .bad
    jmp short .chk
.okname:
    mov si, pathbuf
    mov di, wildbuf
    call str_app
    cmp byte [di-1], '\'
    je  .sep
    mov byte [di], '\'
    inc di
.sep:
    mov si, namebuf
    call str_app
    mov byte [di], 0
    pop di
    pop si
    pop ax
    clc
    ret
.bad:
    mov byte [wildbuf], 0       ; nothing downstream may see a half-built path
    pop di
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_read - NF_READ: a handle and a capacity -> the file
; out: CF=1 = the link went away
;
; THE CAPACITY IS HONOURED HERE, which is what makes an oversized file cheap
; to refuse: the master's buffer is the limit, so a file bigger than it is cut
; at the SOURCE instead of crossing the wire to be discarded. At 3,741
; bytes/second a wasted 116KB module is half a minute.
; -----------------------------------------------------------------------------
srv_read:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword
    jc  .lost
    mov [srv_fh], ax
    call lp_rword               ; the capacity, low
    jc  .lost
    mov [srv_cap], ax
    call lp_rword               ; ...and high
    jc  .lost
    mov [srv_cap+2], ax

    call open_handle            ; the handle -> an open DOS file in [rfh]
    jc  .no
    mov ax, [dtabuf + DTA_SIZE] ; open_handle stats it on the way
    mov [srv_rlen], ax
    mov ax, [dtabuf + DTA_SIZE + 2]
    mov [srv_rlen+2], ax
    call clamp_len              ; ...to the capacity
    mov al, NST_OK
    call lp_sbyte
    jc  .rlost
    mov ax, [srv_rlen]
    call lp_sword
    jc  .rlost
    mov ax, [srv_rlen+2]
    call lp_sword
    jc  .rlost
    call send_body
    jc  .rlost
    call close_handle
    jmp short .out
.rlost:
    call close_handle
    jmp short .lost
.no:
    mov al, FERR_NOENT
    call lp_sbyte
    jc  .lost
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_readat - NF_READAT: a handle, a 32-bit offset and a word capacity
;
; PAST THE END IS A LENGTH OF ZERO AND NOT A REFUSAL, which is the contract
; (SPEC.md 62.9.1) and is what lets a caller walk a file to its end without
; knowing how long it is.
; -----------------------------------------------------------------------------
srv_readat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword
    jc  .lost
    mov [srv_fh], ax
    call lp_rword
    jc  .lost
    mov [srv_off], ax
    call lp_rword
    jc  .lost
    mov [srv_off+2], ax
    call lp_rword               ; the capacity, one word
    jc  .lost
    mov [srv_cap], ax
    mov word [srv_cap+2], 0

    call open_handle
    jc  .no
    mov bx, [rfh]               ; seek to the window's start
    mov ax, 0x4200
    mov cx, [srv_off+2]
    mov dx, [srv_off]
    int 0x21
    jc  .rzero
    ; What is LEFT of the file from there, clamped to the capacity. The size
    ; is DTA's; the offset may be past it, and `sub` would wrap.
    mov ax, [dtabuf + DTA_SIZE]
    mov dx, [dtabuf + DTA_SIZE + 2]
    sub ax, [srv_off]
    sbb dx, [srv_off+2]
    jc  .rzero                  ; the offset is past the end: zero bytes
    mov [srv_rlen], ax
    mov [srv_rlen+2], dx
    call clamp_len
    mov al, NST_OK
    call lp_sbyte
    jc  .rlost
    mov ax, [srv_rlen]
    call lp_sword               ; ONE word: a windowed read is capped at 64KB
    jc  .rlost                  ; by its own capacity
    call send_body
    jc  .rlost
    call close_handle
    jmp short .out
.rzero:
    mov word [srv_rlen], 0
    mov word [srv_rlen+2], 0
    mov al, NST_OK
    call lp_sbyte
    jc  .rlost
    xor ax, ax
    call lp_sword
    jc  .rlost
    call close_handle
    jmp short .out
.rlost:
    call close_handle
    jmp short .lost
.no:
    mov al, FERR_NOENT
    call lp_sbyte
    jc  .lost
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; open_handle - [srv_fh] -> an open DOS file in [rfh], stat in the DTA
; out: CF=1 = no such handle, or DOS would not open it
;
; It stats FIRST and opens second, because the size has to come from somewhere
; and a seek-to-end costs another two int 21h calls on a machine that may be
; an XT. The DTA is left holding the answer for the caller.
; -----------------------------------------------------------------------------
open_handle:
    push ax
    push bx
    push cx
    push dx
    mov word [rfh], 0xFFFF
    mov ax, [srv_fh]
    or  ax, ax
    jz  .no                     ; the ROOT is not a file
    mov [srv_h], ax
    call hd_path                ; ...the handle names the FILE itself here,
    jc  .no                     ; so its path is the whole thing
    mov si, pathbuf
    mov di, wildbuf
    call str_cpy16
    mov al, 'R'                 ; a READ: 62.10.5's per-file log
    call say_file
    call stat_file
    jc  .no
    test byte [dtabuf + DTA_ATTR], 0x10
    jnz .no                     ; a folder is not readable
    mov ax, 0x3D00              ; read-only: phase 2 writes nothing
    mov dx, wildbuf
    int 0x21
    jc  .no
    mov [rfh], ax
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

close_handle:
    push ax
    push bx
    mov bx, [rfh]
    cmp bx, 0xFFFF
    je  .out
    mov ah, 0x3E
    int 0x21
    mov word [rfh], 0xFFFF
.out:
    pop bx
    pop ax
    ret

; clamp_len - [srv_rlen] down to [srv_cap]
clamp_len:
    push ax
    mov ax, [srv_rlen+2]
    cmp ax, [srv_cap+2]
    ja  .cap
    jb  .out
    mov ax, [srv_rlen]
    cmp ax, [srv_cap]
    jbe .out
.cap:
    mov ax, [srv_cap]
    mov [srv_rlen], ax
    mov ax, [srv_cap+2]
    mov [srv_rlen+2], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; send_body - [srv_rlen] bytes of [rfh], through secbuf
; out: CF=1 = the link went away
;
; A SHORT DOS READ ENDS THE FILE AND NOT THE FRAME: the length is already on
; the wire, so the promised bytes are owed whatever the disk says. It pads
; with zeros rather than stopping, because a wire left short is the link dead
; where a short file is one operation wrong.
; -----------------------------------------------------------------------------
send_body:
    push ax
    push bx
    push cx
    push dx
    push si
.chunk:
    mov ax, [srv_rlen]          ; nothing left to send?
    or  ax, [srv_rlen+2]
    jz  .done
    mov cx, 512                 ; ...this pass: min(512, what is left)
    cmp word [srv_rlen+2], 0
    jne .full
    cmp [srv_rlen], cx
    jae .full
    mov cx, [srv_rlen]
.full:
    mov [srv_chunk], cx
    mov ah, 0x3F
    mov bx, [rfh]
    mov dx, secbuf
    int 0x21
    jc  .short
    cmp ax, [srv_chunk]
    je  .have
.short:
    call zero_buf               ; DOS gave us less than the file said it had:
                                ; the frame is owed its bytes regardless
.have:
    mov cx, [srv_chunk]
    mov si, secbuf
.b:
    mov al, [si]
    inc si
    call lp_sbyte
    jc  .lost
    loop .b
    mov ax, [srv_chunk]
    sub [srv_rlen], ax
    sbb word [srv_rlen+2], 0
    jmp short .chunk
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; str_cpy16 - SI -> DI, whole, up to 80 bytes
str_cpy16:
    push cx
    mov cx, 80
    call str_cpy
    pop cx
    ret

; =============================================================================
; WRITING (SPEC.md 62.10.4.5)
;
; Six verbs and one shape: a folder handle, a name, whatever the verb carries,
; and ONE STATUS BYTE back - a FERR_*, not an int 13h code, because that is
; what the kernel's callers already handle.
;
; /RO IS ENFORCED HERE AND NOWHERE ELSE, which is the point of putting the
; test in one routine every one of them calls first: a verb added later gets
; the refusal by construction rather than by its author remembering. The
; machine at this end may be somebody's real DOS box - docs/FIELD-MACHINES.md
; keeps a live DOS 3.3 install on the calibration machine's C: - so the switch
; has to mean it.
; =============================================================================

; wr_gate - may this run write at all? CF=1 = no, AL = the FERR_* to answer
;
; IT REPORTS AND DOES NOT TRANSMIT. wr_open calls it while the master is still
; SENDING the file body, so a gate that answered for itself would drive the
; cable while the other end was driving it: the two four-phase handshakes
; complete against each other's strobe, no byte crosses, the drain is one byte
; short for ever and the master's own status wait never ends. The status is the
; caller's to send, once the wire is quiet (SPEC.md 62.10.4.5)
wr_gate:
    cmp byte [roflag], 0
    je  .ok
    mov al, FERR_WPROT          ; the MEDIA answer, which is what a read-only
    stc                         ; run is from the other end's point of view
    ret
.ok:
    clc
    ret

; wr_arg - the folder handle and one name, off the wire into pathbuf+namebuf
; out: CF=1 = the link died; ZF=1 (via [srv_ok]) = the folder is unknown
wr_arg:
    call lp_rword
    jc  .lost
    mov [srv_h], ax
    call srv_rname
    jc  .lost
    mov byte [srv_ok], 1
    call hd_path                ; -> pathbuf
    jc  .nodir
    call path_join              ; ...+ '\' + namebuf -> wildbuf
    jc  .nodir                  ; ...not a name any entry can have (the fence):
                                ; the verb answers FERR_NOENT, which is true
    mov al, [sf_wtag]           ; ...whichever write verb we are inside
    call say_file
    clc
    ret
.nodir:
    mov byte [srv_ok], 0
    clc
    ret
.lost:
    stc
    ret

; wr_done - AL = a FERR_*, and the verb is over
wr_done:
    call lp_sbyte
    ret

; -----------------------------------------------------------------------------
; srv_copy - NF_COPY: source folder, DEST folder, name -> one status byte
;
; BOTH ENDS ARE HERE (SPEC.md 62.9.8), so this is an ordinary DOS file copy
; and the cable carries nothing but the request. os8088 asks for it only when
; the two folders are on the same redirected volume; a machine that answered
; it any other way would be copying between two places it does not both hold.
;
; /RO refuses it like every other write - wr_gate is the one place that is
; decided, and a copy creates a file.
;
; It reuses secbuf, which nothing else is holding at this point in the command
; loop, and reports the SAME FERR_ set as a write: the caller cannot tell a
; local copy from a stream-and-write except by how long it took.
; -----------------------------------------------------------------------------
srv_copy:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call lp_rword               ; the SOURCE folder
    jc  .lost
    mov [srv_h], ax
    call lp_rword               ; ...and the DESTINATION folder, banked before
    jc  .lost                   ; srv_rname reuses the wire
    mov [srv_dsth], ax
    call srv_rname
    jc  .lost

    call wr_gate                ; /RO, with every argument now off the wire
    jc  .gsay

    call hd_path                ; the SOURCE, as a path
    jc  .nodir
    call path_join              ; + '\' + namebuf -> wildbuf
    jc  .nodir                  ; the fence (path_join's header)
    mov al, 'Y'                 ; the SOURCE, before wildbuf is rebuilt
    call say_file
    mov si, wildbuf             ; ...banked whole: building the destination
    mov di, cpysrc              ; below rewrites pathbuf AND wildbuf
    call str_cpy16

    mov ax, [srv_dsth]          ; the DESTINATION, the same way
    mov [srv_h], ax
    call hd_path
    jc  .nodir
    call path_join
    jc  .nodir                  ; (the same name, so the same verdict)
    mov al, 'y'                 ; ...lower case: the same copy's other end
    call say_file

    mov dx, cpysrc              ; open the source
    mov ax, 0x3D00
    int 0x21
    jc  .nosrc
    mov [rfh], ax

    mov dx, wildbuf             ; create the destination
    mov ah, 0x3C
    xor cx, cx
    int 0x21
    jc  .nodst
    mov [cpydst], ax

.chunk:
    mov ah, 0x3F                ; read...
    mov bx, [rfh]
    mov cx, 512
    mov dx, secbuf
    int 0x21
    jc  .ioerr
    or  ax, ax
    jz  .done                   ; end of file
    mov cx, ax
    mov ah, 0x40                ; ...and write, the count DOS actually gave us
    mov bx, [cpydst]
    mov dx, secbuf
    int 0x21
    jc  .full
    cmp ax, cx                  ; a short write is a full disk, which DOS
    jne .full                   ; reports by count and not by CF
    jmp short .chunk
.done:
    call cpy_close
    mov al, FERR_OK
    call wr_done
    jmp short .out
.full:
    call cpy_close
    mov al, FERR_FULL
    call wr_done
    jmp short .out
.ioerr:
    call cpy_close
    mov al, FERR_IO
    call wr_done
    jmp short .out
.nodst:
    call cpy_close
    mov al, FERR_IO
    call wr_done
    jmp short .out
.nosrc:
    mov al, FERR_NOENT
    call wr_done
    jmp short .out
.gsay:
    mov al, FERR_WPROT          ; wr_gate's answer, sent by its caller
    call wr_done
    jmp short .out
.nodir:
    mov al, FERR_NOENT
    call wr_done
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; cpy_close - shut both handles, whichever of them got opened
cpy_close:
    push ax
    push bx
    call close_handle           ; the SOURCE, through [rfh]
    mov bx, [cpydst]
    cmp bx, 0xFFFF
    je  .out
    mov ah, 0x3E
    int 0x21
    mov word [cpydst], 0xFFFF
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; srv_write - NF_WRITE: create or replace, whole
;
; THE BYTES ARE CONSUMED WHATEVER HAPPENS TO THEM. The length is on the wire
; ahead of them, so a refusal - read-only, no such folder, a full disk - still
; has to take the run off the cable; the alternative is a wire with a file's
; worth of bytes on it and nobody listening, which is the link dead rather
; than one operation failed. So the gate is tested, the run is drained either
; way, and only then does the status go back.
; -----------------------------------------------------------------------------
srv_write:
    mov byte [sf_wtag], 'W'
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_arg
    jc  .lost
    call lp_rword               ; the length, low
    jc  .lost
    mov [srv_rlen], ax
    call lp_rword               ; ...and high
    jc  .lost
    mov [srv_rlen+2], ax
    call wr_open                ; 3Ch: create or truncate. CF=1 = refused
    jc  .sink
    mov byte [srv_wbad], 0
    call recv_body              ; ...straight into the file
    jc  .rlost
    call close_handle
    mov al, FERR_OK
    cmp byte [srv_wbad], 0
    je  .say
    mov al, FERR_FULL           ; DOS took fewer bytes than it was given
.say:
    call wr_done
    jmp short .out
.sink:
    mov [srv_fail], al          ; the reason, banked across the drain
    call sink_body
    jc  .lost
    mov al, [srv_fail]          ; ...and said ONLY NOW, with the whole run off
    call wr_done                ; the cable and the master listening for it
    jmp short .out
.rlost:
    call close_handle
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; srv_append - NF_APPEND: add to the end of an existing file
;
; The length is ONE WORD here (SPEC.md 62.10.1): this is the chunked half of
; the pair, so a copy streams through it a claim at a time.
; -----------------------------------------------------------------------------
srv_append:
    mov byte [sf_wtag], 'A'
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_arg
    jc  .lost
    call lp_rword
    jc  .lost
    mov [srv_rlen], ax
    mov word [srv_rlen+2], 0
    call wr_openap              ; open existing and seek to the end
    jc  .sink
    mov byte [srv_wbad], 0
    call recv_body              ; ...straight into the file
    jc  .rlost
    call close_handle
    mov al, FERR_OK
    cmp byte [srv_wbad], 0
    je  .say
    mov al, FERR_FULL           ; DOS took fewer bytes than it was given
.say:
    call wr_done
    jmp short .out
.sink:
    mov [srv_fail], al          ; the reason, banked across the drain
    call sink_body
    jc  .lost
    mov al, [srv_fail]          ; ...and said ONLY NOW, with the whole run off
    call wr_done                ; the cable and the master listening for it
    jmp short .out
.rlost:
    call close_handle
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; srv_delete / srv_mkdir / srv_rmdir - one name, one status
; -----------------------------------------------------------------------------
srv_delete:
    mov byte [srv_verb], 0
    mov byte [sf_wtag], 'D'
    jmp short srv_name1
srv_mkdir:
    mov byte [srv_verb], 1
    mov byte [sf_wtag], 'M'
    jmp short srv_name1
srv_rmdir:
    mov byte [srv_verb], 2
    mov byte [sf_wtag], 'K'
srv_name1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_arg
    jc  .lost
    call wr_gate
    jc  .gsay                   ; /RO, with the name already off the wire
    cmp byte [srv_ok], 0
    je  .nodir
    mov dx, wildbuf
    cmp byte [srv_verb], 1
    je  .mk
    cmp byte [srv_verb], 2
    je  .rm
    mov ah, 0x41                ; delete
    jmp short .go
.mk:
    mov ah, 0x39                ; mkdir
    jmp short .go
.rm:
    mov ah, 0x3A                ; rmdir
.go:
    int 0x21
    jc  .doserr
    mov al, FERR_OK
    call wr_done
    jmp short .out
.doserr:
    call dos_ferr               ; AX = DOS's code -> AL = a FERR_*
    call wr_done
    jmp short .out
.gsay:
    mov al, FERR_WPROT          ; wr_gate's answer, sent by its caller
    call wr_done
    jmp short .out
.nodir:
    mov al, FERR_NOENT
    call wr_done
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; srv_rmtree - NF_RMTREE: a folder AND EVERYTHING IN IT (SPEC.md 62.10.7)
; out: CF=1 = the link went away
;
; The most destructive verb this program serves, and the one the master cannot
; check for itself: os8088 has no way to see what is inside a folder on this
; machine without asking, so what a WHOLE VOLUME is and what is PROTECTED are
; questions only this side can answer.
;
; ONE THING IS REFUSED OUTRIGHT: a whole volume. Under /W the first level of
; the synthetic root IS a drive (62.10.2), so a name in the root resolves to
; `C:` and this would be a DOS machine's entire disk. Nothing else is
; special-cased, because the fence is drawn in ONE place: path_join refuses a
; name that is not one entry of its folder - a separator, a drive colon, a
; leading `.` - so WITHOUT /W the root is the served folder, a name can only
; ever be one of its children, and the root itself is unnameable. The first
; cut said "by construction rather than by a test", and it was not: thirteen
; raw bytes hold `..\DOS` comfortably, and DOS resolves the `..`. It is by
; test, and the test is path_join's, for every verb at once.
;
; Two more refusals are INHERITED rather than added. A read-only, hidden or
; system entry stops the walk with FERR_PROT, which is letter for letter what
; os8088's own FAT recursive delete does (dskw_dbody_n83's DSKW_PROT test) -
; this is somebody's real disk and a file they protected is not ours to guess
; about. And /RO refuses before anything is touched, through the wr_gate every
; other write verb goes through.
;
; A PARTIAL DELETE IS POSSIBLE AND IS NOT A BUG. The tree is emptied depth
; first, so a refusal part way leaves the deletions already made - which is the
; FAT path's own behaviour, and its own comment is "stop, and go home".
; -----------------------------------------------------------------------------
srv_rmtree:
    mov byte [sf_wtag], 'T'
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_arg                 ; the handle and the name, then pathbuf and
    jc  .lost                   ; wildbuf = the subtree's root
    call wr_gate
    jc  .gsay                   ; /RO, with every argument off the wire

    ; A WHOLE VOLUME IS NOT DELETABLE, and this test comes BEFORE the
    ; `srv_ok` one for the reason srv_list and srv_enum both give about the
    ; same handle: under /W, `hd_path` REFUSES the root on purpose - its first
    ; level IS a drive, so the root has no path of its own - and `wr_arg`
    ; therefore leaves srv_ok = 0. Tested the other way round the fence never
    ; fires at all: the drive survives, because DOS will not rmdir `C:`
    ; either, but the answer is `no such folder` for the most important
    ; refusal this program makes. A refusal that reports the wrong REASON is
    ; a refusal nobody can act on, and it is one edit away from not being a
    ; refusal at all.
    cmp byte [wholemc], 0
    je  .fenced
    cmp word [srv_h], 0
    jne .fenced
    mov al, FERR_PROT
    call wr_done
    jmp .out

.fenced:
    cmp byte [srv_ok], 0
    je  .nodir                  ; a handle we never issued

    mov si, wildbuf             ; the subtree's root, banked as the working
    call rt_fits                ; path: every pass below rebuilds wildbuf.
    jc  .toodeep                ; MEASURED FIRST - str_cpy96 would TRUNCATE,
    mov si, wildbuf             ; and a truncated path names a different folder
    mov di, rtpath
    call str_cpy96
    mov word [rtdep], 0

    ; IS IT A FOLDER AT ALL? A file here is dskw_delete's job and answering OK
    ; would delete it, which is a verb doing another verb's work.
    call rt_stat
    jc  .noent
    test byte [dtabuf + DTA_ATTR], 0x10
    jz  .isfile

.pass:
    ; --- the first entry we can see in rtpath, if there is one --------------
    call rt_first               ; CF=1 = the folder is empty
    jc  .empty
    mov al, [dtabuf + DTA_ATTR]
    test al, 0x07               ; read-only | hidden | system: stop, and say so
    jnz .prot
    test al, 0x10
    jnz .down

    call rt_join                ; rtpath + '\' + the DTA's name -> wildbuf
    jc  .toodeep
    mov al, 'T'
    call say_file
    mov dx, wildbuf
    mov ah, 0x41                ; delete the file where it stands
    int 0x21
    jc  .doserr
    jmp short .pass

.down:
    call rt_push                ; ...into it: rtpath += '\' + the DTA's name
    jc  .toodeep
    jmp short .pass

.empty:
    push si                     ; ...and SAY SO, lower case, which is the same
    push di                     ; convention the copy's two ends use ('Y'/'y').
    mov si, rtpath              ; This is the most destructive verb here and
    mov di, wildbuf             ; the two machines are usually in different
    call str_cpy96              ; rooms (62.10.5): the log is the only record
    pop di                      ; of what went, and one that named the files
    pop si                      ; and not the folders would be half of it
    mov al, 't'
    call say_file
    mov dx, rtpath              ; nothing left in it, so it can go
    mov ah, 0x3A
    int 0x21
    jc  .doserr
    cmp word [rtdep], 0
    je  .done                   ; that was the folder we were asked about
    call rt_pop                 ; ...otherwise climb, and carry on emptying
    jmp short .pass

.done:
    mov al, FERR_OK
    call wr_done
    jmp short .out
.doserr:
    call dos_ferr               ; AX = DOS's code -> AL = a FERR_*
    call wr_done
    jmp short .out
.prot:
    mov al, FERR_PROT
    call wr_done
    jmp short .out
.isfile:
    mov al, FERR_PROT           ; a FILE: dskw_delete's job, not this one
    call wr_done
    jmp short .out
.toodeep:
    mov al, FERR_DIRFULL        ; the path will not fit, so we cannot name the
    call wr_done                ; folder we would be deleting
    jmp short .out
.noent:
    mov al, FERR_NOENT
    call wr_done
    jmp short .out
.gsay:
    mov al, FERR_WPROT
    call wr_done
    jmp short .out
.nodir:
    mov al, FERR_NOENT
    call wr_done
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rt_first - the first entry of rtpath that is not '.' or '..'
; out: CF=0 and the DTA holds it; CF=1 = there is nothing left
;
; THERE IS NO DTA STACK, and that is the design rather than a shortcut. DOS
; keeps findfirst/findnext state IN THE DTA, so a recursive walk wants one per
; level - 43 bytes each, at a depth nobody can bound in advance. The kernel's
; own FAT recursive delete solved this years ago and this is that algorithm:
; delete the first thing you can see, descend into a folder, and remove a
; folder once it is empty. Every pass re-runs findfirst from scratch, so the
; only state is a path string and a counter.
;
; It asks for 0x16 - directories, hidden AND system - because srv_rmtree has
; to SEE a protected entry to refuse it. srv_keep would make one invisible,
; and the folder would then simply never empty.
; -----------------------------------------------------------------------------
rt_first:
    push ax
    push cx
    push dx
    call rt_wild                ; rtpath + '\*.*' -> wildbuf
    mov dx, wildbuf
    call rt_ff16
    jc  .none
.test:
    cmp byte [dtabuf + DTA_NAME], '.'
    jne .have                   ; '.' and '..' are the only names DOS returns
    mov ah, 0x4F                ; that start with one
    int 0x21
    jnc .test
.none:
    pop dx
    pop cx
    pop ax
    stc
    ret
.have:
    pop dx
    pop cx
    pop ax
    clc
    ret

; rt_stat  - findfirst on rtpath ITSELF, so its attribute can be read
; srv_isdir - the same for pathbuf, and it also checks the directory bit
; out: CF=1 = no such thing (srv_isdir: ...or it is not a folder)
;
; Both leave the answer in the DTA. Neither is on a hot path: rt_stat runs
; once per NF_RMTREE and srv_isdir once per NF_ENUM walk, at its end.
rt_stat:
    push dx
    mov dx, rtpath
    call rt_ff16
    pop dx
    ret

srv_isdir:
    push ax
    push dx
    mov dx, pathbuf
    call rt_ff16
    jc  .no
    test byte [dtabuf + DTA_ATTR], 0x10
    jz  .no
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; rt_ff16 - findfirst DS:DX with directories, hidden and system included
rt_ff16:
    push ax
    push cx
    push dx
    mov dx, dtabuf              ; our own DTA, never DOS's at PSP:0080
    mov ah, 0x1A
    int 0x21
    pop dx
    push dx
    mov cx, 0x16
    mov ah, 0x4E
    int 0x21
    pop dx
    pop cx
    pop ax
    ret

; rt_wild  - rtpath + '\*.*'          -> wildbuf
; rt_join  - rtpath + '\' + DTA name  -> wildbuf. CF=1 = it will not fit
rt_wild:
    push si
    mov si, s_wild
    call rt_build
    pop si
    ret

rt_join:
    push si
    mov si, dtabuf + DTA_NAME
    call rt_build
    pop si
    ret

; rt_build - rtpath + '\' + the string at SI -> wildbuf. CF=1 = too long.
; The overflow is a REFUSAL and never a truncation: a truncated path names a
; DIFFERENT folder, and this verb deletes what it names.
;
; wildbuf is RT_BUFSZ and the longest thing that can land in it is
; RT_PATHMAX + '\' + a 12-character 8.3 name + NUL, which is what sizes it -
; so the WRITE can never run off the end and the test below is about whether
; the RESULT is a path we are willing to keep walking, not about the buffer.
rt_build:
    push ax
    push cx
    push di
    mov di, wildbuf
    mov cx, si                  ; ...banked: str_app walks SI
    mov si, rtpath
    call str_app
    cmp byte [di-1], '\'
    je  .sep
    mov byte [di], '\'
    inc di
.sep:
    mov si, cx
    call str_app
    mov byte [di], 0
    sub di, wildbuf
    cmp di, RT_PATHMAX
    ja  .long
    pop di
    pop cx
    pop ax
    clc
    ret
.long:
    pop di
    pop cx
    pop ax
    stc
    ret

; rt_fits - is the ASCIIZ string at SI short enough to be an rtpath?
; out: CF=1 = no. SI preserved.
rt_fits:
    push ax
    push si
    xor ax, ax
.l:
    cmp byte [si], 0
    je  .end
    inc si
    inc ax
    cmp ax, RT_PATHMAX
    jbe .l
    pop si
    pop ax
    stc
    ret
.end:
    pop si
    pop ax
    clc
    ret

; rt_push - descend: rtpath += '\' + the DTA's name. CF=1 = too deep to name.
rt_push:
    push ax
    push si
    push di
    call rt_join                ; the child's full path, length-checked there
    jc  .no
    cmp word [rtdep], RT_DEPTH
    jae .no
    mov si, wildbuf
    mov di, rtpath
    call str_cpy96
    inc word [rtdep]
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret

; rt_pop - climb: cut rtpath at its last '\'. Only ever called with rtdep > 0,
; so there is always one to cut at and no root can be reached by accident.
rt_pop:
    push ax
    push si
    push di
    mov si, rtpath
    xor di, di                  ; the last '\' seen, as an offset+1
.scan:
    mov al, [si]
    or  al, al
    jz  .cut
    cmp al, '\'
    jne .nx
    mov di, si
.nx:
    inc si
    jmp short .scan
.cut:
    or  di, di
    jz  .out                    ; no separator at all: leave it alone rather
    mov byte [di], 0            ; than empty it
.out:
    dec word [rtdep]
    pop di
    pop si
    pop ax
    ret

str_cpy96:                      ; SI -> DI, at most RT_PATHMAX+1 bytes
    push cx
    mov cx, RT_PATHMAX+1
    call str_cpy
    pop cx
    ret

; -----------------------------------------------------------------------------
; srv_rename - NF_RENAME: two 13-byte names and no length between them
; -----------------------------------------------------------------------------
srv_rename:
    mov byte [sf_wtag], 'N'
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_arg                 ; the folder and the OLD name -> wildbuf
    jc  .lost
    mov si, wildbuf             ; ...banked, because the second name reuses it
    mov di, oldbuf
    call str_cpy16
    call srv_rname              ; the NEW name -> namebuf
    jc  .lost
    call wr_gate
    jc  .gsay                   ; /RO, with BOTH names already off the wire
    cmp byte [srv_ok], 0
    je  .nodir
    call path_join              ; the new name, in the same folder
    jc  .nodir                  ; ...and the fence applies to it too: a rename
                                ; TO `..\X` moves the file out of the subtree
    mov dx, oldbuf
    mov di, wildbuf
    push ds
    pop es                      ; int 21h 56h takes ES:DI for the new name
    mov ah, 0x56
    int 0x21
    jc  .doserr
    mov al, FERR_OK
    call wr_done
    jmp short .out
.doserr:
    call dos_ferr
    call wr_done
    jmp short .out
.gsay:
    mov al, FERR_WPROT          ; wr_gate's answer, sent by its caller
    call wr_done
    jmp short .out
.nodir:
    mov al, FERR_NOENT
    call wr_done
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wr_open / wr_openap - the file a write is going into
; out: CF=0 and [rfh] open; CF=1 and AL = the FERR_* to answer with
; -----------------------------------------------------------------------------
wr_open:
    mov word [rfh], 0xFFFF
    call wr_gate
    jc  .gated
    cmp byte [srv_ok], 0
    je  .nodir
    push cx
    push dx
    mov ah, 0x3C                ; create/truncate, ordinary attributes
    xor cx, cx
    mov dx, wildbuf
    int 0x21
    pop dx
    pop cx
    jc  .doserr
    mov [rfh], ax
    clc
    ret
.doserr:
    call dos_ferr
    stc
    ret
.nodir:
    mov al, FERR_NOENT
    stc
    ret
.gated:
    stc                         ; AL is wr_gate's FERR_WPROT: the caller sends
    ret                         ; it once the whole run is off the wire

wr_openap:
    mov word [rfh], 0xFFFF
    call wr_gate
    jc  wr_open.gated
    cmp byte [srv_ok], 0
    je  wr_open.nodir
    push bx
    push cx
    push dx
    mov ax, 0x3D01              ; write-only, and it must already exist
    mov dx, wildbuf
    int 0x21
    jc  .no
    mov [rfh], ax
    mov bx, ax
    mov ax, 0x4202              ; ...to the end
    xor cx, cx
    xor dx, dx
    int 0x21
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    call dos_ferr
    stc
    ret

; -----------------------------------------------------------------------------
; recv_body - [srv_rlen] bytes off the wire into [rfh], through secbuf
; out: CF=1 = the link went away
; -----------------------------------------------------------------------------
recv_body:
    push ax
    push bx
    push cx
    push dx
    push di
.chunk:
    mov ax, [srv_rlen]
    or  ax, [srv_rlen+2]
    jz  .done
    mov cx, 512
    cmp word [srv_rlen+2], 0
    jne .full
    cmp [srv_rlen], cx
    jae .full
    mov cx, [srv_rlen]
.full:
    mov [srv_chunk], cx
    mov di, secbuf
.b:
    call lp_rbyte
    jc  .lost
    mov [di], al
    inc di
    loop .b
    mov ah, 0x40                ; ...and out to the file
    mov bx, [rfh]
    mov cx, [srv_chunk]
    mov dx, secbuf
    int 0x21
    jc  .wfail
    cmp ax, [srv_chunk]         ; A SHORT WRITE IS A FULL DISK and DOS reports
    je  .wok                    ; it that way rather than with CF - which is
.wfail:                         ; why this is a compare and not a `jc` alone.
    mov byte [srv_wbad], 1      ; The run is STILL OWED off the wire, so the
.wok:                           ; verdict is banked and the loop carries on
    mov ax, [srv_chunk]
    sub [srv_rlen], ax
    sbb word [srv_rlen+2], 0
    jmp short .chunk
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.lost:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; sink_body - swallow [srv_rlen] bytes and store none of them
sink_body:
    push ax
    push cx
    push dx
    mov cx, [srv_rlen]
    mov dx, [srv_rlen+2]
.b:
    mov ax, cx
    or  ax, dx
    jz  .out
    call lp_rbyte
    jc  .lost
    sub cx, 1
    sbb dx, 0
    jmp short .b
.out:
    pop dx
    pop cx
    pop ax
    clc
    ret
.lost:
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_ferr - AX = the code int 21h returned -> AL = a FERR_*
;
; Only the ones a write can actually produce; anything else is FERR_IO, which
; is the honest answer for "DOS said no and this end does not know why".
; -----------------------------------------------------------------------------
dos_ferr:
    cmp ax, 2                   ; file not found
    je  .noent
    cmp ax, 3                   ; path not found
    je  .noent
    cmp ax, 5                   ; access denied - a read-only file, or a
    je  .prot                   ; directory that is not empty
    cmp ax, 80                  ; already exists (mkdir/rename)
    je  .exist
    cmp ax, 4                   ; too many open files
    je  .io
    mov al, FERR_FULL           ; 39 is 'insufficient disk space', and the
    cmp ax, 39                  ; rest of DOS's write failures are near
    je  .out                    ; enough the same thing from here
.io:
    mov al, FERR_IO
    jmp short .out
.noent:
    mov al, FERR_NOENT
    jmp short .out
.prot:
    mov al, FERR_PROT
    jmp short .out
.exist:
    mov al, FERR_EXIST
.out:
    ret

; =============================================================================
; HANDLES
; =============================================================================

; -----------------------------------------------------------------------------
; hd_get - the handle for (AX = parent, SI = name), making one if it is new
; out: AX = the handle, or 0 when the table is full
;
; 0 IS A LEGITIMATE ANSWER for a full table and not an error code: the kernel
; would chdir into the root instead of into the folder clicked, which is
; wrong-but-harmless, where a refusal would need a status this verb's frame
; has no room for. It takes 64 folders in one session to reach.
; -----------------------------------------------------------------------------
hd_get:
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, ax                  ; the parent, banked
    xor cx, cx
    mov di, hdtab
.scan:
    cmp cx, [nhd]
    jae .new
    cmp [di + HD_PAR], dx
    jne .nx
    push cx
    push si
    push di
    add di, HD_NAME
    call str_eq                 ; SI vs DI
    pop di
    pop si
    pop cx
    jc  .hit
.nx:
    inc cx
    add di, HD_SZ
    jmp short .scan
.hit:
    mov ax, cx
    inc ax                      ; slot i is handle i+1
    jmp short .out
.new:
    cmp word [nhd], HD_MAX
    jae .full
    mov [di + HD_PAR], dx
    push di
    add di, HD_NAME
    mov cx, 13
    call str_cpy                ; SI -> DI, at most CX
    pop di
    mov ax, [nhd]
    inc word [nhd]
    inc ax
    jmp short .out
.full:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; hd_par - AX = a handle -> AX = its parent (0 for the root or a bad handle)
hd_par:
    push bx
    or  ax, ax
    jz  .root
    cmp ax, [nhd]
    ja  .root
    mov bx, ax
    dec bx
    call hd_slot                ; BX -> the row
    mov ax, [bx + HD_PAR]
    pop bx
    ret
.root:
    xor ax, ax
    pop bx
    ret

; hd_slot - BX = a slot index -> BX = its row. BX * HD_SZ, no multiply.
hd_slot:
    push cx
    mov cl, 4                   ; HD_SZ is 16 BY CHOICE, so this is a shift -
    shl bx, cl                  ; the machine serving this may be an XT and
    add bx, hdtab               ; `mul` there is 118 clocks
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_path - AX(=[srv_h]) -> pathbuf, the DOS path of that handle
; out: CF=0 and pathbuf is a NUL-terminated path with NO trailing backslash
;      (unless it is a drive root); CF=1 = a handle we never issued
;
; It walks UP collecting slots onto the STACK and then builds downward, which
; is what a table of names costs and is cheaper than it looks: the depth is
; bounded at HD_DEPTH, so the stack use is bounded too, and DOS's own path
; limit is about the same.
; -----------------------------------------------------------------------------
hd_path:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [srv_h]
    xor cx, cx                  ; how many we pushed
.up:
    or  ax, ax
    jz  .base
    cmp ax, [nhd]
    ja  .bad
    cmp cx, HD_DEPTH
    jae .bad
    push ax
    inc cx
    call hd_par
    jmp short .up

.base:
    ; The bottom of the path. Whole-machine mode has no path of its own - its
    ; first level IS a drive - so the first name popped supplies the drive and
    ; the root itself is unnameable, which is why srv_list answers it with the
    ; drive list rather than with a findfirst.
    mov di, pathbuf
    cmp byte [wholemc], 0
    jne .wroot
    mov si, rootpath
    call str_app                ; DI advances past what it wrote
    jmp short .down
.wroot:
    jcxz .bad                   ; the whole-machine ROOT: nothing to walk
    pop ax                      ; the drive slot, popped out of order on
    dec cx                      ; purpose - it is the one that has no '\'
    push cx                     ; in front of it
    mov bx, ax
    dec bx
    call hd_slot
    mov si, bx
    add si, HD_NAME
    call str_app                ; 'C:'
    mov byte [di], '\'          ; ...and its root
    inc di
    pop cx

.down:
    jcxz .done
.step:
    pop ax
    mov bx, ax
    dec bx
    call hd_slot
    cmp byte [di-1], '\'        ; a drive root already ends in one; anything
    je  .nosep                  ; else needs a separator
    mov byte [di], '\'
    inc di
.nosep:
    mov si, bx
    add si, HD_NAME
    call str_app
    loop .step
.done:
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    jcxz .baddone               ; ...and unwind whatever is still on the stack,
.badpop:                        ; or every bad handle leaks six words of it
    pop ax
    loop .badpop
.baddone:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; path_wild - pathbuf + '\*.*' -> wildbuf
; -----------------------------------------------------------------------------
path_wild:
    push ax
    push si
    push di
    mov si, pathbuf
    mov di, wildbuf
    call str_app
    cmp byte [di-1], '\'
    je  .sep
    mov byte [di], '\'
    inc di
.sep:
    mov si, s_wild
    call str_app
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; --- three string routines, because DOS gives none ---------------------------
; str_app: SI -> DI up to but not including the NUL; DI ends past the last
;          byte written and NOTHING is terminated - the caller does that once,
;          which is what lets these be chained.
str_app:
    push ax
.l:
    mov al, [si]
    or  al, al
    jz  .out
    mov [di], al
    inc si
    inc di
    jmp short .l
.out:
    pop ax
    ret

; str_cpy: SI -> DI, at most CX bytes INCLUDING the NUL
str_cpy:
    push ax
    push cx
.l:
    jcxz .out
    mov al, [si]
    mov [di], al
    or  al, al
    jz  .out
    inc si
    inc di
    dec cx
    jmp short .l
.out:
    pop cx
    pop ax
    ret

; str_eq: SI vs DI, both NUL-terminated. CF=1 = equal.
str_eq:
    push ax
.l:
    mov al, [si]
    cmp al, [di]
    jne .no
    or  al, al
    jz  .yes
    inc si
    inc di
    jmp short .l
.yes:
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; root_here - the served root, when it is the current directory
;
; int 21h 47h answers the path WITHOUT the drive and WITHOUT a leading
; backslash ('WORK\DOCS'), which is DOS's own oddity and not a mistake here -
; both have to be put back.
; -----------------------------------------------------------------------------
root_here:
    push ax
    push bx
    push dx
    push si
    push di
    mov ah, 0x19                ; current drive, 0 = A:
    int 0x21
    add al, 'A'
    mov [rootpath], al
    mov byte [rootpath+1], ':'
    mov byte [rootpath+2], '\'
    mov ah, 0x47
    xor dl, dl                  ; the current drive
    mov si, rootpath+3
    int 0x21
    mov byte [rootpath+3+64], 0 ; DOS writes at most 64 and a NUL; belt and
                                ; braces, because everything below indexes off
                                ; this string
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; crit24 - int 24h, DOS's critical error, answered FAIL
;
; Nobody is standing at this machine: it is a transfer station with a cable in
; it, and the master is inside a bounded wait. DOS's own Abort/Retry/Fail
; prompt would stop the program dead with the other end reporting a timeout,
; which is the one failure that looks like a broken cable and is not. An empty
; floppy drive in the /W drive scan is the ordinary way to reach it.
;
; AL = 3 is FAIL, which DOS 3.0+ turns into an ordinary carry-flag error at
; the int 21h call - exactly what every caller here already tests.
; -----------------------------------------------------------------------------
crit24:
    mov al, 3
    iret

; -----------------------------------------------------------------------------
; pin_or_scan - CF=1 if there is nothing to listen on
; -----------------------------------------------------------------------------
pin_or_scan:
    push ax
    push bx
    push cx
    push di
    mov cl, [ncand]
    or  cl, cl
    jz  .none
    xor ch, ch
    xor di, di
.l:
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .nx
    cmp byte [pinned], 0
    je  .yes
    shl bx, 1
    mov ax, [cand_base+bx]
    cmp ax, [pinbase]
    je  .yes
.nx:
    inc di
    loop .l
.none:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; report_ports - the survey, so a machine that will not link says why
; -----------------------------------------------------------------------------
report_ports:
    push ax
    push bx
    push cx
    push di
    mov si, s_phead
    call puts
    mov cl, [ncand]
    xor ch, ch
    jcxz .out
    xor di, di
.row:
    push cx
    mov si, s_ind
    call puts
    mov bx, di
    shl bx, 1
    mov ax, [cand_base+bx]
    call puthex16
    mov si, s_gap
    call puts
    mov bx, di
    mov si, s_dash
    cmp byte [cand_ok+bx], 0
    je  .say
    mov si, s_ok
.say:
    call puts
    call crlf
    inc di
    pop cx
    loop .row
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zero_buf - secbuf to zeros, for a sector we are refusing
;
; The frame is 512 bytes whether or not the read worked, so the master stays
; in step; ZEROS rather than the previous sector's contents is what stops a
; refusal looking like data to anything that ignores the status.
; -----------------------------------------------------------------------------
zero_buf:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, secbuf
    mov cx, 512
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

tick_dot:                       ; one dot per RUN, so a long transfer looks
    push ax                     ; alive rather than hung
    mov al, '.'
    call putc
    pop ax
    ret

; =============================================================================
; lplink.inc / lplslv.inc's two requirements, and the console
; =============================================================================
lpl_ticks:
    push es
    push bx
    mov bx, 0x0040
    mov es, bx
    mov ax, [es:0x006C]
    pop bx
    pop es
    ret

lpl_kbhit:
    mov ah, 1
    int 0x16
    jz  .none
    xor ax, ax
    int 0x16
    stc
    ret
.none:
    xor ax, ax
    clc
    ret

putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

puts:
    push ax
    push si
.l:
    lodsb
    or  al, al
    jz  .out
    call putc
    jmp short .l
.out:
    pop si
    pop ax
    ret

crlf:
    push ax
    mov al, 13
    call putc
    mov al, 10
    call putc
    pop ax
    ret

puthex16:
    push ax
    push cx
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    call putc
    pop ax
    loop .d
    pop cx
    pop ax
    ret

putdec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or  ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    call putc
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; DATA
; =============================================================================
s_banner:
    db 13,10,'OS88NET - the DOS end of an os8088 parallel file link',13,10
    db 'ESC stops.  OS88NET /? for options.',13,10,13,10,0
s_serving:  db 'Serving: ',0
s_size:     db '  ',0
s_secs:     db ' sectors',0
s_roflag:   db ', READ ONLY',0
s_capped:   db '  (over 32MB - serving the first 65535 sectors)',13,10,0
s_tiny:     db '  that file is under one sector',13,10,0
s_noopen:   db '  cannot open it',13,10,0
s_phead:    db 13,10,'  port  latch',13,10,0
s_ind:      db '  ',0
s_gap:      db '   ',0
s_ok:       db '  ok',0
s_dash:     db '  --',0
s_noport:   db 13,10,'No parallel port to listen on.',13,10,0
s_ready:    db 13,10,'Listening. Start the os8088 end (Control Panel,'
            db ' os88net, Connect).',13,10,0
s_called:   db 13,10,'Called on ',0
s_bye:      db 13,10,'Master finished.',13,10,0
s_stopped:  db 13,10,'Stopped.',13,10,0

s_wild:     db '*.*',0
s_nodir:    db 13,10,'No such folder.',13,10,0
dirarg:     times 80 db 0       ; the bare argument: the folder to serve
s_root:     db 'Serving files from ',0
s_wmc:      db 'Serving files from EVERY DRIVE on this machine.',13,10,0
s_ronly:    db 'Writes REFUSED (/RO).',13,10,0
s_rwok:     db 'Writes allowed.',13,10,0
s_help:
    db 'OS88NET [folder] [/W] [/RO] [/N] [/P:base] [/I:image]',13,10,13,10
    db '  folder    serve this folder and everything under it.',13,10
    db '            Default: the current directory.',13,10
    db '  /W        serve EVERY drive on this machine: the root of the',13,10
    db '            link then lists drives rather than files.',13,10
    db '  /RO       refuse every write. Read-only, whatever os8088 asks.',13,10
    db '  /P:base   pin the parallel port, in hex - /P:378, /P:278,',13,10
    db '            /P:3BC. Default: scan and use the first that answers.',13,10
    db '  /I:image  block mode: serve 512-byte sectors out of an image',13,10
    db '            file instead of serving files.',13,10
    db '  /N       serve SOCKETS as well as files: os8088 packages then',13,10
    db '            reach the network through this machine. Needs a packet',13,10
    db '            driver loaded (the one mTCP uses) and a DHCP server.',13,10
    db '            IT TAKES THE NETWORK for the run - do not start mTCP',13,10
    db '            programs alongside it.',13,10
    db '  /?        this.',13,10,13,10
    db 'ESC stops a run. os8088 connects from Control Panel, os88net.',13,10,0
sf_wtag:    db 'W'              ; the tag wr_arg's log line carries. A byte of
                                ; its own and NOT srv_verb, which already
                                ; exists three hundred lines down as a 0/1/2
                                ; selector for delete/mkdir/rmdir
sf_pre:     db '  ['
sf_tag:     db '?', '] ',0
s_portpin:  db 'Port pinned by /P: to ',0
s_portscan: db 'Port chosen by scanning (see the table below).',0
s_imgsay:   db 'Block-mode image (/I:): ',0
helpflag:   db 0
netflag:    db 0                ; /N was asked for (net_start)

imgname:    times 80 db 0       ; ...EMPTY by default now: the bare argument
                                ; is the FOLDER to serve, and block mode is
                                ; behind /I: (see parse_args)
fh:         dw 0
nsecs:      dw 0
roflag:     db 0
s_nwtry:    db 'Sockets (/N): looking for a packet driver...',13,10,0
s_nwok:     db '  up, address ',0
s_nwnopkt:  db '  no packet driver in 60h..80h, or it is not Ethernet',13,10,0
s_nwnodhcp: db '  a packet driver, but DHCP never answered',13,10,0
s_nwfiles:  db '  serving FILES only - os8088 will say No partner',13,10,0
s_crlf:     db 13,10,0
pinned:     db 0
pinbase:    dw 0
nrun:       db 0                ; sectors left in the run being served
nrun2:      db 0                ; ...and how many it started with
rstat:      db 0                ; this sector's verdict
wstat:      times 256 db 0      ; a write run's verdicts. 256 and not NET_RUN,
                                ; because the count crosses as ONE BYTE and
                                ; that is the protocol's own ceiling: sized to
                                ; net.asm's constant instead, the two programs
                                ; would be coupled across a cable by a number
                                ; neither can see, and raising it at one end
                                ; would overrun this at the other
secbuf:     times 512 db 0

; --- file mode's (SPEC.md 62.10.3) -------------------------------------------
wholemc:    db 0                ; /W: the root lists the DRIVES
srv_h:      dw 0                ; the handle the command in hand is about
srv_n:      dw 0                ; ...and how many entries we promised it
srv_ord:    dw 0                ; NF_ENUM's ordinal, across hd_path (62.10.6)
srv_cwd:    db 0                ; the DRIVE the last NF_CHDIR stood on, for
                                ; NF_DFREE - which is the one verb with no
                                ; handle to work it out from. 0 = the default
                                ; drive, which is int 21h 36h's own convention
srv_dsth:   dw 0                ; NF_COPY's DESTINATION folder (62.9.8)
cpydst:     dw 0xFFFF           ; ...and its open handle, 0xFFFF = none
cpysrc:     times 96 db 0       ; the source path, banked whole: building the
                                ; destination rewrites pathbuf AND wildbuf
nhd:        dw 0                ; handles issued this session
hdtab:      times HD_MAX*HD_SZ db 0
rootpath:   times 72 db 0       ; 'C:\' + DOS's 64 + a NUL
pathbuf:    times 80 db 0       ; ...one handle's, rebuilt per command
wildbuf:    times RT_BUFSZ db 0 ; ...plus '\*.*', and it is sized by the
                                ; RECURSIVE DELETE now rather than by that:
                                ; rt_build appends an 8.3 name to a path that
                                ; may already be RT_PATHMAX long, and a write
                                ; that could overrun is not something a
                                ; length check AFTER it can fix (62.10.7.2)
rtpath:     times RT_BUFSZ db 0 ; the folder srv_rmtree is currently emptying.
                                ; The whole of the walk's state, with rtdep -
                                ; there is no DTA stack, deliberately
rtdep:      dw 0                ; ...how far below the subtree's root we are
entbuf:     times DE_SZ db 0    ; one staged entry, on its way to the wire
namebuf:    times 16 db 0       ; the 13 bytes a name crosses as, terminated
srv_fh:     dw 0                ; the FILE handle a read is about
srv_cap:    dd 0                ; what the master says it can take
srv_rlen:   dd 0                ; ...and what we are actually sending
srv_off:    dd 0                ; FSV_READAT's window
srv_chunk:  dw 0                ; bytes in the secbuf pass in hand
rfh:        dw 0xFFFF           ; the open DOS handle, FFFF = none
srv_ok:     db 0                ; wr_arg resolved the folder
srv_verb:   db 0                ; which of delete/mkdir/rmdir is in hand
srv_fail:   db 0                ; ...the FERR_* banked across a drain
srv_wbad:   db 0                ; a DOS write came up short: the disk is full
oldbuf:     times 88 db 0       ; rename's first path, while the second is
                                ; built in wildbuf
dtabuf:     times 48 db 0       ; OUR DTA, and not DOS's at PSP:0080 - that is
                                ; where the command tail lives

; =============================================================================
; THE BUFFERS, PAST THE IMAGE (ETH_NOEMIT, SPEC.md 62.11.1)
;
; `absolute` gives every label below an address and emits NOT ONE BYTE, which
; is the standard shape for a .COM's bss - DOS hands a .COM the whole memory
; block, so everything above the image is already ours.
;
; **IT MUST BE THE LAST THING IN THIS FILE AND THE ASSERTION BELOW IS WHY.**
; `absolute $` reserves from wherever it is written, and this program's own
; strings and flags are at the BOTTOM of the file - so placed in the middle it
; handed out addresses that were about to be emitted over, and net_bss_clear
; then zeroed [netflag], the help text and everything else in its path. What
; that looked like was /N silently not being asked for, several screens away
; from the cause.
;
; **THEY ARRIVE DIRTY AND net_bss_clear IS WHAT PAYS FOR THAT.** Emitted, they
; were zero because the file said so; reserved, they are whatever DOS last
; left there - and a socket table of rubbish is a table of sockets that are
; already open. That is PERFORMANCE.md's DIRTYRAM lesson arriving on a real
; machine instead of an emulator, which is where it always arrives.
; =============================================================================
    absolute $
net_bss0:
sk_tab:     resb NET_SOCKS * SK_SZ
sk_rxb:     resb NET_SOCKS * SK_RXCAP
sk_txb:     resb NET_SOCKS * SK_TXCAP
eth_rxh:    resb 4
eth_rxb:    resb NE_FRAME + 4
eth_txb:    resb NE_FRAME + 4
pk_ring:    resb PKT_NBUF * PKT_FRAME
nw_buf:     resb NET_SKMAX
net_bss1:
    section .text
os88net_tail:
%if os88net_tail > net_bss0
  %error "os88net: something is emitted AFTER the absolute block - it and the reserved buffers overlap, and net_bss_clear will zero it"
%endif
