; =============================================================================
; tests/dosstub - just enough DOS to RUN a .COM, on a machine that has none
;
; WHY THIS EXISTS. `OS88NET.COM` is the DOS half of SPEC.md 62's parallel link,
; and this container has no DOS - MartyPC boots os8088 and 86Box boots os8088,
; and neither has a copy of DOS this tree may ship. So the DOS end was written,
; assembled, packaged and SENT TO THE FIELD TWICE without one instruction of it
; ever being executed. The second time it came back with the whole answer:
; "returns to prompt instantly with nothing printed, whatever parameters".
;
; The bug was that its two %includes sat above `start:`, and DOS enters a .COM
; at offset 0x100 - the FIRST byte of the file. So DOS ran the transport's
; first routine on a garbage port, hit its `ret`, popped the word DOS pushes at
; PSP:0000 and terminated on the int 20h sitting there. Both .COM sources
; assert their entry is at 0x100 now, which stops THAT bug. This stops the
; class: it is a machine to run the program on.
;
; WHAT IT IS. A bootable 360KB floppy carrying an int 21h stub and the .COM
; embedded in its own image. It boots on the same 4.77MHz 8088 MartyPC runs
; everything else on, with a REAL parallel port at 0x378, so the latch probe,
; the port scan and the dwell all execute for real.
;
; AND IT TALKS NOW, which reverses what this paragraph used to say. It said
; MartyPC "cannot be a PARTNER, because its status lines read a constant" -
; and the status lines a GUEST reads are whatever something wrote to them, so
; the debug server can be the far end (SPEC.md 62.10.3).
; tests/lptlink/partner.py in its MASTER role plays NET.DRV against this, and
; that is what finally executed OS88NET.COM's own code: the argument parsing,
; the handle table, the path walk and the two-pass listing had, until then,
; run on exactly one machine in the world.
;
; WHAT IT IS NOT. It is not DOS and must never grow towards being DOS. It
; implements the int 21h functions os88net.com ACTUALLY CALLS and refuses the
; rest LOUDLY - an unimplemented call prints its AH and halts, rather than
; returning a plausible zero, because a stub that silently succeeds is how a
; harness starts lying about the thing it is testing.
;
; That set is SIXTEEN now and it was seven, because the DOS side became a FILE
; SERVER (SPEC.md 62.10): 0Eh 19h 1Ah 25h 36h 3Bh 47h 4Eh 4Fh joined the
; original seven, over the nine-row synthetic directory below. That growth is
; not the growth the paragraph above forbids - the rule is "what the program
; calls", and the program calls nine more than it did. What would break the
; rule is a function nothing here reaches.
;
;   make dosstub          build/dosstub.img, and how to run it
;
; The "file" it serves is RAM: DSF_SEG holds the first DSF_KB, and the SIZE it
; reports is a pair of constants, so the sector-count arithmetic in
; open_image - a 32-bit byte count folded into a 16-bit sector count with no
; 32-bit registers - is exercised over its whole range without a 32MB image.
; The three FSIZE lines below need `/I:` IN THE TAIL to reach it at all now:
; the redirector superseded block mode, so open_image returns at once unless a
; run asks for an image by name (SPEC.md 62.10).
;   make dosstub ARGS='/I:IMAGE'         10MB, the old ordinary path (20,480 secs)
;   make dosstub FSIZE=64M ARGS='/I:IMAGE'  past os8088's cap: 65,535, and it says so
;   make dosstub FSIZE=256 ARGS='/I:IMAGE'  under one sector: the refusal
;   make dosstub ARGS='/P:378 /W'    ...and the whole-machine root, whose
;                                    listing is srv_drives rather than a
;                                    findfirst - a different walk entirely
;   make dosstub ARGS='/RO /P:378'   the command tail, which is code nothing
;                                    else in this tree executes
; =============================================================================

    cpu 8086
    org 0                       ; loaded at KERNEL_SEG:0000 by boot/boot.asm

com_entry:
    times ($$ - com_entry) db 0 ; the entry is the first byte here too

; -----------------------------------------------------------------------------
; The boot sector's handoff contract (boot/boot.asm), so the shipped loader can
; carry this instead of the kernel: it far-calls SPLASH_OFF = 0x0008 once the
; first sectors are aboard, writes its t=0 word at 0x000C, and far-jumps to
; 0x0000 with DL = the boot drive. tests/lptlink/lptlink.asm has the same
; three lines and for the same reason.
;
; LEAVING THEM OUT IS NOT A CRASH, WHICH IS WHY IT COSTS A SESSION: the loader
; far-called offset 8 - four bytes into this file's own first instruction -
; ran whatever that decoded as, and returned. The machine then booted and ran
; the program, and the only symptom was that the screen had one line on it.
; -----------------------------------------------------------------------------
    jmp start
    times 0x08-($-$$) db 0
    retf                        ; 0x0008 - the splash tick, which we have none
    times 0x0C-($-$$) db 0      ; of, so it just returns
    dw 0                        ; 0x000C - the boot timer's t=0

PSP_SEG     equ 0x2000          ; 128KB: above us, below the RAM file
DSF_SEG     equ 0x4000          ; 256KB: the backing store's first DSF_KB
DSF_KB      equ 32              ; ...and how much of it is real. 32 and not
                                ; 64 because clamp's `DSF_KB * 1024` is a
                                ; word and 64KB is 0x10000: the one place a
                                ; round number does not fit the machine

%ifndef FSIZE_LO
%define FSIZE_LO 0x0000         ; default 10MB = 0x00A00000 -> 20,480 sectors
%endif
%ifndef FSIZE_HI
%define FSIZE_HI 0x00A0
%endif

FH          equ 5               ; the one handle we hand out

start:
    cli
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, stack_top
    sti
    cld

    mov si, s_banner
    call puts

    ; --- the two vectors a .COM can leave through ---------------------------
    xor ax, ax
    mov es, ax
    cli
    mov word [es:0x20*4], int20
    mov [es:0x20*4+2], cs
    mov word [es:0x21*4], int21
    mov [es:0x21*4+2], cs
    sti

    ; --- a PSP, and the command tail argument parsing needs -----------------
    mov ax, PSP_SEG
    mov es, ax
    xor di, di
    mov cx, 128
    xor ax, ax
    rep stosw                   ; 256 bytes of PSP, cleared
    mov word [es:0x0000], 0x20CD ; int 20h, which is what a bare `ret` lands on
    mov si, cmdtail_txt         ; the text first, COUNTING it: the length byte
    mov di, 0x0081              ; at 0x80 is a forward reference if it is
    xor cx, cx                  ; emitted with the string, and nasm is right
