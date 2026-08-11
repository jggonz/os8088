; =============================================================================
; lptlink - what is on this machine's parallel ports, and how fast is the cable
;
; Step 1 of docs/NET-PLAN.md, and it is deliberately ONE artifact rather than
; the two the plan first proposed (a port survey and a throughput benchmark).
; They want the same code and the same trip: a survey that stops at "there is
; a latch here" cannot answer the question that actually matters - is there a
; COMPUTER on the other end - and answering that means implementing the
; handshake, at which point the throughput figure is a loop around it.
;
; It answers, from the machine itself:
;
;   1. which parallel ports does the BIOS say it found (0040:0008), and which
;      of 3BC / 378 / 278 answer a real probe? The two disagreeing is itself
;      an answer - comscan's warn_outside, in the other connector. FIELD
;      RESULT: the 5150's GB101 is at 3BC and the DOS machine's DIO-500 is at
;      378, which is why neither end of this may assume an address
;      (docs/NET-PLAN.md 1.4.1)
;   2. is there a LapLink cable on one of them with a LIVE PARTNER - as
;      against nothing, or a printer, both of which read as a CONSTANT status
;      byte where a partner reads as whatever we ask it to (NET-PLAN 1.4.4)
;   3. how many bytes a second does it move, EACH WAY, measured on the machine
;      rather than modelled - which is the number NET-PLAN 1.2 does not have
;      and PERFORMANCE.md Part 6 rule 8 will not accept a model for
;
; NEITHER END IS os8088. No kernel byte moves, no driver exists yet, and a
; failure here is a failure of the cable or the protocol and cannot be
; anything else. It is built two ways from this one source, which is
; comscan's shape:
;
;   lptlink.com   a DOS program. Output goes through int 21h, so
;                 `LPTLINK > LPTLINK.TXT` captures the report to a file.
;   lptlink.img   a BOOTABLE 360KB floppy. No DOS, no os8088, no mouse -
;                 the machine boots straight into this. Output via int 10h.
;
; Run it on BOTH machines: one Slave, one Master. START THE SLAVE FIRST - it
; listens, and the Master is the one that sweeps and calls (NET-PLAN 1.3:
; os8088 is the master and never receives unsolicited data).
;
; NOTHING here writes to a disk.
;
; Assemble: nasm -f bin -DCOMFILE -o lptlink.com lptlink.asm   (DOS)
;           nasm -f bin           -o lptlink.bin lptlink.asm   (bootable)
; =============================================================================

    cpu 8086                    ; the 5150 is the point of this

%ifdef COMFILE
    org 0x100
%else
    org 0                       ; loaded at KERNEL_SEG:0000 by boot/boot.asm
%endif

; THE ENTRY IS THE FIRST BYTE, in both builds - DOS enters a .COM at offset
; 0x100 and boot/boot.asm far-jumps to KERNEL_SEG:0000, and neither asks where
; the author put `start:`. This file is safe by LAYOUT (its two %includes are
; at the END, ~600 lines down) and that safety is one edit from gone, so it is
; asserted rather than trusted. It emits nothing; if anything is ever put
; above it, nasm says `TIMES value -N is negative` and N is how many bytes got
; in front of the entry.
;
; os88net.asm is the same assertion for the reason it EXISTS: it had the two
; includes above `start:`, so DOS ran the transport's first routine on a
; garbage port and terminated instantly with nothing printed, whatever
; arguments it was given.
com_entry:
    times ($$ - com_entry) db 0

; --- the benchmark -----------------------------------------------------------
; 64 x 256 = 16KB, which is ~1.6s at the 10KB/s NET-PLAN 1.2 models and ~5s at
; a pessimistic 3KB/s. The tick is 54.9ms, so either way the measurement has
; 2-4% resolution - and the report prints the TICK COUNT beside the rate so a
; reader can see that for themselves rather than taking it on trust.
BENCH_BLKS  equ 64
BLK_SIZE    equ 256
BENCH_BYTES equ BENCH_BLKS*BLK_SIZE

; THE CLOCK STARTS AFTER BLOCK 0, and the reason is lp_turn: a direction
; reversal costs a whole system tick by construction, and on the inbound leg
; that guard runs on the FAR side while this one sits in its receive - so
; timing from the first byte would charge the link 55-110ms it did not spend
; moving data. Block 0 is exchanged untimed and the figure is the 63 after it.
TIMED_BYTES equ (BENCH_BLKS-1)*BLK_SIZE

