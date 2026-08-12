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

NC_INFO     equ 'I'
NC_READ     equ 'R'
NC_WRITE    equ 'W'
NC_BYE      equ 'X'

; --- and file mode's (SPEC.md 62.10.1) ---------------------------------------
NF_LIST     equ 'L'             ; handle -> status, count, count x 32 bytes
NF_CHDIR    equ 'C'             ; handle -> status, the PARENT's handle
NF_DFREE    equ 'F'             ; -> status, free dword, granule word

NST_OK      equ 0x00
NST_WPROT   equ 0x03
NST_NOSEC   equ 0x04
NST_NOENT   equ 0x02            ; no such handle - DOS's own 'file not found'

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

    mov ax, 0x2524              ; FAIL every critical error, before anything
    mov dx, crit24              ; touches a drive - the /W scan is the first
    int 0x21                    ; thing that can meet an empty one, and DOS
                                ; restores this vector itself at 4Ch
    call setup_root
    jc  .bye
    call open_image             ; ...only if /I: named one
    jc  .bye

    call lpl_scan
    call report_ports
    call pin_or_scan
    jc  .noport

    mov si, s_ready
    call puts
.serve:
    call listen_once            ; one full sweep of the candidates
    call lpl_kbhit
    jnc .serve
    cmp al, 27
    jne .serve
    mov si, s_stopped
    call puts
.bye:
    mov ax, 0x4C00
    int 0x21
.noport:
    mov si, s_noport
    call puts
    jmp short .bye

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
    jmp short .cmd

.flist:
    call srv_list
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
    jcxz .argdone
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
    mov si, s_ronly
    call puts
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

    call hd_path                ; -> pathbuf, or CF=1 for a handle we never
    jc  .nodir                  ; issued

    cmp byte [wholemc], 0       ; the whole-machine root is a list of DRIVES
    je  .files                  ; and not a directory at all
    cmp word [srv_h], 0
    jne .files
    call srv_drives
    jmp short .out

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
    mov al, NST_NOENT           ; a count of zero still completes the frame,
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
    pop bx                      ; rather than NST_NOENT
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
    jmp short .out
.file:
    mov word [entbuf + DE_TYPE], 0
    jmp short .out
.dir:
    mov word [entbuf + DE_TYPE], 2
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
    call hd_path                ; the only validation there is: can we name it
    jc  .no
    mov [srv_cwd], ax           ; ...AX is untouched by hd_path
    mov al, NST_OK
    call lp_sbyte
    jc  .lost
    mov ax, [srv_h]
    call hd_par                 ; -> AX = the parent, 0 for the root
    call lp_sword
    jc  .lost
    jmp short .out
.no:
    mov al, NST_NOENT
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
    mov al, NST_NOENT
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
    db 13,10,'OS88NET - the DOS end of an os8088 parallel Network drive',13,10
    db 'docs/NET-PLAN.md stage 1.  ESC stops.',13,10,13,10,0
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
s_ready:    db 13,10,'Listening. Start the os8088 end (Control Panel, Network,'
            db ' Connect).',13,10,0
s_called:   db 13,10,'Called on ',0
s_bye:      db 13,10,'Master finished.',13,10,0
s_stopped:  db 13,10,'Stopped.',13,10,0

s_wild:     db '*.*',0
s_nodir:    db 13,10,'No such folder.',13,10,0
dirarg:     times 80 db 0       ; the bare argument: the folder to serve
s_root:     db 'Serving files from ',0
s_wmc:      db 'Serving files from EVERY DRIVE on this machine.',13,10,0
s_ronly:    db 13,10,'Phase 1 is READ ONLY whatever the switches say.',13,10,0

imgname:    times 80 db 0       ; ...EMPTY by default now: the bare argument
                                ; is the FOLDER to serve, and block mode is
                                ; behind /I: (see parse_args)
fh:         dw 0
nsecs:      dw 0
roflag:     db 0
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
srv_cwd:    db 0                ; the DRIVE the last NF_CHDIR stood on, for
                                ; NF_DFREE - which is the one verb with no
                                ; handle to work it out from. 0 = the default
                                ; drive, which is int 21h 36h's own convention
nhd:        dw 0                ; handles issued this session
hdtab:      times HD_MAX*HD_SZ db 0
rootpath:   times 72 db 0       ; 'C:\' + DOS's 64 + a NUL
pathbuf:    times 80 db 0       ; ...one handle's, rebuilt per command
wildbuf:    times 88 db 0       ; ...plus '\*.*'
entbuf:     times DE_SZ db 0    ; one staged entry, on its way to the wire
dtabuf:     times 48 db 0       ; OUR DTA, and not DOS's at PSP:0080 - that is
                                ; where the command tail lives