.tl:                            ; to refuse that
    lodsb
    or  al, al
    jz  .tdone
    stosb
    inc cx
    jmp short .tl
.tdone:
    mov al, 13                  ; ...and DOS's own terminator
    stosb
    mov [es:0x0080], cl

    ; --- the program itself, to PSP:0100 ------------------------------------
    mov si, com_image
    mov di, 0x0100
    mov cx, com_len
    rep movsb

%ifdef PKTFAKE
    call pkt_install            ; a PACKET DRIVER on int 60h, for /N
%endif
    mov si, s_go
    call puts

    ; --- and away: DS = ES = SS = the PSP, a word of 0 on the stack ---------
    mov ax, PSP_SEG
    mov ds, ax
    mov es, ax
    cli
    mov ss, ax
    mov sp, 0xFFFE
    sti
    xor ax, ax
    push ax                     ; so a bare `ret` reaches PSP:0000's int 20h
    push ds                     ; ...and the far jump in
    mov ax, 0x0100
    push ax
    retf

; =============================================================================
; int 20h / int 21h
;
; THE IRET FRAME IS [bp+2] IP, [bp+4] CS, [bp+6] FLAGS after `push bp` /
; `mov bp, sp`, and CF has to be written THERE rather than left in the live
; flags - an `iret` reloads them from the stack, so a `clc` before it is a
; `clc` the caller never sees. Every DOS call here answers in CF.
; =============================================================================
int20:
    xor al, al
    jmp term

int21:
    sti
    cmp ah, 0x02
    je  f_putc
    cmp ah, 0x3D
    je  f_open
    cmp ah, 0x3E
    je  f_close
    cmp ah, 0x3F
    je  f_read
    cmp ah, 0x40
    je  f_write
    cmp ah, 0x42
    je  f_seek
    cmp ah, 0x4C
    je  term
    cmp ah, 0x0E
    je  f_setdrv
    cmp ah, 0x19
    je  f_getdrv
    cmp ah, 0x1A
    je  f_setdta
    cmp ah, 0x25
    je  f_setvec
    cmp ah, 0x35
    je  f_getvec
    cmp ah, 0x36
    je  f_dfree
    cmp ah, 0x3B
    je  f_chdir
    cmp ah, 0x47
    je  f_getcwd
    cmp ah, 0x4E
    je  f_findfirst
    cmp ah, 0x4F
    je  f_findnext
    cmp ah, 0x3C
    je  f_create
    cmp ah, 0x41
    je  f_unlink
    cmp ah, 0x39
    je  f_mkdir
    cmp ah, 0x3A
    je  f_rmdir
    cmp ah, 0x56
    je  f_rename
    jmp f_unimp

; --- AH=02h: DL to the screen -------------------------------------------------
f_putc:
    push ax
    mov al, dl
    call putc
    pop ax
    iret

; --- AH=3Dh: open. Any name, one handle, position 0. --------------------------
; The NAME is at the CALLER's DS:DX and is printed before anything moves,
; which is the point of a harness: "the open failed" and "the open failed on
; THAT name" are different sentences.
; A NAME IN THE TREE OPENS THE TREE; THE ONE NAME `IMAGE` OPENS THE RAM FILE;
; ANYTHING ELSE FAILS. Both of the first two are wanted - the redirector reads
; files out of the synthetic directory below, and block mode's `/I:` names
; something deliberately not in it so the FSIZE arithmetic keeps its harness -
; and the third is what makes the harness honest.
;
; It used to fall back to the RAM file for ANY unknown name, and that quietly
; removed a whole class of test: `open a file that is not there` could not
; fail, so FSV_APPEND to a missing file came back FERR_FULL (the RAM file
; opened, the seek went past its real DSF_KB, the write came up short) where
; DOS would have said FERR_NOENT. A stub that succeeds at an open the real
; thing refuses is exactly the lying this file's header forbids.
;
; Which one a handle refers to is [fh_row] - 0xFF for the RAM file.
;
; It no longer PRINTS the name it was given. That was the right call when an
; open was a once-per-run event; a redirected read opens a file per command,
; and a server scrolling its own console per read is a harness charging the
; thing under test for a screenful of int 10h.
f_open:
    push si
    push ds
%ifdef FAILOPEN
    mov si, dx
    call farputs                ; DS:SI, still the caller's segment
    push cs
    pop ds
    mov si, s_nofile            ; ...the OTHER answer, which the program has an
    call puts                   ; error path for that nothing else here can
    pop ds                      ; reach: DOS says no.
    pop si
    mov ax, 2                   ; file not found
    jmp cf_set
%else
    mov si, dx
    call row_of                 ; the caller's DS:SI, against the tree
    push cs
    pop ds
    jc  .notree
    ; A DIRECTORY IS REFUSED, because DOS 2.0+ refuses one: int 21h 3Dh on a
    ; folder's name answers AX = 5 (access denied) with CF, and `srv_copy`
    ; leans on that - it opens the source and lets DOS decide. Opening it here
    ; made this stub KINDER THAN THE MACHINE (SPEC.md 62.10.4.2/62.10.4.7.1),
    ; and the shape of the lie is the usual one: NF_COPY of a folder answered
    ; `ok` on the harness and would have answered `no such file` in the field.
    push bx
    push ax
    xor ah, ah
    call dr_row                 ; -> CS:BX, the row
    pop ax
    test byte [cs:bx+DR_ATTR], 0x10
    pop bx
    jnz .nodir
    mov [cs:fh_row], al
    jmp short .ok
.notree:
    mov si, cmpbuf              ; row_of left the last component here, whether
    mov di, ram_name            ; or not it matched
    call cs_eq
    jnc .nofile
    mov byte [cs:fh_row], 0xFF