; bytes/second = bytes * 18.2065 / ticks, done as bytes*182 / (ticks*10) in 32
; bits. The numerator is a constant, so it costs nothing at run time.
RATE_NUM    equ TIMED_BYTES*182
RATE_HI     equ (RATE_NUM >> 16) & 0xFFFF
RATE_LO     equ RATE_NUM & 0xFFFF


%ifndef COMFILE
; -----------------------------------------------------------------------------
; The boot sector's handoff contract (boot/boot.asm), so the shipped loader can
; carry this instead of the kernel: it far-calls SPLASH_OFF = 0x0008 once the
; first sectors are aboard, writes its t=0 word at 0x000C, and far-jumps to
; 0x0000 with DL = the boot drive.
; -----------------------------------------------------------------------------
    jmp main
    times 0x08-($-$$) db 0
    retf                        ; 0x0008 - the splash tick, which we have none
    times 0x0C-($-$$) db 0      ; of, so it just returns
    dw 0                        ; 0x000C - the boot timer's t=0
%endif

; =============================================================================
main:
%ifndef COMFILE
    mov ax, cs                  ; the loader jumped here far; own every segment
    mov ds, ax                  ; and put a stack somewhere harmless (this
    cli                         ; image is a few KB and the relocated boot
    mov ss, ax                  ; sector is far above)
    mov sp, 0x7000
    sti
%endif
    push cs
    pop es
    cld

    mov si, s_title
    call puts
    call survey

.menu:
    call crlf
    mov si, s_menu
    call puts
.key:
    call getkey
    or  al, 0x20                ; fold case
    cmp al, 'm'
    je  .master
    cmp al, 's'
    je  .slave
    cmp al, 'r'
    je  .again
    cmp al, 'q'
    je  .quit
    jmp short .key

.again:
    call crlf
    call survey
    jmp short .menu

.master:
    call crlf
    call role_master
    jmp short .menu

.slave:
    call crlf
    call role_slave
    jmp short .menu

.quit:
    call crlf
%ifdef COMFILE
    mov ax, 0x4C00
    int 0x21
%else
    mov si, s_halt
    call puts
.hang:
    hlt
    jmp short .hang
%endif

; =============================================================================
; THE SURVEY
; =============================================================================

; -----------------------------------------------------------------------------
; survey - build the candidate list, probe every entry, print the table
;
; The list is the BIOS's own table PLUS the three standard bases, deduped. The
; table is TRUSTED here in a way int 11h's floppy count is not (NET-PLAN
; 1.4.1): the POST fills it by PROBING - a write and a read-back, the same
; test lp_latch does below - where the floppy count comes from SW1's DIP
; switches, which is a human's assertion and on the calibration machine a
; wrong one (SPEC.md 18.97). It is still verified, because a clone BIOS may
; scan fewer addresses and a card can sit where the POST never looks.
; -----------------------------------------------------------------------------
survey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call lpl_scan               ; the candidates and their latch verdicts belong
                                ; to drivers/net/lplink.inc, so this tool and
                                ; NET.DRV cannot disagree about what a parallel
                                ; port is or where one might be
    ; --- the table ------------------------------------------------------------
    mov si, s_phead
    call puts
    mov cl, [ncand]
    xor ch, ch
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
    mov al, [cand_bios+bx]
    call put_yn
    mov si, s_gap
    call puts
    mov bx, di
    mov al, [cand_ok+bx]
    call put_ok
    mov si, s_gap
    call puts
    mov bx, di                  ; the raw status and control bytes, printed
    shl bx, 1                   ; whatever the latch said - an absent port
    mov dx, [cand_base+bx]      ; reading FF FF is exactly as informative as a
    inc dx                      ; live one, and it is what a reader compares
    in  al, dx                  ; against the next machine's report
    call puthex8
    call putsp
    inc dx
    in  al, dx
    call puthex8
    call crlf
    inc di
    pop cx
    loop .row
    jmp short .out
.none:
    mov si, s_noports
    call puts
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; THE MASTER - sweeps, calls, benchmarks
; =============================================================================

