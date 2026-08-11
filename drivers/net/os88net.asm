; =============================================================================
; OS88NET.COM - the DOS end of os8088's parallel Network drive
;
; Serves 512-byte sectors out of an image file over a LapLink cable, so an
; os8088 machine with NET.DRV loaded mounts it as an ordinary FAT volume.
; Stage 1 of docs/NET-PLAN.md; NET.DRV is the other end.
;
;   OS88NET [image] [/P:378] [/RO]
;
;   image   the file to serve, default OS8088.IMG in the current directory.
;           Its size decides the volume's: os8088 caps a volume at 65,535
;           sectors (31.99MB) by BPB rule 8, so a bigger file is served
;           truncated to that and says so.
;   /P:     pin the port. WITHOUT IT THIS SCANS - the BIOS's own table plus
;           3BC/378/278, deduped and latch-probed - because the two ends of
;           this cable are at different addresses on the two machines it was
;           written for (NET-PLAN 1.4.1: the 5150's GB101 is at 3BC and the
;           DOS box's DIO-500 at 378).
;   /RO     refuse every write. THE DEFAULT FOR A FIRST RUN, and the honest
;           setting for anything you would mind losing.
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

NST_OK      equ 0x00
NST_WPROT   equ 0x03
NST_NOSEC   equ 0x04

SEC_MAX     equ 0xFFFF          ; os8088's own cap: BPB rule 8 (SPEC.md 18.2)

start:
    cld
    mov si, s_banner
    call puts

    call parse_args
    jc  .bye
    call open_image
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
    jmp short .cmd

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
    jcxz .done
    mov al, [si]
    cmp al, '/'
    je  .opt
    cmp al, '-'
    je  .opt
    ; a bare word: the image name
    mov di, imgname
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
    jcxz .done
    mov al, [si]
    or  al, 0x20
    cmp al, 'r'
    je  .ro
    cmp al, 'p'
    je  .port
    jmp short .skipopt
.ro:
    mov byte [roflag], 1
    jmp short .skipopt
.port:
    inc si                      ; past 'P'
    dec cx
    jcxz .done
    cmp byte [si], ':'
    jne .skipopt
    inc si
    dec cx
    call gethex                 ; AX = the base, SI/CX advanced
    mov [pinbase], ax
    mov byte [pinned], 1
    jmp short .next
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
open_image:
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

imgname:    db 'OS8088.IMG'
            times 68 db 0       ; a DOS path is 64 + the name
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