.ok:
    pop ds
    pop si
    mov word [cs:fpos], 0
    mov word [cs:fpos+2], 0
    mov ax, FH
    jmp cf_clear
.nofile:
    pop ds
    pop si
    mov ax, 2                   ; DOS's 'file not found', which is what an
    jmp cf_set                  ; open of something absent has to be
.nodir:
    pop ds
    pop si
    mov ax, 5                   ; ...and 'access denied' for a directory, which
    jmp cf_set                  ; is the other answer DOS gives here
%endif

; --- AH=3Eh: close ------------------------------------------------------------
f_close:
    jmp cf_clear

; --- AH=3Fh: read into the caller's DS:DX ------------------------------------
; ONLY THE FIRST DSF_KB IS REAL. A request past it is answered SHORT rather
; than wrongly - a caller that asked for 512 and got 0 finds out, where one
; handed 512 bytes of whatever was in RAM does not.
f_read:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    cmp byte [cs:fh_row], 0xFF
    jne .tree
    call clamp                  ; CX = how much of it is real, from [fpos]
    mov [cs:xfer], cx
    jcxz .none
    mov ax, ds                  ; the caller's DS:DX is the DESTINATION
    mov es, ax
    mov di, dx
    mov ax, DSF_SEG
    mov ds, ax
    mov si, [cs:fpos]           ; DSF_KB <= 64, so the offset is one word
    rep movsb
    jmp short .none

    ; --- a file from the tree: GENERATED, not stored ------------------------
    ; Byte i of row r is (i + 7*r) & 0xFF, so a nine-row directory with a
    ; 20,000-byte file in it costs the image nothing at all - and the host end
    ; can predict every byte, which is the point: a read that arrives is not
    ; the same claim as a read that arrives CORRECT. It is the sysbench
    ; BENCH.DAT trick (SPEC.md 18.94), one file down.
.tree:
    call tclamp                 ; CX = min(asked, what is left of the row)
    mov [cs:xfer], cx
    jcxz .none
    mov ax, ds
    mov es, ax
    mov di, dx
    mov al, [cs:fh_row]         ; the per-row phase
    mov bl, 7
    mul bl                      ; AX = 7*row, and AL is what we want
    mov bl, al
    add bl, byte [cs:fpos]      ; ...plus the position's low byte: the whole
                                ; pattern is mod 256, so only the low byte of
                                ; the offset can matter
.gen:
    mov al, bl
    stosb
    inc bl
    loop .gen
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, [cs:xfer]
    call fadv
    jmp cf_clear

; --- AH=40h: write from the caller's DS:DX -----------------------------------
f_write:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    cmp byte [cs:fh_row], 0xFF
    jne .tree
    call clamp
    mov [cs:xfer], cx
    jcxz .none
    mov si, dx                  ; the caller's DS:DX is the SOURCE
    mov ax, DSF_SEG
    mov es, ax
    mov di, [cs:fpos]
    rep movsb
    jmp short .none

    ; --- a tree row: THE BYTES ARE DISCARDED AND THE SIZE IS KEPT -----------
    ; A row's contents are generated (f_read), so there is nowhere to put
    ; these and nothing that would read them back. What a write here has to
    ; get right is the METADATA - the file grows, and a later listing and stat
    ; say so - because that is what this end of the wire can be honest about.
    ; The byte-exact claim belongs to the os8088-side harness, where
    ; partner.py holds real blobs; see the header above f_create.
.tree:
    mov [cs:xfer], cx           ; every byte accepted: no medium to fill
    jcxz .none
    mov al, [cs:fh_row]
    xor ah, ah
    call dr_row
    mov ax, [cs:fpos]           ; the new END, if this write went past it
    add ax, cx
    mov dx, [cs:fpos+2]
    adc dx, 0
    cmp dx, [cs:bx+DR_SIZE+2]
    jb  .none
    ja  .grew
    cmp ax, [cs:bx+DR_SIZE]
    jbe .none
.grew:
    mov [cs:bx+DR_SIZE], ax
    mov [cs:bx+DR_SIZE+2], dx
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, [cs:xfer]
    call fadv
    jmp cf_clear

; --- AH=42h: seek. AL = 0 start / 1 current / 2 end. CX:DX in, DX:AX out. ----
f_seek:
    cmp al, 2
    je  .end
    cmp al, 1
    je  .cur
    mov [cs:fpos], dx           ; from the start
    mov [cs:fpos+2], cx
    jmp short .out
.cur:
    add [cs:fpos], dx
    adc [cs:fpos+2], cx
    jmp short .out
.end:
    push bx
    mov ax, FSIZE_LO            ; ...the RAM file's end, or the ROW's
    mov bx, FSIZE_HI
    cmp byte [cs:fh_row], 0xFF
    je  .fend
    push ax
    mov al, [cs:fh_row]
    xor ah, ah
    call dr_row
    mov bx, [cs:bx+DR_SIZE+2]
    pop ax
    push bx
    mov al, [cs:fh_row]
    xor ah, ah
    call dr_row
    mov ax, [cs:bx+DR_SIZE]
    pop bx
.fend:
    add ax, dx
    mov [cs:fpos], ax
    mov ax, bx
    adc ax, cx
    mov [cs:fpos+2], ax
    pop bx
.out:
    mov ax, [cs:fpos]
    mov dx, [cs:fpos+2]
    jmp cf_clear