role_master:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, s_msweep
    call puts
    mov cl, [ncand]
    or  cl, cl
    jz .noports
    xor ch, ch
    xor di, di
.try:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next                   ; no latch there: do not call into nothing
    shl bx, 1
    mov si, s_trying
    call puts
    mov ax, [cand_base+bx]
    call puthex16
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call mst_hello
    jc  .nope
    mov si, s_linked
    call puts
    mov al, [lp_ver]
    call puthex8
    call crlf
    call mst_bench
    call lp_restore
    pop cx
    jmp short .out
.nope:
    call lp_restore
    mov si, s_noans
    call puts
.next:
    inc di
    pop cx
    loop .try
    mov si, s_nolink
    call puts
    jmp short .out
.noports:
    mov si, s_noports
    call puts
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mst_bench - 16KB out, 16KB back, each timed and verified
; -----------------------------------------------------------------------------
mst_bench:
    push ax
    push bx
    push cx
    push si

    ; --- outbound -------------------------------------------------------------
    mov si, s_bout
    call puts
    mov al, 'B'
    call lp_sbyte
    jc  .fail
    mov ax, BENCH_BLKS
    call lp_sword
    jc  .fail
    mov word [bk_blk], 0
.oblk:
    cmp word [bk_blk], 1        ; the clock starts after block 0 - see
    jne .onot0                  ; TIMED_BYTES
    call lpl_ticks
    mov [t0], ax
.onot0:
    mov cx, BLK_SIZE
    xor bx, bx
.obyt:
    mov al, bl
    add al, [bk_blk]            ; the pattern: (block + index) & FF
    call lp_sbyte
    jc  .fail
    inc bl
    loop .obyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, BENCH_BLKS
    jb  .oblk
    call lpl_ticks
    sub ax, [t0]
    mov [dt], ax
    call lp_rword               ; what the far end made of it
    jc  .fail
    mov [bk_err], ax
    call report

    ; --- inbound --------------------------------------------------------------
    mov si, s_bin
    call puts
    mov al, 'R'
    call lp_sbyte
    jc  .fail
    mov ax, BENCH_BLKS
    call lp_sword
    jc  .fail
    mov word [bk_err], 0
    call lpl_ticks
    mov [t0], ax
    mov word [bk_blk], 0
.iblk:
    cmp word [bk_blk], 1
    jne .inot0
    call lpl_ticks
    mov [t0], ax
.inot0:
    mov cx, BLK_SIZE
    xor bx, bx
.ibyt:
    call lp_rbyte
    jc  .fail
    mov ah, bl
    add ah, [bk_blk]
    cmp al, ah
    je  .iok
    inc word [bk_err]
.iok:
    inc bl
    loop .ibyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, BENCH_BLKS
    jb  .iblk
    call lpl_ticks
    sub ax, [t0]
    mov [dt], ax
    call report

    mov al, 'X'                 ; release the far end
    call lp_sbyte
    jmp short .out
.fail:
    mov si, s_bfail
    call puts
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; report - [dt] ticks and [bk_err] errors for BENCH_BYTES bytes
;
; The TICK COUNT is printed beside the rate on purpose: the tick is 54.9ms, so
; a reader can see the measurement's own resolution instead of taking the
; bytes/second on trust. PERFORMANCE.md Part 6 rule 8's habit, in miniature.
; -----------------------------------------------------------------------------
report:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, TIMED_BYTES
    call putdec16
    mov si, s_rticks
    call puts
    mov ax, [dt]
    call putdec16
    mov si, s_rrate
    call puts
    mov ax, [dt]
    or  ax, ax
    jz  .fast
    mov cx, 10
    mul cx                      ; DX:AX = ticks*10 - and DX is 0, because a
    mov cx, ax                  ; benchmark that ran for 6553 ticks is 6 min
    mov ax, RATE_LO
    mov dx, RATE_HI
    call div32
    call putdec32
    jmp short .err
.fast:
    mov si, s_rfast
    call puts
.err:
    mov si, s_rerr
    call puts
    mov ax, [bk_err]
    call putdec16
    call crlf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE SLAVE - listens, answers, serves
; =============================================================================

role_slave:
    push ax
    push bx
    push cx
    push si
    push di
    mov byte [slv_abort], 0
    mov si, s_slisten
    call puts
    mov cl, [ncand]
    or  cl, cl
    jz  .noports
