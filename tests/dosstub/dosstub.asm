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
;   make dosstub ARGS='/I:X'         10MB, the old ordinary path (20,480 secs)
;   make dosstub FSIZE=64M ARGS='/I:X'  past os8088's cap: 65,535, and it says so
;   make dosstub FSIZE=256 ARGS='/I:X'  under one sector: the refusal
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
f_open:
    push si
    push ds
    mov si, dx
    call farputs                ; DS:SI, still the caller's segment
    push cs
    pop ds
%ifdef FAILOPEN
    mov si, s_nofile            ; ...and the OTHER answer, which the program
    call puts                   ; has an error path for that nothing else here
    pop ds                      ; can reach: DOS says no.
    pop si
    mov ax, 2                   ; file not found
    jmp cf_set
%else
    mov si, s_opened
    call puts
    pop ds
    pop si
    mov word [cs:fpos], 0
    mov word [cs:fpos+2], 0
    mov ax, FH
    jmp cf_clear
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
    call clamp
    mov [cs:xfer], cx
    jcxz .none
    mov si, dx                  ; the caller's DS:DX is the SOURCE
    mov ax, DSF_SEG
    mov es, ax
    mov di, [cs:fpos]
    rep movsb
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
    mov ax, FSIZE_LO
    add ax, dx
    mov [cs:fpos], ax
    mov ax, FSIZE_HI
    adc ax, cx
    mov [cs:fpos+2], ax
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
    call dir_of                 ; the path, minus its last component
    jc  .no
    mov [cs:find_dir], al
    mov byte [cs:find_row], 0
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
; dir_of - the caller's DS:SI path -> AL = the row it names (0xFF = the root)
; out: CF=1 = no such folder
;
; It drops 'X:', a leading '\', and a LAST COMPONENT CONTAINING A WILDCARD -
; which is what lets findfirst and chdir share it. Anything else in the path
; has to match a directory row.
; -----------------------------------------------------------------------------
dir_of:
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, 0xFF                ; where we are: the root
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
    jne .last                   ; the LAST component: a wildcard in it means
    inc si                      ; it is findfirst's '*.*' and not a folder
    call dir_step
    jc  .no
    mov dl, al
    jmp short .comp
.last:
    call cmp_wild
    jc  .done                   ; '*.*' - we are already standing in it
    call dir_step
    jc  .no
    mov dl, al
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