; =============================================================================
; A DIRECTORY, for the half of OS88NET.COM that serves FILES (SPEC.md 62.10.2)
;
; SPEC.md 62.10.3 says the wire's verdict comes off two period boxes, and that
; is about the CABLE. The DOS side's own code - argument parsing, the handle
; table, the path walk, the two-pass listing - is not about the cable at all,
; and until this existed the only machine it had ever run on was the field
; one. That is the exact history this whole file was written to end.
;
; SO THE TREE IS A TABLE AND NOT A FILESYSTEM. Nine rows of (parent, name,
; attribute, size), walked linearly - there is no free list, no allocation and
; nothing writable, because phase 1 serves no write verb and a stub that
; implemented one would be testing itself. Growing this towards DOS is the
; thing this file's header forbids; growing it to cover what the program
; ACTUALLY CALLS is what the file is for, and the program calls nine more
; functions than it did.
;
;   \                 README.TXT 1234, HELLO.O88 4096, GAMES\, EMPTY\
;   \GAMES            CHESS.EXE 20000, SUB\
;   \GAMES\SUB        DEEP.TXT 7
;   \EMPTY            - nothing, which is the case findfirst reports as an
;                       ERROR and the server must report as a COUNT OF ZERO
;
; The shapes it exists to exercise: a folder inside a folder (so hd_path walks
; more than one level), an .O88 (so ent_ispkg fires), a subdirectory whose
; parent is not the root, and an empty one.
; =============================================================================
DR_PAR      equ 0               ; byte: the parent's row, 0xFF = the root
DR_ATTR     equ 1               ; byte: 0x10 = directory
DR_SIZE     equ 2               ; dword
DR_NAME     equ 6               ; 13 bytes, ASCIIZ - the DTA's own layout
DR_SZ       equ 20
DR_GONE     equ 0xFE            ; a row that has been deleted, and is free for
                                ; tree_add to take. NOT 0xFF: that is the ROOT,
                                ; and a freed row wearing it would appear in
                                ; the root's listing the moment it was removed
                                ; from a subfolder

; A MACRO AND NOT HAND-COUNTED PADDING, because hand-counting it is exactly
; how this shipped broken: every row emitted 19 bytes against a DR_SZ of 20,
; so row 1 onward was read at an offset one byte short of where it was written
; and the walk stopped after the first entry. What that looks like from the
; other end of a cable is a directory with one file in it - a perfectly
; plausible answer, on a server that is working. `times` computes the pad, and
; a name too long for the field fails the BUILD with a negative count.
%macro DR_ROW 4                 ; parent, attribute, size, name
    db %1, %2
    dd %3
%%n:
    db %4, 0
    times DR_SZ - 6 - ($ - %%n) db 0
%endmacro

dr_tab:
    DR_ROW 0xFF, 0x00, 1234,  'README.TXT'
    DR_ROW 0xFF, 0x00, 4096,  'HELLO.O88'
    DR_ROW 0xFF, 0x10, 0,     'GAMES'
    DR_ROW 0xFF, 0x10, 0,     'EMPTY'
    DR_ROW 2,    0x00, 20000, 'CHESS.EXE'
    DR_ROW 2,    0x10, 0,     'SUB'
    DR_ROW 5,    0x00, 7,     'DEEP.TXT'
    ; ...and SPARE ROWS, because phase 3 CREATES. A write to a name that is
    ; not here has to land somewhere, and a table sized exactly to its seeds
    ; would refuse every one of them - which reads as the write path failing.
%rep 8
    DR_ROW DR_GONE, 0x00, 0, ''
%endrep
DR_N        equ ($ - dr_tab) / DR_SZ
%if ($ - dr_tab) % DR_SZ != 0
  %error "dosstub: dr_tab is not a whole number of DR_SZ rows"
%endif

; --- AH=0Eh: select disk. One drive, and it is C:. ---------------------------
; It ACCEPTS any letter and reports one drive, because os88net only ever calls
; it for a drive letter the user typed - and the honest failure for a wrong
; one is the chdir that follows saying no, not this.
f_setdrv:
    mov al, 3
    jmp cf_clear

; --- AH=19h: current drive. 2 = C:. ------------------------------------------
f_getdrv:
    mov al, 2
    jmp cf_clear

; --- AH=1Ah: set the DTA -----------------------------------------------------
f_setdta:
    mov [cs:dta_off], dx
    push ds
    pop ax
    mov [cs:dta_seg], ax
    jmp cf_clear

; --- AH=25h: set an interrupt vector -----------------------------------------
; TAKEN AND HONOURED, not swallowed: os88net installs an int 24h handler and
; a stub that pretended to would be hiding whether the program can be reached
; by one. Nothing here raises a critical error, so it is never called - but
; the vector is where the program put it.
; --- AH=35h: get interrupt vector. AL = the vector -> ES:BX -----------------
; ne_probe (drivers/net/pktdrv.inc) walks 60h..80h with this looking for a
; packet driver's signature, so /N needs it. The IVT is at 0:0 and this is
; the whole of the call.
f_getvec:
    push ax
    xor bx, bx
    mov es, bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [es:bx+2]
    mov bx, [es:bx]
    mov es, ax
    pop ax
    iret

f_setvec:
    push bx
    push es
    push ds
    xor bx, bx
    mov es, bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [es:bx], dx
    pop ax                      ; the caller's DS, pushed above
    mov [es:bx+2], ax
    push ax                     ; ...put back for the pop below
    pop ds
    pop es
    pop bx
    jmp cf_clear

; --- AH=36h: free space. AX = spc, BX = free clusters, CX = bps, DX = total --
; DL = 0 is the default drive and 1..26 are A:..Z:. Only C: answers, so /W's
; drive scan sees exactly one live drive - which is the case worth testing,
; because it is the one where the root's listing is a DIFFERENT walk from
; every other folder's.
f_dfree:
    cmp dl, 0
    je  .ok
    cmp dl, 3                   ; C:
    je  .ok
    mov ax, 0xFFFF              ; ...and everything else is not there, which
    jmp cf_clear                ; is what the int 24h path would otherwise be
.ok:
    mov ax, 4                   ; 2KB clusters
    mov bx, 1000                ; 2,048,000 bytes free
    mov cx, 512
    mov dx, 5000
    jmp cf_clear

; --- AH=3Bh: chdir. DS:DX = the path. ----------------------------------------
f_chdir:
    push si
    push di
    mov si, dx
    call dir_of                 ; the CALLER's DS:SI -> AL = a row, CF=1 = no
    jc  .no
    mov [cs:cwd_row], al
    pop di
    pop si
    jmp cf_clear
.no:
    pop di
    pop si
    mov ax, 3                   ; DOS's 'path not found'
    jmp cf_set

; --- AH=47h: the current directory, INTO DS:SI, no drive and no leading \ ----
; That shape is DOS's own oddity and os88net puts both back; getting it wrong
; here would hide the fact that it does.
f_getcwd:
    push si
    push di
    push ds
    pop es                      ; the caller's segment, for the write
    mov di, si
    mov al, [cs:cwd_row]
    call path_to                ; ES:DI, root-relative
    pop di
    pop si
    mov ax, 0x0100              ; DOS leaves this in AX, and something might
    jmp cf_clear                ; one day read it