.cycle:
    mov cl, [ncand]
    xor ch, ch
    xor di, di
.one:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call slv_hunt
    jc  .quiet
    mov si, s_scalled
    call puts
    mov ax, [lp_base]
    call puthex16
    call crlf
    call slv_reply
    jc  .drop
    call slv_serve
.drop:
    call lp_restore
    pop cx
    mov si, s_sdone
    call puts
    jmp short .out
.quiet:
    call lp_restore
    cmp byte [slv_abort], 0
    jne .stopped
.next:
    inc di
    pop cx
    loop .one
    jmp short .cycle
.stopped:
    pop cx
    mov si, s_sstop
    call puts
    jmp short .out
.noports:
    mov si, s_noports
    call puts
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; slv_serve - the command loop. 'B' take a run, 'R' send one, 'X' finished
; -----------------------------------------------------------------------------
slv_serve:
    push ax
    push bx
    push cx
    push si
.cmd:
    call lp_rbyte_w
    jc  .out
    cmp al, 'B'
    je  .take
    cmp al, 'R'
    je  .give
    cmp al, 'X'
    je  .out
    jmp short .cmd

.take:
    mov si, s_stake
    call puts
    call lp_rword
    jc  .out
    mov [bk_n], ax
    mov word [bk_err], 0
    mov word [bk_blk], 0
.tblk:
    mov cx, BLK_SIZE
    xor bx, bx
.tbyt:
    call lp_rbyte
    jc  .out
    mov ah, bl
    add ah, [bk_blk]
    cmp al, ah
    je  .tok
    inc word [bk_err]
.tok:
    inc bl
    loop .tbyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, [bk_n]
    jb  .tblk
    mov ax, [bk_err]
    call lp_sword
    jc  .out
    mov si, s_sdot
    call puts
    jmp short .cmd

.give:
    mov si, s_sgive
    call puts
    call lp_rword
    jc  .out
    mov [bk_n], ax
    mov word [bk_blk], 0
.gblk:
    mov cx, BLK_SIZE
    xor bx, bx
.gbyt:
    mov al, bl
    add al, [bk_blk]
    call lp_sbyte
    jc  .out
    inc bl
    loop .gbyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, [bk_n]
    jb  .gblk
    mov si, s_sdot
    call puts
    jmp .cmd
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

%include "lplink.inc"           ; the transport - ONE BODY, shared with
                                ; NET.DRV, so what this measures is what ships
%include "lplslv.inc"           ; ...and the slave half, shared with OS88NET

; =============================================================================
; ARITHMETIC AND OUTPUT
; =============================================================================

; -----------------------------------------------------------------------------
; div32 - DX:AX / CX -> DX:AX
;
; Two 16-bit divides, the first one's remainder becoming the second's high
; half. Neither can overflow: the first has a zero high word, and the second's
; dividend high half is a remainder and so is already less than CX. That
; matters here because a single `div` of 2,981,888 by a small tick count
; DIVIDE-FAULTS, and a fast link is exactly when the tick count is small.
; -----------------------------------------------------------------------------
div32:
    push bx
    push ax
    mov ax, dx
    xor dx, dx
    div cx                      ; AX = high quotient, DX = remainder
    mov bx, ax
    pop ax
    div cx                      ; AX = low quotient
    mov dx, bx
    pop bx
    ret

; div10 - DX:AX / 10 -> DX:AX, BX = the remainder
div10:
    push cx
    mov cx, 10
    push ax
    mov ax, dx
    xor dx, dx
    div cx
    mov bx, ax
    pop ax
    div cx
    mov cx, dx
    mov dx, bx
    mov bx, cx
    pop cx
    ret

putdec32:                       ; DX:AX, no leading zeros
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
.d:
    call div10
    push bx
    inc cx
    mov bx, ax
    or  bx, dx
    jnz .d
.o:
    pop ax
    add al, '0'
    call putc
    loop .o
    pop dx
    pop cx
    pop bx
    pop ax
    ret

putdec16:                       ; AX, no leading zeros
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

; lpl_ticks - lplink.inc's ONE requirement of its includer (see its header)
lpl_ticks:                      ; out AX = the BIOS tick counter
    push es
    push bx
    mov bx, 0x0040
    mov es, bx
    mov ax, [es:0x006C]
    pop bx
    pop es
    ret