; --- AH=4Eh / 4Fh: findfirst / findnext --------------------------------------
; The wildcard is IGNORED. os88net only ever asks '*.*' (path_wild), so
; matching a pattern would be code the program under test cannot reach - and
; an unreachable branch that looks tested is what f_unimp exists to refuse.
; What IS honoured is the directory the path names, which is the part the
; program computes.
f_findfirst:
    push si
    push di
    mov si, dx
    call path_split             ; -> AL = the folder, cmpbuf = the last part
    jc  .no
    mov [cs:find_dir], al
    mov byte [cs:find_row], 0
    mov byte [cs:find_one], 0
    cmp byte [cs:cmpbuf], 0
    je  .walk                   ; a bare folder: everything in it
    call cmp_wild
    jc  .walk                   ; '*.*': likewise
    ; ONE NAMED FILE, which is how a size and an attribute are asked for.
    ; The walk below would answer with the folder's FIRST entry instead, and
    ; a stat that confidently describes the wrong file is worse than one that
    ; fails.
    mov si, dx
    call row_of
    jc  .no
    mov [cs:find_row], al
    mov byte [cs:find_one], 1
.walk:
    pop di
    pop si
    jmp f_findnext
.no:
    pop di
    pop si
    mov ax, 2                   ; 'file not found', which is also what DOS
    jmp cf_set                  ; answers for an EMPTY folder - and the server
                                ; must read that as a count of zero, not as a
                                ; refusal
f_findnext:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov al, [cs:find_row]
    xor ah, ah
    cmp byte [cs:find_one], 0   ; the single-name case: this row, once, and
    je  .scan                   ; then 'no more files'
    cmp byte [cs:find_one], 1
    jne .end
    mov byte [cs:find_one], 2
    call dr_row
    jmp short .fill
.scan:
    cmp ax, DR_N
    jae .end
    call dr_row                 ; AX -> BX
    mov cl, [cs:find_dir]
    cmp [cs:bx+DR_PAR], cl
    je  .hit
    inc ax
    jmp short .scan
.hit:
    inc ax
    mov [cs:find_row], al
.fill:
    mov es, [cs:dta_seg]        ; ...and fill the DTA the program set
    mov di, [cs:dta_off]
    mov al, [cs:bx+DR_ATTR]
    mov [es:di+21], al
    mov ax, [cs:bx+DR_SIZE]
    mov [es:di+26], ax
    mov ax, [cs:bx+DR_SIZE+2]
    mov [es:di+28], ax
    mov si, bx
    add si, DR_NAME
    add di, 30
    mov cx, 13
.nm:
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    loop .nm
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    jmp cf_clear
.end:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    mov ax, 18                  ; 'no more files'
    jmp cf_set

; --- dr_row: AX = a row index -> BX = its address ----------------------------
dr_row:
    push ax
    push dx
    mov bx, DR_SZ
    mul bx
    mov bx, ax
    add bx, dr_tab
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; path_split - the caller's DS:SI path -> AL = the PARENT row, cmpbuf = the
;              LAST component
; out: CF=1 = a component along the way was not a directory
;
; It drops 'X:' and a leading '\', walks every component but the last as a
; directory, and leaves the last one in cmpbuf for whoever asked - which is
; the whole reason it exists in this shape. The first version resolved the
; last component too, dropping it only when it held a WILDCARD, and that is
; fine for chdir and for `*.*` and wrong for everything else: a findfirst of
; `C:\GAMES\CHESS.EXE` - which is how a size and an attribute are asked for -
; looked for a DIRECTORY called CHESS.EXE and answered "no such path" about a
; file that was right there.
;
; cmpbuf comes back EMPTY when the path ends in a separator or is a bare
; drive, which is the caller's cue that the parent IS the answer.
; -----------------------------------------------------------------------------
path_split:
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, 0xFF                ; where we are: the root
    mov byte [cs:cmpbuf], 0
    cmp byte [si+1], ':'
    jne .noc
    inc si
    inc si
.noc:
    cmp byte [si], '\'
    jne .comp
    inc si
.comp:
    cmp byte [si], 0
    je  .done
    mov di, cmpbuf              ; one component into our own buffer, so the
    xor cx, cx                  ; comparison below is CS-relative on both
.cp:                            ; sides
    mov al, [si]
    or  al, al
    jz  .cend
    cmp al, '\'
    je  .cend
    cmp cx, 12
    jae .skipc
    mov [cs:di], al
    inc di
    inc cx
.skipc:
    inc si
    jmp short .cp
.cend:
    mov byte [cs:di], 0
    cmp byte [si], '\'
    jne .done                   ; ...the LAST one: leave it in cmpbuf
    inc si
    call dir_step               ; a middle component MUST be a directory
    jc  .no
    mov dl, al
    jmp short .comp
.done:
    mov al, dl
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dir_of - path_split, then enter the last component too. chdir's shape.
; out: AL = the row the whole path names; CF=1 = not a folder
; -----------------------------------------------------------------------------
dir_of:
    push dx
    call path_split
    jc  .no
    cmp byte [cs:cmpbuf], 0
    je  .done                   ; the path ended at a folder already
    mov dl, al
    call dir_step
    jc  .no
.done:
    pop dx
    clc
    ret
.no:
    pop dx
    stc
    ret

; -----------------------------------------------------------------------------
; row_of - path_split, then match the last component against ANY row
; out: AL = the row; CF=1 = no such name in that folder
;
; dir_step's sibling without the directory test, for open and for a findfirst
; that names one file.
; -----------------------------------------------------------------------------
row_of:
    push bx
    push cx
    push dx
    push si
    push di
    call path_split
    jc  .no
    cmp byte [cs:cmpbuf], 0
    je  .no                     ; a folder is not a file
    mov dl, al
    xor ax, ax
.l:
    cmp ax, DR_N
    jae .no
    call dr_row
    mov cl, [cs:bx+DR_PAR]
    cmp cl, dl
    jne .nx
    mov si, bx
    add si, DR_NAME
    mov di, cmpbuf
    call cs_eq
    jc  .hit