; getkey - wait for a key and RETURN IT
; out: AL = ASCII, AH = the scan code
;
; IT MUST NOT PRESERVE AX, and that is the whole comment. comscan's getkey is
; `push ax / xor ax,ax / int 16h / pop ax / ret` - correct there, because it is
; a "press any key to continue" pause and the key is not wanted. Copied into a
; MENU it is a program that reads a keystroke, throws it away, restores the
; caller's AX, matches nothing, and loops - so it sits at its prompt EATING
; every key while looking hung. Reported off both field machines as "neither
; end responded to keyboard input (might be frozen?)". It was not frozen.
getkey:
    xor ax, ax
    int 0x16
    ret

; lpl_kbhit - lplslv.inc's requirement of its includer
kbhit:
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

; putc - AL to the console. Under DOS that is stdout, so the whole report
;        redirects into a file; standalone it is the BIOS teletype.
putc:
    push ax
    push bx
    push dx
%ifdef COMFILE
    mov dl, al
    mov ah, 0x02
    int 0x21
%else
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
%endif
    pop dx
    pop bx
    pop ax
    ret

puts:                           ; DS:SI, NUL-terminated
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

putsp:
    push ax
    mov al, ' '
    call putc
    pop ax
    ret

puthex8:                        ; AL
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
    call .nib
    pop cx
    pop ax
    ret
.nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    call putc
    ret

puthex16:                       ; AX
    push ax
    mov al, ah
    call puthex8
    pop ax
    push ax
    call puthex8
    pop ax
    ret

put_yn:                         ; AL = 0/1
    push si
    mov si, s_no
    or  al, al
    jz  .say
    mov si, s_yes
.say:
    call puts
    pop si
    ret

put_ok:                         ; AL = 0/1
    push si
    mov si, s_dash
    or  al, al
    jz  .say
    mov si, s_ok
.say:
    call puts
    pop si
    ret

; =============================================================================
; DATA
; =============================================================================

s_title:
    db 13,10,'lptlink - parallel port survey and cable benchmark',13,10
    db 'docs/NET-PLAN.md step 1.  Neither end of this is os8088.',13,10,13,10,0
s_phead:
    db '  base  bios  latch  stat ctrl',13,10,0
s_ind:      db '  ',0
s_gap:      db '   ',0
s_yes:      db ' yes',0
s_no:       db '  no',0
s_ok:       db '   ok',0
s_dash:     db '   --',0
s_noports:  db '  no parallel port found',13,10,0
s_menu:
    db '[M]aster - sweep and call    [S]lave - listen and answer',13,10
    db '[R]escan  [Q]uit             START THE SLAVE FIRST',13,10,'> ',0
s_msweep:   db 'Master: sweeping the ports that answered.',13,10,0
s_trying:   db '  calling ',0
s_noans:    db '  - no answer',13,10,0
s_linked:   db '  - LINKED, partner protocol ',0
s_nolink:   db '  no partner found. Is the slave running, and started first?',13,10,0
s_bout:     db '  out: ',0
s_bin:      db '  in : ',0
s_bfail:    db '  link lost mid-benchmark',13,10,0
s_rticks:   db ' bytes in ',0
s_rrate:    db ' ticks = ',0
s_rfast:    db '(under one tick - too fast to time here)',0
s_rerr:     db ' bytes/sec, errors ',0
s_slisten:
    db 'Slave: listening on every port that answered. ESC stops.',13,10,0
s_scalled:  db '  called on ',0
s_stake:    db '  <- ',0
s_sgive:    db '  -> ',0
s_sdot:     db 'ok',13,10,0
s_sdone:    db '  master finished.',13,10,0
s_sstop:    db '  stopped.',13,10,0
%ifndef COMFILE
s_halt:     db 'halted - power off or reset.',13,10,0
%endif

; --- state that is THIS TOOL's. The transport's own - the candidate list, the
; --- port words and the deadline scratch - lives in lplink.inc with the code
; --- that uses it, so nothing here can drift out of step with NET.DRV.

t0:         dw 0
dt:         dw 0
bk_n:       dw 0
bk_blk:     dw 0
bk_err:     dw 0