.nx:
    inc ax
    jmp short .l
.hit:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; dir_step - cmpbuf, a directory child of DL -> AL. CF=1 = no such child.
dir_step:
    push bx
    push cx
    push si
    push di
    xor ax, ax
.l:
    cmp ax, DR_N
    jae .no
    call dr_row
    test byte [cs:bx+DR_ATTR], 0x10
    jz  .nx
    mov cl, [cs:bx+DR_PAR]
    cmp cl, dl
    jne .nx
    mov si, bx
    add si, DR_NAME
    mov di, cmpbuf
    call cs_eq
    jc  .hit
.nx:
    inc ax
    jmp short .l
.hit:
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.no:
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret

; cmp_wild - does cmpbuf hold a '*' or a '?'. CF=1 = yes.
cmp_wild:
    push ax
    push si
    mov si, cmpbuf
.l:
    mov al, [cs:si]
    or  al, al
    jz  .no
    cmp al, '*'
    je  .yes
    cmp al, '?'
    je  .yes
    inc si
    jmp short .l
.yes:
    pop si
    pop ax
    stc
    ret
.no:
    pop si
    pop ax
    clc
    ret

; cs_eq - CS:SI vs CS:DI, both ASCIIZ, case-insensitive. CF=1 = equal.
cs_eq:
    push ax
    push bx
    push si
    push di
.l:
    mov al, [cs:si]
    mov bl, [cs:di]
    cmp al, 'A'
    jb  .nl
    cmp al, 'Z'
    ja  .nl
    add al, 0x20
.nl:
    cmp bl, 'A'
    jb  .nl2
    cmp bl, 'Z'
    ja  .nl2
    add bl, 0x20
.nl2:
    cmp al, bl
    jne .no
    or  al, al
    jz  .yes
    inc si
    inc di
    jmp short .l
.yes:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret
.no:
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; path_to - AL = a row -> ES:DI, the path from the root, NO leading backslash
; (int 21h 47h's shape). Walks UP onto the stack and builds down, os88net's
; own hd_path in miniature - which is fitting, since proving that routine is
; most of why this exists.
; -----------------------------------------------------------------------------
path_to:
    push ax
    push bx
    push cx
    push si
    xor cx, cx
.up:
    cmp al, 0xFF
    je  .base
    cmp al, DR_N
    jae .base
    push ax
    inc cx
    xor ah, ah
    call dr_row
    mov al, [cs:bx+DR_PAR]
    jmp short .up
.base:
    jcxz .done
.step:
    pop ax
    xor ah, ah
    call dr_row
    mov si, bx
    add si, DR_NAME
.nm:
    mov al, [cs:si]
    or  al, al
    jz  .nmd
    mov [es:di], al
    inc si
    inc di
    jmp short .nm
.nmd:
    dec cx
    jcxz .done
    mov byte [es:di], '\'
    inc di
    jmp short .step
.done:
    mov byte [es:di], 0
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; WRITING, on the synthetic tree (SPEC.md 62.10.4.5)
;
; THE TREE IS MUTABLE NOW AND THE FILES ARE STILL NOT STORED. A row's contents
; remain generated - byte i is (i + 7r) & 0xFF - and what a write changes is
; the row's SIZE and a per-row PHASE byte, so a file written and read back
; returns exactly what was put in it without this harness holding a single
; byte of it. That is the whole reason the pattern was chosen in phase 2: it
; is checkable from either end at no cost.
;
; What that CANNOT model is arbitrary content, so the checkable claim here is
; "the right number of bytes arrived and the metadata moved", and the byte-
; exact claim belongs to the os8088 side of the wire, where partner.py holds
; real blobs. Two harnesses, each honest about a different half.
; =============================================================================

; --- AH=3Ch: create or truncate ----------------------------------------------
f_create:
    push bx
    push si
    push ds
    mov si, dx
    call row_of                 ; the caller's DS:SI
    push cs
    pop ds
    jnc .have                   ; ...it exists: truncate it
    pop ds
    push ds
    mov si, dx
    call tree_add               ; ...or make a row for it
    push cs
    pop ds
    jc  .full
.have:
    mov [cs:fh_row], al
    xor ah, ah
    call dr_row
    mov word [cs:bx+DR_SIZE], 0     ; truncated: no bytes, and the next write
    mov word [cs:bx+DR_SIZE+2], 0   ; decides the phase
    mov word [cs:fpos], 0
    mov word [cs:fpos+2], 0
    pop ds
    pop si
    pop bx
    mov ax, FH
    jmp cf_clear
.full:
    pop ds
    pop si
    pop bx
    mov ax, 4                   ; 'too many open files' - the nearest DOS code
    jmp cf_set                  ; to "this stub's table is full"

; --- AH=41h: delete ----------------------------------------------------------
f_unlink:
    push bx
    push si
    push ds
    mov si, dx
    call row_of
    push cs
    pop ds
    jc  .no
    xor ah, ah
    call dr_row
    test byte [cs:bx+DR_ATTR], 0x10
    jnz .isdir
    mov byte [cs:bx+DR_PAR], DR_GONE
    pop ds
    pop si
    pop bx
    jmp cf_clear
.isdir:
    pop ds
    pop si
    pop bx
    mov ax, 5                   ; access denied: 41h does not remove folders
    jmp cf_set
.no:
    pop ds
    pop si
    pop bx
    mov ax, 2
    jmp cf_set

; --- AH=39h: mkdir -----------------------------------------------------------
f_mkdir:
    push bx
    push si
    push ds
    mov si, dx
    call row_of
    push cs
    pop ds
    jnc .exists
    pop ds
    push ds
    mov si, dx
    call tree_add
    push cs
    pop ds
    jc  .full
    xor ah, ah
    call dr_row
    mov byte [cs:bx+DR_ATTR], 0x10
    pop ds
    pop si
    pop bx
    jmp cf_clear
.exists:
    pop ds
    pop si
    pop bx
    mov ax, 80                  ; already exists
    jmp cf_set
.full:
    pop ds
    pop si
    pop bx
    mov ax, 3
    jmp cf_set

; --- AH=3Ah: rmdir -----------------------------------------------------------
; EMPTINESS IS TESTED, because the kernel leaves that entirely to this side
; (dskw_rmbody: "only the side holding the directory can answer it") and a
; stub that removed an occupied folder would be testing the wrong contract.
f_rmdir:
    push bx
    push cx
    push si
    push ds
    mov si, dx
    call row_of
    push cs
    pop ds
    jc  .no
    xor ah, ah
    mov cl, al                  ; CL = the row, for the child scan
    call dr_row
    test byte [cs:bx+DR_ATTR], 0x10
    jz  .notdir
    call dir_empty              ; CL's children. CF=1 = it has some
    jc  .full
    mov byte [cs:bx+DR_PAR], DR_GONE
    pop ds
    pop si
    pop cx
    pop bx
    jmp cf_clear
.notdir:
.full:
    pop ds
    pop si
    pop cx
    pop bx
    mov ax, 5                   ; access denied, which is what DOS answers for
    jmp cf_set                  ; both "not a directory" and "not empty"
.no:
    pop ds
    pop si
    pop cx
    pop bx
    mov ax, 3
    jmp cf_set

; dir_empty - CL = a row. CF=1 = something still names it as a parent.
dir_empty:
    push ax
    push bx
    xor ax, ax
.l:
    cmp ax, DR_N
    jae .yes
    call dr_row
    cmp byte [cs:bx+DR_PAR], cl
    je  .no
    inc ax
    jmp short .l
.yes:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; --- AH=56h: rename. DS:DX = the old name, ES:DI = the new one. --------------
f_rename:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov si, dx
    call row_of                 ; the OLD name, in the caller's DS
    jc  .no
    mov [cs:ren_row], al
    pop es
    push es
    push ds
    mov ax, es                  ; ...and the NEW one, in ES:DI
    mov ds, ax
    mov si, di
    call row_of                 ; it must NOT exist
    pop ds
    jnc .exists
    mov si, di                  ; take just its last component
    mov ax, es
    mov ds, ax
    call last_comp              ; -> cmpbuf, CS-relative
    push cs
    pop ds
    mov al, [cs:ren_row]
    xor ah, ah
    call dr_row
    mov si, cmpbuf              ; ...over the row's name
    mov di, bx
    add di, DR_NAME
    call cs_move
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    jmp cf_clear
.exists:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, 80
    jmp cf_set
.no:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, 2
    jmp cf_set

; cs_move - CS:SI -> CS:DI, up to 13 bytes including the NUL
cs_move:
    push ax
    push cx
    mov cx, 13
.l:
    mov al, [cs:si]
    mov [cs:di], al
    or  al, al
    jz  .out
    inc si
    inc di
    loop .l
.out:
    pop cx
    pop ax
    ret

; last_comp - the caller's DS:SI path -> cmpbuf, the part after the last '\'
last_comp:
    push ax
    push di
    push si
    mov di, si                  ; DI walks to the end, then back to a '\'
.end:
    cmp byte [si], 0
    je  .found
    cmp byte [si], '\'
    jne .nx
    mov di, si
    inc di
.nx:
    inc si
    jmp short .end
.found:
    mov si, di
    mov di, cmpbuf
.cp:
    mov al, [si]
    mov [cs:di], al
    or  al, al
    jz  .done
    inc si
    inc di
    jmp short .cp
.done:
    pop si
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; tree_add - the caller's DS:SI path -> a NEW row. AL = it; CF=1 = table full
;
; It reuses a DR_GONE row before appending, so a session that creates and
; deletes in a loop does not run the table out - which is exactly what a copy
; test does.
; -----------------------------------------------------------------------------
tree_add:
    push bx
    push cx
    push dx
    push di
    call path_split             ; -> AL = the parent, cmpbuf = the new name
    jc  .no
    cmp byte [cs:cmpbuf], 0
    je  .no
    mov dl, al                  ; the parent, banked
    xor ax, ax
.scan:
    cmp ax, DR_N
    jae .no
    call dr_row
    cmp byte [cs:bx+DR_PAR], DR_GONE
    je  .got
    inc ax
    jmp short .scan
.got:
    mov [cs:bx+DR_PAR], dl
    mov byte [cs:bx+DR_ATTR], 0
    mov word [cs:bx+DR_SIZE], 0
    mov word [cs:bx+DR_SIZE+2], 0
    mov si, cmpbuf
    mov di, bx
    add di, DR_NAME
    call cs_move
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret

; --- anything else: SAY SO AND STOP ------------------------------------------
; Never a plausible zero. A stub that quietly succeeds at a call it does not
; implement is a harness that has started lying about the thing under test.
f_unimp:
    push cs
    pop ds
    mov al, ah
    push ax
    mov si, s_unimp
    call puts
    pop ax
    call puthex8
    mov si, s_halt
    call puts
    jmp hang

; --- the CF answer, written into the stacked FLAGS ---------------------------
cf_clear:
    push bp
    mov bp, sp
    and word [bp+6], 0xFFFE
    pop bp
    iret

cf_set:
    push bp
    mov bp, sp
    or  word [bp+6], 1
    pop bp
    iret

; =============================================================================
; helpers the handlers share
; =============================================================================
; clamp - CX = how many of the requested bytes are inside the real DSF_KB
; in:  CX = wanted, [cs:fpos] = position;  out: CX = allowed
clamp:
    push ax
    cmp word [cs:fpos+2], 0
    jne .zero                   ; past the real part: none of it is
    mov ax, DSF_KB * 1024
    sub ax, [cs:fpos]
    jbe .zero
    cmp cx, ax
    jbe .out
    mov cx, ax
    jmp short .out
.zero:
    xor cx, cx
.out:
    pop ax
    ret

; tclamp - the same for a TREE row: CX down to what is left of [fh_row]
; in:  CX = wanted, [fh_row], [fpos];  out: CX = allowed
;
; Every row here is under 64KB, so a position with a high word is past the end
; of any of them and the whole answer is zero - which is also the honest thing
; for a seek that overshot.
tclamp:
    push ax
    push bx
    push dx
    mov al, [cs:fh_row]
    xor ah, ah
    call dr_row                 ; -> BX
    mov ax, [cs:bx+DR_SIZE]     ; what is LEFT, in 32 bits: `size - pos` with
    mov dx, [cs:bx+DR_SIZE+2]   ; a borrow, because either can exceed a word
    sub ax, [cs:fpos]
    sbb dx, [cs:fpos+2]
    jc  .zero                   ; the position is past the end
    or  dx, dx
    jnz .out                    ; over 64KB left: nothing CX can ask for is
    or  ax, ax                  ; outside it
    jz  .zero
    cmp cx, ax
    jbe .out
    mov cx, ax
    jmp short .out
.zero:
    xor cx, cx
.out:
    pop dx
    pop bx
    pop ax
    ret

; fadv - advance [cs:fpos] by AX, AX preserved
fadv:
    push ax
    add [cs:fpos], ax
    adc word [cs:fpos+2], 0
    pop ax
    ret

term:
    push cs
    pop ds
    push ax
    mov si, s_term
    call puts
    pop ax
    call puthex8
    mov si, s_halt
    call puts
hang:
    hlt
    jmp short hang

; =============================================================================
; Console, through the BIOS - nothing here may use int 21h
; =============================================================================
putc:
    push ax
    push bx
    push bp
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop bp
    pop bx
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

; farputs - the same string walk, through the CALLER's DS
farputs:
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

puthex8:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
    push ax
    and al, 0x0F
    call .nib
    pop ax
    pop cx
    pop ax
    ret
.nib:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    call putc
    ret

%ifdef PKTFAKE
; =============================================================================
; A PACKET DRIVER THAT IS NOT ONE (make dosstub PKTFAKE=1)
;
; Enough of the FTP Software interface for drivers/net/pktdrv.inc to find it,
; claim it and read a station address off it: the signature, driver_info,
; access_type, get_address, send_pkt and release_type. It puts nothing on any
; wire - there is no wire - so a stack over it gets no answer to anything and
; DHCP times out, which is the point: what this proves is the half that is
; ours (the probe, the class check, the handle, the MAC) on the machine the
; program will really run on.
;
; **IT IS A KNOB AND NOT THE DEFAULT** for the stub's own reason: a harness
; that answers where the real thing would not is one that hides the failure
; it exists to find, and a plain `make dosstub` must still show /N refusing.
; =============================================================================
PKT_VEC     equ 0x60

pkt_install:
    push ax
    push bx
    push dx
    push ds
    xor ax, ax
    mov ds, ax
    mov bx, PKT_VEC * 4
    mov word [bx], pkt_entry
    mov ax, cs
    mov [bx + 2], ax
    pop ds
    pop dx
    pop bx
    pop ax
    ret

; The signature the spec puts at offset 3 of the handler, which is why the
; three bytes in front of it are a jump over it.
pkt_entry:
    nop                         ; **THREE BYTES, because the signature is at
    jmp short pkt_go            ; OFFSET 3** and `jmp short` is two of them -
    db 'PKT DRVR', 0            ; a real driver gets there with a near jump
pkt_go:
    cmp ah, 1
    je  pkt_info
    cmp ah, 2
    je  pkt_access
    cmp ah, 3
    je  pkt_release
    cmp ah, 4
    je  pkt_send
    cmp ah, 6
    je  pkt_addr
    stc                         ; anything else: BAD_COMMAND, which is what a
    mov dh, 2                   ; driver answers rather than pretending
    jmp short pkt_ret
pkt_info:
    mov bx, 0x0109              ; version
    mov ch, 1                   ; class 1: Ethernet - what the probe checks
    mov dx, 1                   ; type
    mov cl, 0                   ; number
    mov al, 2                   ; functionality: basic
    clc
    jmp short pkt_ret
pkt_access:
    mov ax, 0x4242              ; a handle, and the probe only keeps it
    clc
    jmp short pkt_ret
pkt_release:
    clc
    jmp short pkt_ret
pkt_send:
    clc                         ; taken, and dropped on the floor
    jmp short pkt_ret
pkt_addr:
    push si
    push di
    push cx
    mov si, pkt_mac
    cld
.c:
    jcxz .done
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jmp short .c
.done:
    pop cx
    pop di
    pop si
    clc
pkt_ret:
    retf 2                      ; **retf 2, NOT iret**: the packet driver
                                ; returns its CARRY to the caller, and an
                                ; iret would put the flags back the way they
                                ; were and lose it
pkt_mac:    db 0x52,0x54,0x00,0x12,0x34,0x56
%endif

; =============================================================================
s_banner:   db 13,10,'dosstub: just enough DOS to run a .COM',13,10,0
s_go:       db 'dosstub: handing over',13,10,13,10,0
s_opened:   db ' <- open',13,10,0
s_nofile:   db ' <- NOT FOUND (FAILOPEN)',13,10,0
s_term:     db 13,10,13,10,'dosstub: program exited, AL=',0
s_unimp:    db 13,10,13,10,'dosstub: UNIMPLEMENTED int 21h AH=',0
s_halt:     db 13,10,0

; The command tail, as DOS builds it: a length byte, then the text, and this
; stub appends the CR. Argument parsing is code nothing else in this tree
; executes, so the tail is a build knob rather than a constant.
cmdtail_txt:
%ifdef ARGS
    db ARGS
%endif
    db 0

xfer:       dw 0
fpos:       dd 0

; --- the directory half's state ----------------------------------------------
dta_seg:    dw 0
dta_off:    dw 0
cwd_row:    db 0xFF             ; the root
find_dir:   db 0xFF             ; the folder the walk in progress is in
find_row:   db 0                ; ...and how far through dr_tab it has got
ram_name:   db 'IMAGE',0       ; ...the ONE name that reaches the RAM file
ren_row:    db 0                ; the row 56h is renaming
find_one:   db 0                ; 0 = enumerate the folder, 1 = one named row
                                ; still to hand over, 2 = and it has been
fh_row:     db 0xFF             ; the open handle's tree row, FF = the RAM file
cmpbuf:     times 16 db 0       ; one path component, staged into CS so both
                                ; sides of a comparison are CS-relative

    align 2
com_image:
%ifndef COMFILE
%define COMFILE "build/os88net.com"
%endif
    incbin COMFILE
com_len     equ $ - com_image

    times 512 db 0
stack_top:
