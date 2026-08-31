; =============================================================================
; bootdiag - why does THIS BIOS answer `Disk error` when os8088 boots?
;            (SPEC.md 2.9.10)
;
; A handful of 86Box BIOSes have refused to boot this OS since the first
; commit, and every one of them says the same eleven characters and stops. The
; string is printed from three places, the loader has been rewritten four
; times underneath it, and none of that narrows anything: `Disk error` means
; "int 13h set carry three times running" and nothing else.
;
; So this asks the machine directly. It walks the boot loader's own sequence
; one step at a time and reports what each step actually did - what the BIOS
; handed over, what its diskette parameter table says, whether the memory the
; loader relocates into is there and stays there, and what int 13h answers for
; each of the eight read shapes the loader uses. Every read is checked against
; the DATA, not against the carry flag, because SPEC.md 18.91/18.93.1 are two
; separate cases of a BIOS answering CF=0 for a transfer it did not do.
;
; It is built three ways from this one source:
;
;   bootdiag.img    BOOTABLE, behind tests/bootdiag/bdboot.asm - a loader that
;                   depends on NONE of the things under test (one sector an
;                   int 13h, no relocation, no int 1Eh patch). If the machine
;                   can read a single sector, this boots.
;   bootdiagx.img   BOOTABLE, behind the SHIPPED boot/boot.asm - which does
;                   relocate, does patch the table and does read whole tracks.
;                   THE PAIR IS THE EXPERIMENT: identical payload, and the
;                   only difference is the loader. One booting and the other
;                   not is a one-bit answer that no amount of reading gives.
;   BOOTDIAG.COM    a DOS program, for a machine that boots DOS and not this.
;                   `BOOTDIAG > BD.TXT` captures the report, and it tells you
;                   what the ROM does on a machine where the ROM is provably
;                   fine. The memory tests that would scribble on DOS are
;                   skipped and say so.
;
; NOTHING HERE WRITES TO A DISK. Every int 13h is AH=00h, 02h, 08h or 15h.
;
; Assemble: nasm -f bin -DCOMFILE -DSECS=<n> -o bootdiag.com bootdiag.asm
;           nasm -f bin          -DSECS=<n> -o bootdiag.bin bootdiag.asm
; =============================================================================

    cpu 8086                    ; SPEC.md 1, and here for a second reason: the
                                ; machines this is aimed at are the oldest
                                ; ROMs 86Box has

%ifdef COMFILE
    org 0x100
%else
    org 0                       ; loaded at 0800:0000 by bdboot.asm, or at
%endif                          ; KERNEL_SEG:0000 by boot/boot.asm

%ifndef SECS
%define SECS 48                 ; payload sectors, injected by the Makefile
%endif
%ifndef STAMP0
%define STAMP0 16               ; ...of which the first STAMP0 are THIS CODE
%endif                          ; and the rest carry nothing but their own
                                ; index, in every word (tests/rdiag.asm's
                                ; trick). The split is what makes a read
                                ; CHECKABLE: a sector holding its own number
                                ; proves the transfer, where a sector holding
                                ; code proves only that the code is code. The
                                ; Makefile pads to STAMP0, stamps from there,
                                ; and FAILS THE BUILD if the code outgrows it
                                ; - which is the one way this could go quietly
                                ; wrong, every verified read below silently
                                ; becoming a comparison against a routine.

HAND_AT     equ 0x0010          ; bdboot.asm's handover record
HAND_MAGIC  equ 0x4442          ; 'BD' - present only when bdboot loaded us
KERNEL_SEG  equ 0x0060          ; where boot/boot.asm puts the kernel
BOOT2_SECS  equ 11              ; ...and how many sectors of blob precede it
                                ; (kernel/kernel.asm's own constant, mirrored
                                ; for the stage-1 replay in section 7)
RELOC_ADJ   equ 0x07E0          ; boot/boot.asm's, exactly
BOOT_STACK  equ 2048
STAGE2_ADJ  equ 0x07C0 - BOOT_STACK/16 - BOOT2_SECS*32
DPT_AT      equ 0x0580          ; the address stage 1 parks its patched
                                ; diskette parameter table at
SCR_SECS    equ 16              ; sectors the read buffer holds - 8KB, which
                                ; is the widest run any test below asks for
SCR_ADJ     equ SECS*32 + 64    ; ...and where it starts: DERIVED from the
                                ; image rather than written down, because the
                                ; first version had 0x540 as a constant beside
                                ; a 48-sector image that is 0x600 paragraphs
                                ; long - so the buffer and the stack were
                                ; INSIDE the image, and the stack was growing
                                ; down through the stamped sectors. It worked,
                                ; because nothing reads the image back; it
                                ; would have stopped working the day something
                                ; did. 64 paragraphs is the 1KB of stack that
                                ; sits between the two
PROBE_DEPTH equ 4096            ; how far BELOW stage 1's SP the stack probe
                                ; fills, which is TWICE what stage 1 reserves.
                                ; Measuring only the reserved 2,048 answers
                                ; "did it fit" and cannot answer "by how much
                                ; did it not"
PAGELEN     equ 22              ; lines before "-- more --" on a 25-row screen,
                                ; leaving two for a line that wraps and one
                                ; for the prompt itself

%ifndef COMFILE
; -----------------------------------------------------------------------------
; The shipped loader's handoff contract (boot/boot.asm), so bootdiagx.img can
; carry this as a FLAT_PAYLOAD: far-jump to 0x0000 with DL = the boot drive, a
; far call to 0x0008 for the splash tick, and its t=0 word at 0x000C.
; -----------------------------------------------------------------------------
    jmp main
    times 0x08 - ($ - $$) db 0
    retf                        ; 0x0008 - no splash here, so it just returns
    times 0x0C - ($ - $$) db 0
    dw 0                        ; 0x000C - the boot timer's t=0
    times HAND_AT - ($ - $$) db 0
                                ; ...and bdboot.asm's record HAS to land on
                                ; HAND_AT, which it has as a constant. A byte
                                ; added above here would otherwise hand the
                                ; report somebody else's bytes and be silent
                                ; about it; a negative TIMES fails the build.
                                ; `%if hand != HAND_AT` is NOT the check - the
                                ; preprocessor runs before any label exists and
                                ; reads the name as zero, so it fires always
hand:                           ; 0x0010
    times 16 db 0               ; ASSEMBLED ZERO: the magic is written at run
                                ; time, so an image the shipped loader carried
                                ; has none
%else
    jmp main
hand:
    times 16 db 0
%endif

; =============================================================================
main:
%ifndef COMFILE
    mov ax, cs                  ; own every segment, and stand in the 1KB
    cli                         ; between the end of our image and the read
    mov ss, ax                  ; buffer above it
    mov sp, SCR_ADJ*16 - 2
    sti
    mov ds, ax
%else
    push cs
    pop ds
%endif
    mov [d_bootdl], dl          ; BOTH loaders leave DL = the boot drive, and
                                ; it is the first thing a wrong one destroys
    cld
    call cls

    ; --- which loader carried us, and what it saw ---------------------------
    mov ax, [hand]
    cmp ax, HAND_MAGIC
    jne .noband
    mov byte [d_loader], 1
    mov al, [hand + 2]
    mov [d_bootdl], al          ; bdboot's record beats the register: it is
    mov al, [hand + 3]          ; what the BIOS handed over BEFORE any fallback
    mov [d_usedl], al
    mov al, [hand + 4]
    mov [d_ldst], al
    mov al, [hand + 5]
    mov [d_ldretry], al
    mov ax, [hand + 6]
    mov [g_spt], ax
    mov ax, [hand + 8]
    mov [g_heads], ax
    mov ax, [hand + 10]
    mov [g_lba0], ax
    mov ax, [hand + 12]
    mov [g_secs], ax
    mov al, [hand + 14]
    mov [d_fellback], al
    jmp short .havehand
.noband:
    mov al, [d_bootdl]          ; no record: the shipped stage 1 carried us, or
    mov [d_usedl], al           ; this is the .COM under DOS. Either way the
                                ; geometry has to come off the disk itself
%ifdef COMFILE
    mov byte [d_loader], 2
    mov byte [d_bootdl], 0      ; DOS's DL is not the BIOS's handover, so it
    mov byte [d_usedl], 0       ; says nothing - and the unit under test is
    call com_drive              ; A: unless the command tail names another
%endif
    call geom_from_bpb
.havehand:

    call scratch_seg            ; AX = the scratch segment, computed from CS
    mov [d_scr], ax

    call sec_machine
    call sec_handover
    call sec_dpt
    call sec_drive
    call sec_reads
    call sec_memory
    call sec_replay
    call sec_verdict

%ifdef COMFILE
    mov ax, 0x4C00
    int 0x21
%else
    mov si, s_done
    call puts
    call getkey
    int 0x19                    ; back to the BIOS bootstrap
%endif

; =============================================================================
; [1] MACHINE - which ROM is this, and what does it claim about the machine
; =============================================================================
sec_machine:
    mov si, s_h1
    call puts

    ; --- the two bytes every PC-compatible ROM has --------------------------
    mov si, s_biosdate
    call puts
    mov ax, 0xF000
    mov es, ax
    mov si, 0xFFF5              ; mm/dd/yy, eight characters, since 1981
    mov cx, 8
.date:
    mov al, [es:si]
    call putprint
    inc si
    loop .date
    mov si, s_model
    call puts
    mov al, [es:0xFFFE]         ; FF=PC, FE=XT, FC=AT, F8=PS/2 model 80...
    call puthex8
    push ds                     ; ...and int 15h AH=C0h, which an XT ROM does
    push es                     ; not have. Its absence is itself a finding
    mov ah, 0xC0
    stc
    int 0x15
    jc .noc0
    or ah, ah
    jnz .noc0
    mov ax, [es:bx + 2]         ; model, submodel - STASHED HERE, because a
    mov [cs:d_c0mm], ax         ; ROM that eats DS across this call would
    mov ax, [es:bx + 4]         ; otherwise have `puts` reading its strings
    mov [cs:d_c0rf], ax         ; out of whatever segment it left behind
    pop es
    pop ds
    mov si, s_sub
    call puts
    mov al, [d_c0mm]
    call puthex8
    call putsp
    mov al, [d_c0mm + 1]
    call puthex8
    call putsp
    mov al, [d_c0rf + 1]        ; feature byte 1 - bit 6 is "slave PIC"
    call puthex8
    jmp short .c0done
.noc0:
    pop es
    pop ds
    mov si, s_noc0
    call puts
.c0done:
    call crlf

    ; --- and the first long printable run in the ROM, which names it --------
    ; Every ROM in 86Box carries a copyright or a vendor line somewhere in
    ; F000, and it is the one string that tells the two of us we are talking
    ; about the same machine. 24 printable characters running is the filter;
    ; 46 of them get printed.
    mov si, s_ident
    call puts
    mov ax, 0xF000
    mov es, ax
    xor si, si
    xor cx, cx                  ; CX = length of the run ending at SI
.scan:
    mov al, [es:si]
    cmp al, 0x20
    jb .break
    cmp al, 0x7E
    ja .break
    inc cx
    cmp cx, 24
    je .found
    jmp short .step
.break:
    xor cx, cx
.step:
    inc si
    or si, si                   ; wrapped past FFFF: nothing in this ROM
    jnz .scan
    mov si, s_noident
    call puts
    jmp short .identdone
.found:
    sub si, 23                  ; back to the run's first character
    mov cx, 46
.put:
    mov al, [es:si]
    cmp al, 0x20
    jb .identdone
    cmp al, 0x7E
    ja .identdone
    call putc
    inc si
    loop .put
.identdone:
    push cs
    pop es
    call crlf

    ; --- CPU tier, because SPEC.md 18.93.2's run bound turns on it ----------
    mov si, s_cpu
    call puts
    call cpu_tier
    mov al, [d_cpu]
    mov si, s_cpu86
    or al, al
    jz .cpuput
    mov si, s_cpu286
    cmp al, 2
    jb .cpuput
    mov si, s_cpu386
.cpuput:
    call puts
    call crlf

    ; --- how much RAM the BIOS says there is, and what it is equipped with --
    mov si, s_mem
    call puts
    int 0x12
    mov [d_kb], ax
    call putdec16
    mov si, s_kb
    call puts
    int 0x11
    mov [d_equip], ax
    call puthex16
    mov si, s_fdd
    call puts
    mov ax, [d_equip]
    test al, 1                  ; bit 0 - any floppy drives at all
    jz .nofdd
    mov cl, 6
    shr ax, cl
    and ax, 3
    inc ax                      ; bits 7:6 are (drives - 1)
    call putdec16
    jmp short .vid
.nofdd:
    mov al, '0'
    call putc
.vid:
    mov si, s_vid
    call puts
    mov ax, [d_equip]
    mov cl, 4
    shr ax, cl
    and ax, 3                   ; bits 5:4 - 01 CGA 40, 10 CGA 80, 11 mono
    call putdec16
    call crlf
    ret

; =============================================================================
; [2] HANDOVER - the four bytes the loader is handed, and what it did with them
; =============================================================================
sec_handover:
    mov si, s_h2
    call puts

    mov si, s_ldr
    call puts
    mov al, [d_loader]
    mov si, s_ldrx
    or al, al
    jz .ldrput
    mov si, s_ldrb
    cmp al, 1
    je .ldrput
    mov si, s_ldrd
.ldrput:
    call puts
    call crlf

    mov si, s_dl
    call puts
    mov al, [d_bootdl]
    call puthex8
    mov si, s_dlused
    call puts
    mov al, [d_usedl]
    call puthex8
    cmp byte [d_fellback], 0
    je .dlok
    mov si, s_dlfell            ; THE finding, when it fires: a ROM that does
    call puts                   ; not set DL at the boot jump makes every read
    jmp short .dldone           ; of every loader fail identically
.dlok:
    mov al, [d_bootdl]
    cmp al, 0x80
    jb .dlshipped
    mov si, s_dlhd              ; DL >= 80h on a floppy boot is the same fault
    call puts                   ; wearing a different hat
.dlshipped:
    cmp byte [d_loader], 0      ; ...and on the bootdiagx build this number is
    jne .dldone                 ; NOT what the BIOS said: SPEC.md 2.9.11's
    call crlf                   ; range check has already been over it, so a
    mov si, s_dlclamp           ; ROM that set no DL at all reports 00 here and
    call puts                   ; looks like a ROM that set it correctly
.dldone:
    call crlf

    mov si, s_load
    call puts
    mov ax, [g_secs]
    call putdec16
    mov si, s_loadsec
    call puts
    mov al, [d_ldretry]
    call puthex8
    mov si, s_loadst
    call puts
    mov al, [d_ldst]
    call puthex8
    call crlf

    mov si, s_geom
    call puts
    mov ax, [g_spt]
    call putdec16
    mov si, s_geomh
    call puts
    mov ax, [g_heads]
    call putdec16
    mov si, s_geoml
    call puts
    mov ax, [g_lba0]
    call putdec16
    call crlf
    ret

; =============================================================================
; [3] THE DISKETTE PARAMETER TABLE - int 1Eh, and byte 4 of it
;
; SPEC.md 18.92: int 1Eh is a POINTER to an 11-byte table the BIOS re-reads on
; every floppy operation, and byte 4 is EOT - the last sector number the FDC
; may touch on a track. The IBM PC and XT ROMs say 8 and a 360KB disk has
; nine, so every DOS since 1982 replaces the table at boot and so does os8088.
; The three questions here are the three ways that can go wrong on a ROM we
; have not met: what does it say, does it STAY replaced, and is 0000:0580 -
; where os8088 parks its copy - actually free memory on this machine.
; =============================================================================
sec_dpt:
    mov si, s_h3
    call puts

    mov si, s_dptown            ; as booted - which on the bootdiagx build is
                                ; ALREADY OURS, stage 1 having installed it
                                ; before this code ran. Section 2 says which
                                ; loader, so the two together are unambiguous
                                ; where "the ROM's own" would have been wrong
    call puts
    call dpt_show
    call dpt_bytes

    ; --- install ours exactly where stage 1 does, then look again -----------
    push ds
    xor ax, ax
    mov es, ax
    mov di, DPT_AT
    lds si, [es:0x0078]
    mov cx, 11
    rep movsb
    pop ds
    mov al, [g_spt]
    mov [es:DPT_AT + 4], al     ; EOT = this disk's sectors per track
    mov word [es:0x0078], DPT_AT
    mov word [es:0x007A], 0

    mov word [d_pat], 0x5A5A    ; a pattern in the bytes stage 1 does not use,
    xor ax, ax                  ; so a reset that walks over 0000:0580 shows up
    mov es, ax                  ; as a changed word rather than as a wrong read
    mov word [es:DPT_AT + 11], 0x5A5A

    xor ah, ah                  ; a controller reset - which is where a BIOS
    mov dl, [d_usedl]           ; that keeps its own table puts it back
    int 0x13
    mov [d_rstst], ah
    call rd_boot                ; ...and one real read through it

    mov si, s_dptours
    call puts
    call dpt_show
    call dpt_bytes

    ; --- did our patch survive it? ------------------------------------------
    mov si, s_dptrst
    call puts
    mov al, [d_rstst]
    call puthex8
    mov si, s_dptkept
    call puts
    xor ax, ax
    mov es, ax
    mov si, s_no
    cmp word [es:0x0078], DPT_AT
    jne .kept
    cmp word [es:0x007A], 0
    jne .kept
    mov al, [g_spt]
    cmp [es:DPT_AT + 4], al
    jne .kept
    mov si, s_yes
    mov byte [d_dptkept], 1
.kept:
    call puts
    mov si, s_dptscr
    call puts
    mov si, s_no
    cmp word [es:DPT_AT + 11], 0x5A5A
    jne .scr
    mov si, s_yes
    mov byte [d_scrok], 1
.scr:
    call puts
    push cs
    pop es
    call crlf
    ret

; dpt_show - the int 1Eh vector, and whether it points at ROM or at RAM
dpt_show:
    mov si, s_dptvec
    call puts
    xor ax, ax
    mov es, ax
    mov ax, [es:0x007A]
    call puthex16
    mov al, ':'
    call putc
    mov ax, [es:0x0078]
    call puthex16
    mov ax, [es:0x007A]
    mov si, s_inrom
    cmp ax, 0xC000              ; anything at or above C000 is adapter or
    jae .where                  ; system ROM on every machine we care about
    mov si, s_inram
.where:
    call puts
    push cs
    pop es
    ret

; dpt_bytes - the eleven bytes, with EOT called out
dpt_bytes:
    mov si, s_dptb
    call puts
    push ds
    xor ax, ax
    mov es, ax
    lds si, [es:0x0078]
    mov cx, 11
.b:
    mov al, [si]
    push cx
    push si
    call puthex8
    call putsp
    pop si
    pop cx
    inc si
    loop .b
    pop ds
    push ds
    xor ax, ax
    mov es, ax
    lds si, [es:0x0078]
    mov al, [si + 4]
    pop ds
    mov [d_eot], al
    call crlf
    mov si, s_eot
    call puts
    mov al, [d_eot]
    call puthex8
    mov al, [g_spt]
    cmp [d_eot], al
    je .ok
    mov si, s_eotbad            ; the 1982 default, on a 1986 disk
    call puts
.ok:
    call crlf
    push cs
    pop es
    ret

; =============================================================================
; [4] DRIVE - what the BIOS thinks is in the machine
; =============================================================================
sec_drive:
    call newpage
    mov si, s_h4
    call puts

    mov si, s_p8
    call puts
    push ds
    push es
    mov ah, 0x08
    mov dl, [d_usedl]
    xor di, di                  ; some ROMs write ES:DI; give them a safe one
    mov es, di
    stc
    int 0x13
    mov [cs:d_p8st], ah
    jc .p8bad
    mov [cs:d_p8cx], cx
    mov [cs:d_p8dx], dx
    mov [cs:d_p8bl], bl
    pop es
    pop ds
    mov al, [d_p8st]
    call puthex8
    mov si, s_p8c
    call puts
    mov ax, [d_p8cx]            ; CH IS THE LAST CYLINDER, NOT HOW MANY: the
    mov al, ah                  ; field report read `cyls 79 heads 1` off a
    xor ah, ah                  ; drive with 80 and 2 of them, because two of
    inc ax                      ; these four are maxima and two are counts and
    call putdec16               ; all four were printed as counts. CL bits 7:6
                                ; are the cylinder's high two, and they are a
                                ; FIXED-disk field: every AH=08h here is asked
                                ; about a floppy unit, so they are always 0
    mov si, s_p8h
    call puts
    mov ax, [d_p8dx]            ; ...and DH is the last HEAD, same thing
    mov al, ah
    xor ah, ah
    inc ax
    call putdec16
    mov si, s_p8s
    call puts
    mov ax, [d_p8cx]            ; CL bits 5:0 ARE a count, and stay one
    and al, 0x3F
    xor ah, ah
    call putdec16
    mov si, s_p8n
    call puts
    mov al, [d_p8dx]
    xor ah, ah
    call putdec16
    mov si, s_p8t
    call puts
    mov al, [d_p8bl]
    call puthex8
    jmp short .p8done
.p8bad:
    pop es
    pop ds
    mov al, [d_p8st]
    call puthex8
    mov si, s_p8no              ; an original PC/XT ROM has no AH=08h at all,
    call puts                   ; and that is a fact rather than a fault
.p8done:
    call crlf

    mov si, s_p15
    call puts
    push ds
    push es
    mov ah, 0x15
    mov dl, [d_usedl]
    stc
    int 0x13
    mov [cs:d_p15st], ah
    pop es
    pop ds
    jc .p15bad
    mov si, s_p15t
    call puts
    mov al, [d_p15st]
    call puthex8
    mov si, s_p15n              ; AH=1: no change line. AH=2: change line
    cmp byte [d_p15st], 2       ; present, and it ASSERTS after a disk swap -
    jne .p15p                   ; which is status 06 on the first read and is
    mov si, s_p15c              ; recovered by a reset, if the loader resets
.p15p:
    call puts
    jmp short .p15done
.p15bad:
    mov si, s_p15no
    call puts
.p15done:
    call crlf
    ret

; =============================================================================
; [5] READS - the eight shapes the loader uses, each checked against the DATA
;
; The carry flag is not the answer to any of these. SPEC.md 18.91 is a BIOS
; that moves nine sectors and answers AL=1; SPEC.md 18.93.1 is a BIOS that
; answers CF=0 and the full count for a transfer it never finished. Both look
; perfect from the flag, so every run below is verified sector by sector
; against the index the Makefile stamped into it.
; =============================================================================
sec_reads:
    mov si, s_h5
    call puts
    mov si, s_rhdr
    call puts

    ; 1 - the boot sector, one sector, cylinder 0 head 0 sector 1. The
    ;     smallest request there is: if this fails nothing else matters.
    mov si, s_r1
    call rd_lab
    mov word [t_lba], 0
    mov word [t_n], 1
    call rd_go
    call rd_st
    mov si, s_dsig              ; verified against 0xAA55, not against a stamp
    call puts                   ; - LBA 0 is the boot sector and has none
    mov ax, [d_scr]
    mov es, ax
    mov ax, [es:510]
    push cs
    pop es
    cmp ax, 0xAA55
    call rd_okc
    call crlf

    ; 2 - one sector of the payload, so the stamp check is live from here on
    mov si, s_r2
    call rd_lab
    mov ax, [g_lba0]
    add ax, STAMP0
    mov [t_lba], ax
    mov word [t_n], 1
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 3 - cylinder 0, head 1, sector 1: the LOWEST head-1 sector on the disk,
    ;     and the far side of the very first head flip a multi-track run makes
    ;     (SPEC.md 18.92). No seek is involved, so a failure here is the HEAD
    ;     and not the stepper - which is what separates it from read 4. It is
    ;     in the root directory on every geometry, so there is no stamp to
    ;     check and the data column says so.
    mov si, s_r3
    call rd_lab
    mov ax, [g_spt]
    mov [t_lba], ax
    mov word [t_n], 1
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 4 - the first sector on CYLINDER 1: a seek, with the step rate that came
    ;     out of the ROM's own parameter table
    mov si, s_r4
    call rd_lab
    call lba_cyl1
    mov [t_lba], ax
    mov word [t_n], 1
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 5 - a whole track in one int 13h. The shipped loader's smallest
    ;     multi-sector request, and TRACKRUN=1's largest.
    mov si, s_r5
    call rd_lab
    call lba_trk
    mov [t_lba], ax
    mov ax, [g_spt]
    call rd_clamp
    mov [t_n], ax
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 6 - a run that CROSSES A HEAD, which is what SPEC.md 18.91.1's cylinder
    ;     bound buys and SPEC.md 18.93.1's canary exists to check. This is the
    ;     one that has already been caught lying on a 286 clone
    ;     (docs/FIELD-NOTES.md 31), and the EOT patch is live for it.
    mov si, s_r6
    call rd_lab
    call lba_cross_head
    mov [t_lba], ax
    mov word [t_n], 4
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 7 - and a run that crosses a CYLINDER, which no BIOS is required to do
    ;     and the loader never asks for. It is here as the control: a machine
    ;     that fails 6 and 7 alike has a bound problem, one that fails only 7
    ;     is behaving exactly as documented.
    mov si, s_r7
    call rd_lab
    call lba_cross_cyl
    mov [t_lba], ax
    mov word [t_n], 4
    call rd_go
    call rd_st
    call rd_verify
    call crlf

    ; 8 - into KERNEL_SEG. Where every sector of the kernel actually lands,
    ;     and the first destination that is not our own memory.
    mov si, s_r8
    call rd_lab
    mov ax, cs                  ; ...unless we are loaded THERE, which is
    cmp ax, KERNEL_SEG + 0x20   ; exactly what bootdiagx.img is: the shipped
    jb .r8skip                  ; stage 1 puts a FLAT_PAYLOAD at KERNEL_SEG,
    mov ax, [g_lba0]            ; so this read would land on this code
    add ax, STAMP0
    mov [t_lba], ax
    mov word [t_n], 1
    mov word [t_seg], KERNEL_SEG
    call rd_go
    call rd_st
    call rd_verify
    call crlf
    ret
.r8skip:
    mov si, s_r8skip
    call puts
    call crlf
    ret

; =============================================================================
; [6] MEMORY - is the RAM the loader relocates into there, and does it stay
;
; boot/boot.asm computes its own home from int 12h and moves to the LAST 512
; bytes of conventional RAM, with its stack in the 2KB below that and stage 2
; in the 5.5KB below that again (SPEC.md 2.7). Every one of those is a bet
; that int 12h is telling the truth and that nothing else wants the top of
; memory. Both bets are testable, and neither has ever been tested.
; =============================================================================
sec_memory:
    mov si, s_h6
    call puts

    ; --- the three addresses stage 1 derives, spelled out -------------------
    mov ax, [d_kb]
    mov cl, 6
    shl ax, cl                  ; KB*64 = the paragraph one past the top
    mov [d_topseg], ax
    sub ax, RELOC_ADJ
    mov [d_relseg], ax
    add ax, STAGE2_ADJ
    mov [d_b2seg], ax

    mov si, s_mtop
    call puts
    mov ax, [d_topseg]
    call puthex16
    mov si, s_mrel
    call puts
    mov ax, [d_relseg]
    call puthex16
    mov si, s_mb2
    call puts
    mov ax, [d_b2seg]
    call puthex16
    call crlf

%ifdef COMFILE
    mov si, s_mdos              ; under DOS the top of memory is DOS's, and a
    call puts                   ; diagnostic that scribbles on it is a bug
    ret
%else
    ; --- 1: is it THERE? ----------------------------------------------------
    ; int 12h over-reporting is not exotic - a shift that overflows, a ROM
    ; that counts a shadow area - and stage 1's far jump into memory that does
    ; not exist is a hang, not a message.
    mov si, s_mthere
    call puts
    mov ax, [d_relseg]
    mov es, ax
    call mem_probe              ; ES:7C00, where the relocated sector lands
    call rd_okc
    mov si, s_mthere2
    call puts
    mov ax, [d_b2seg]
    mov es, ax
    xor di, di
    call mem_probe_at           ; ...and stage 2's own first byte
    call rd_okc
    push cs
    pop es
    call crlf

    ; --- 2: does it STAY, and HOW DEEP does the ROM go? ---------------------
    ; THE ONE NOBODY LOOKS FOR, and it is two questions in one probe. A ROM
    ; that keeps scratch in the top of conventional memory without subtracting
    ; it from int 12h has stage 1's stack living inside the BIOS's own
    ; variables - so the first int 13h corrupts the loader, or the loader
    ; corrupts the int 13h, and what reaches the screen is `Disk error` either
    ; way. And a ROM whose int 13h simply uses a LOT of stack runs off the
    ; bottom of the 2,048 bytes stage 1 reserves, which is the same fault
    ; arrived at from the other end.
    ;
    ; So the fill goes PROBE_DEPTH below stage 1's SP - twice what it reserves
    ; - and what comes back is a DEPTH IN BYTES rather than a yes. A number
    ; under 2,048 is stage 1's bet paying off and says by how much; anything
    ; over it is the bug, named.
    mov ax, [d_relseg]
    mov es, ax
    mov di, 0x7C00 - PROBE_DEPTH
    mov cx, PROBE_DEPTH
    mov ax, 0xA5A5
    call mem_fill

    ; **STANDING WHERE STAGE 1 STANDS, which is the only way to ask this.**
    ; The first version of this filled the region and then made the calls on
    ; the program's OWN stack, and reported 0 bytes on a machine whose ROM
    ; cannot have used none: an `int` pushes six bytes before a line of BIOS
    ; code runs, and they went where SP pointed - here, not there. What that
    ; measured was whether the top of RAM is scratch, which is half the
    ; question and reads exactly like the whole one.
    ;
    ; So SS:SP goes to stage 1's, and the four calls below are INLINE with no
    ; CALL among them: a call of ours on the borrowed stack is two more bytes
    ; the ROM did not spend, and the number this prints is meant to be the
    ; ROM's.
    ;
    ; SS:SP is banked in memory rather than pushed, because the stack it would
    ; be pushed on is the one being replaced. The 8086 masks interrupts for one
    ; instruction after a load of SS, so each pair is atomic on its own; the
    ; cli/sti brackets are for the reader.
    mov [d_savesp], sp
    mov [d_savess], ss
    mov ax, [d_relseg]
    cli
    mov ss, ax
    mov sp, 0x7C00
    sti
    xor ah, ah
    mov dl, [cs:d_usedl]
    int 0x13                    ; ...a reset
    mov es, [cs:d_scr]          ; EVERY operand from CS below, not from a
    xor bx, bx                  ; register banked before the switch: int 13h
                                ; AH=00h is not required to preserve one, and
                                ; a ROM that eats BX here would have us read
                                ; into a segment nobody chose. ...a real read
    mov cx, 0x0001
    xor dh, dh
    mov dl, [cs:d_usedl]
    mov ax, 0x0201
    int 0x13
    mov [cs:d_stkst], ah        ; A READ THAT ONLY FAILS FROM HERE IS THE BUG
    mov ah, 0x0E
    mov al, ' '
    mov bx, 7
    int 0x10                    ; ...and the two other interrupts the loader
    mov ah, 0x01                ; takes with this stack live. int 16h AH=01h
    int 0x16                    ; is a peek and never a wait
    cli
    mov ss, [cs:d_savess]
    mov sp, [cs:d_savesp]
    sti
    push cs
    pop ds
    push cs
    pop es

    mov si, s_mstay
    call puts
    mov al, [d_stkst]
    call puthex8
    mov si, s_mstay1
    call puts
    mov ax, [d_relseg]
    mov es, ax
    mov di, 0x7C00
    mov cx, PROBE_DEPTH
    mov ax, 0xA5A5
    call mem_depth
    push cs
    pop es
    call putdec16
    mov si, s_mstay2b
    call puts
    cmp ax, BOOT_STACK
    jbe .stackok
    mov si, s_mdeep             ; ...and it is not a near miss: below that
    call puts                   ; stage 1 has nothing but stage 2's own image
.stackok:
    call crlf

    ; --- 3: and the same question for stage 2's 5.5KB -----------------------
    mov si, s_mstay2
    call puts
    mov ax, [d_b2seg]
    mov es, ax
    xor di, di
    mov cx, BOOT2_SECS * 512
    mov ax, 0x5A5A
    call mem_fill
    xor ah, ah
    mov dl, [d_usedl]
    int 0x13
    call rd_boot
    mov ax, [d_b2seg]
    mov es, ax
    xor di, di
    mov cx, BOOT2_SECS * 512
    mov ax, 0x5A5A
    call mem_check
    call rd_okc
    push cs
    pop es
    call crlf
    ret
%endif

; =============================================================================
; [7] THE STAGE-1 REPLAY - the shipped loader's own read, instrumented
;
; Everything above tests a mechanism. This does what boot/boot.asm actually
; does, in the order it does it, into the addresses it uses: the patched table
; is already installed by section 3, the destination is the computed stage-2
; segment, the run is bounded by the track and by the 64KB DMA page, and the
; sectors are the first BOOT2_SECS of the data area - which on this disk are
; the first BOOT2_SECS of the payload, so every one of them carries a stamp
; and the transfer can be checked rather than believed.
;
; IT IS LAST ON PURPOSE. If the machine stops here, that is not the diagnostic
; failing - it is the diagnostic succeeding, and everything above is already
; on the screen to be photographed.
; =============================================================================
sec_replay:
    call newpage
    mov si, s_h7
    call puts
%ifdef COMFILE
    mov si, s_mdos
    call puts
    ret
%else
    mov ax, [g_lba0]            ; PASS 1: the sectors stage 1 actually reads,
    call replay_run             ; which are this code and cannot be checked -
                                ; so read the STATUS columns, which are the
                                ; whole point of the pass
    mov si, s_rp2
    call puts
    mov ax, [g_lba0]            ; PASS 2: the same shape over the stamped
    add ax, STAMP0              ; region - same count, same bounds, same
    call replay_run             ; destination, and every sector checkable
    ret

; replay_run - BOOT2_SECS sectors from AX to the stage-2 segment, cut into
;              runs by the same two bounds stage 1 uses
replay_run:
    mov [t_lba], ax
    mov word [r_left], BOOT2_SECS
    mov ax, [d_b2seg]
    mov [r_dest], ax
    mov byte [d_replay], 1
.next:
    mov ax, [t_lba]             ; --- bound 1: the end of the track ----------
    xor dx, dx
    div word [g_spt]
    mov ax, [g_spt]
    sub ax, dx
    cmp ax, [r_left]
    jbe .b2
    mov ax, [r_left]
.b2:
    mov [t_n], ax
    mov ax, [r_dest]            ; --- bound 2: the 64KB DMA page -------------
    mov cl, 4
    shl ax, cl
    neg ax
    jz .go
    mov cl, 9
    shr ax, cl
    cmp ax, [t_n]
    jae .go
    mov [t_n], ax
.go:
    mov si, s_rp
    call rd_lab                 ; ...which resets [t_seg] to "the scratch
    mov ax, [r_dest]            ; buffer", so the real destination goes in
    mov [t_seg], ax             ; AFTER it and not before
    call rd_go
    call rd_st
    call rd_verify
    call crlf
    cmp byte [t_bad], 0
    jne .stop                   ; a bad run makes every run after it noise
    cmp byte [t_cf], 0          ; ...and so does one that never completed. NOT
    jne .stop                   ; [t_st], which since the fix above is the
                                ; first status ANY attempt returned - a run
                                ; that hiccupped and then succeeded is a run
                                ; that succeeded, and stopping on it truncated
                                ; the replay at its first line
    mov ax, [t_n]
    add [t_lba], ax
    sub [r_left], ax
    mov cl, 5
    shl ax, cl
    add [r_dest], ax
    cmp word [r_left], 0
    jne .next
.stop:
    mov byte [d_replay], 0
    ret
%endif

; =============================================================================
; [8] VERDICT - the one line to photograph if only one line fits
; =============================================================================
sec_verdict:
    mov si, s_h8
    call puts
    mov si, s_vdl               ; SELF-LABELLING, because the thing this line
    call puts                   ; is for is being photographed and read back
    mov al, [d_bootdl]          ; down a phone
    call puthex8
    mov si, s_vused
    call puts
    mov al, [d_usedl]
    call puthex8
    mov si, s_veot
    call puts
    mov al, [d_eot]
    call puthex8
    mov si, s_vdpt
    call puts
    mov al, [d_dptkept]
    call puthex8
    mov si, s_vscr
    call puts
    mov al, [d_scrok]
    call puthex8
    mov si, s_vst
    call puts
    mov al, [d_stmask]          ; every status any attempt returned, OR'd -
    call puthex8                ; one glance says whether anything ever set
    mov si, s_vbad              ; carry, even on a read that then succeeded
    call puts
    mov ax, [d_badmask]         ; ...and which checked runs came back with the
    call puthex16               ; wrong BYTES, one fixed bit each
    call crlf
    mov si, s_vkey
    call puts
    ret

; =============================================================================
; the read engine
; =============================================================================
; rd_lab - print a test's label and its CHS, and set the default destination
; in: DS:SI = label
rd_lab:
    call puts
    mov word [t_seg], 0         ; 0 = "the scratch buffer", resolved in rd_go
    ret

; rd_go - one int 13h AH=02h of [t_n] sectors from [t_lba] to [t_seg]:0000,
;         three attempts with a reset between, exactly as the loader does it.
;         Prints the CHS it used.
; out: [t_st] AH, [t_al] AL, [t_cf], [t_try]
rd_go:
    mov ax, [t_seg]
    or ax, ax
    jnz .haveseg
    mov ax, [d_scr]
    mov [t_seg], ax
.haveseg:
    mov ax, [t_lba]             ; LBA -> CHS
    xor dx, dx
    div word [g_spt]
    inc dx
    mov [t_sec], dl
    xor dx, dx
    div word [g_heads]
    mov [t_cyl], al
    mov [t_hd], dl
    mov byte [t_try], 0
    mov byte [t_bad], 0
    mov byte [t_st], 0
.attempt:
    inc byte [t_try]
    mov ch, [t_cyl]
    mov cl, [t_sec]
    mov dh, [t_hd]
    mov dl, [d_usedl]
    mov es, [t_seg]
    xor bx, bx
    mov al, [t_n]
    mov ah, 0x02
    stc
    int 0x13
    mov [cs:t_al], al
    pushf                       ; ...because everything below sets flags, and
    push cs                     ; the CARRY is the only thing int 13h says
    pop ds                      ; that cannot be reconstructed
    push cs
    pop es
    cmp byte [t_st], 0          ; THE FIRST NON-ZERO STATUS, not the last one.
    jne .stkeep                 ; A read that failed once and then succeeded
    mov [t_st], ah              ; printed 00 here and `try 2` three columns
.stkeep:                        ; along, which says something went wrong and
    popf                        ; refuses to say what - and the whole point of
    jnc .ok                     ; this program is the what
    mov byte [t_cf], 1
    or [d_stmask], ah
    cmp byte [t_try], 3
    jae .out
    xor ah, ah
    mov dl, [d_usedl]
    int 0x13
    jmp short .attempt
.ok:
    mov byte [t_cf], 0
.out:
    mov al, [t_cyl]             ; ...and say where it looked
    call puthex8
    mov al, '/'
    call putc
    mov al, [t_hd]
    call puthex8
    mov al, '/'
    call putc
    mov al, [t_sec]
    call puthex8
    mov si, s_rx
    call puts
    mov al, [t_n]
    call puthex8
    ret

; rd_st - the status columns: AH, AL and how many attempts it took
rd_st:
    mov si, s_rst
    call puts
    mov al, [t_st]
    call puthex8
    call putsp
    mov al, [t_al]
    call puthex8
    call putsp
    mov al, [t_try]
    add al, '0'
    call putc
    ret

; rd_verify - every sector of the run against the index the build stamped in.
;             A sector outside the payload has no stamp and is skipped, which
;             is why every read above starts at [g_lba0]+1 or later.
rd_verify:
    mov si, s_rdat
    call puts
    mov cx, [t_n]
    mov ax, [t_lba]
    sub ax, [g_lba0]            ; AX = payload sector index of the first
    mov [t_idx], ax
    mov bx, [t_seg]
    mov byte [t_bad], 0
    mov word [t_nbad], 0
.s:
    push cx
    mov es, bx
    cmp word [t_idx], STAMP0    ; below STAMP0 the payload is this CODE, which
    jb .skip                    ; the build has nowhere to stamp an index in.
                                ; The replay's first pass reads exactly there
                                ; on purpose, and its STATUS columns are the
                                ; point of it - not this one
    cmp word [t_idx], SECS      ; ...and above it is somebody else's file
    jae .skip                   ; system. An LBA BELOW the payload subtracts
                                ; to a huge unsigned index and lands here,
                                ; which is what read 3 relies on
    mov ax, [es:0]              ; every stamped sector holds its own index in
    cmp ax, [t_idx]             ; every word of it (tests/rdiag.asm's trick)
    je .good
    inc byte [t_bad]
    inc word [t_nbad]
    mov al, 'X'
    jmp short .put
.skip:
    mov al, '?'                 ; read, unverifiable, not counted against it
    jmp short .put
.good:
    mov al, '.'
.put:
    push bx
    push cs
    pop es
    call putc
    pop bx
    add bx, 0x20                ; 32 paragraphs a sector
    inc word [t_idx]
    pop cx
    loop .s
    push cs
    pop es
    cmp byte [t_bad], 0
    je .clean
    mov si, s_rbad              ; ...and NAME it, because a wrong sector with
    call puts                   ; CF=0 is the failure mode that hides
    mov ax, [t_nbad]
    call putdec16
    mov si, s_rbad2
    call puts
    mov cl, [d_vidx]            ; bit 0 is the first checked run of the
    mov ax, 1                   ; report, bit 1 the second, and so on down
    shl ax, cl                  ; the page - a FIXED position, because a
    or [d_badmask], ax          ; shift register makes the reader count the
.clean:                         ; runs backwards to find out which one it was
    cmp byte [d_vidx], 15
    jae .out
    inc byte [d_vidx]
.out:
    ret

; rd_boot - a plain one-sector read of LBA 0 into scratch, used by the memory
;           tests as "make the BIOS do some real floppy work"
rd_boot:
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, [d_scr]
    mov es, ax
    xor bx, bx
    mov cx, 0x0001
    mov dh, 0
    mov dl, [d_usedl]
    mov ax, 0x0201
    int 0x13
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rd_clamp - AX sectors, but never more than the scratch buffer holds
rd_clamp:
    cmp ax, SCR_SECS
    jbe .ok
    mov ax, SCR_SECS
.ok:
    ret

; rd_okc - "yes"/"no" from ZF (set = yes), for a `cmp` in the caller
rd_okc:
    mov si, s_no
    jne .say
    mov si, s_yes
.say:
    call puts
    ret

; =============================================================================
; where the interesting sectors are on THIS geometry
; =============================================================================
; lba_cyl1 - the first payload sector on cylinder 1 or beyond
lba_cyl1:
    mov ax, [g_lba0]
    add ax, STAMP0              ; the stamped region: everything below it is
.try:                           ; this code, and proves nothing about a read

    push ax
    xor dx, dx
    div word [g_spt]
    xor dx, dx
    div word [g_heads]
    or ax, ax
    pop ax
    jnz .out
    inc ax
    jmp short .try
.out:
    ret

; lba_trk - the first track-aligned payload sector (sector 1 of some track)
lba_trk:
    mov ax, [g_lba0]
    add ax, STAMP0              ; the stamped region: everything below it is
.try:                           ; this code, and proves nothing about a read

    push ax
    xor dx, dx
    div word [g_spt]
    or dx, dx
    pop ax
    jz .out
    inc ax
    jmp short .try
.out:
    ret

; lba_cross_head - two sectors before the first head 0 -> head 1 flip inside
;                  the payload, so a four-sector run straddles it
lba_cross_head:
    call lba_flip0
    sub ax, 2
    ret

; lba_cross_cyl - the same, for the head 1 -> head 0 flip that ends a cylinder
lba_cross_cyl:
    call lba_flip1
    sub ax, 2
    ret

; lba_flip0 - the first LBA in the payload that starts head 1 of a cylinder
lba_flip0:
    mov ax, [g_lba0]
    add ax, STAMP0 + 2          ; ...and room for the two-sector back-off the
.try:                           ; crossing runs take
    push ax
    xor dx, dx
    div word [g_spt]
    or dx, dx
    jnz .no                     ; not the first sector of a track
    xor dx, dx
    div word [g_heads]
    cmp dx, 1
    je .yes
.no:
    pop ax
    inc ax
    jmp short .try
.yes:
    pop ax
    ret

; lba_flip1 - ...and the first that starts head 0 of a NEW cylinder
lba_flip1:
    mov ax, [g_lba0]
    add ax, STAMP0 + 2
.try:
    push ax
    xor dx, dx
    div word [g_spt]
    or dx, dx
    jnz .no
    xor dx, dx
    div word [g_heads]
    or dx, dx
    jne .no
    or ax, ax
    jz .no                      ; cylinder 0 head 0 is not a flip
    pop ax
    ret
.no:
    pop ax
    inc ax
    jmp short .try

; geom_from_bpb - SPT, heads and the data area's LBA off the disk itself.
;                 CHS (0,0,1) is LBA 0 in every geometry, so this needs no
;                 geometry to bootstrap from.
geom_from_bpb:
    call scratch_seg
    mov es, ax
    xor bx, bx
    mov cx, 0x0001
    xor dh, dh
    mov dl, [d_usedl]
    mov ax, 0x0201
    int 0x13
    jc .fail
    mov ax, [es:24]             ; BPB_SecPerTrk
    mov [g_spt], ax
    mov ax, [es:26]             ; BPB_NumHeads
    mov [g_heads], ax
    mov al, [es:16]             ; rsvd + nfat*fatsz + ceil(rootent*32/512)
    xor ah, ah
    mul word [es:22]
    add ax, [es:14]
    mov bx, ax
    mov ax, [es:17]
    add ax, 15
    mov cl, 4
    shr ax, cl
    add ax, bx
    mov [g_lba0], ax
    mov word [g_secs], SECS
    push cs
    pop es
    ret
.fail:
    mov word [g_spt], 9         ; nothing read: assume the 360KB shape so the
    mov word [g_heads], 2       ; rest of the report still runs and says why
    mov word [g_lba0], 12
    mov word [g_secs], SECS
    push cs
    pop es
    ret

%ifdef COMFILE
; com_drive - `BOOTDIAG B:` or `BOOTDIAG 1` picks the unit to survey; with no
;             argument it is A:. The tail is at PSP:0080 - a length byte and
;             then the characters, and DS is still the PSP for a .COM.
com_drive:
    mov si, 0x0081
    mov cl, [0x0080]
    xor ch, ch
    jcxz .out
.scan:
    lodsb
    cmp al, 'a'
    jb .digit
    cmp al, 'z'
    ja .digit
    sub al, 0x20                ; fold case, so `b:` works as well as `B:`
.digit:
    cmp al, 'A'
    jb .num
    cmp al, 'D'
    ja .num
    sub al, 'A'
    mov [d_usedl], al
    ret
.num:
    cmp al, '0'
    jb .next
    cmp al, '3'
    ja .next
    sub al, '0'
    mov [d_usedl], al
    ret
.next:
    loop .scan
.out:
    ret
%endif

; scratch_seg - AX = the read buffer's segment: SCR_ADJ paragraphs above our
;               own base, moved up to the next 64KB page if 8KB from there
;               would straddle one.
;
; The fixup does not fire where either build actually lands, and it is here
; anyway: a straddling buffer answers error 09 on every multi-sector read
; (SPEC.md 18.93 bound 3), which is a fault of OURS presented in the column
; the whole report is about. Where the payload lands is boot/boot.asm's
; business in one of the two builds, so it is not a constant this file can
; assert about.
scratch_seg:
    mov ax, cs
    add ax, SCR_ADJ
    push ax
    mov cl, 4
    shl ax, cl                  ; the low 16 bits of the linear address...
    neg ax                      ; ...and the bytes to the page end, 0 = a
    jz .ok                      ; whole page, which is page-aligned and fine
    cmp ax, SCR_SECS * 512
    jae .ok
    pop ax
    add ax, 0x1000              ; ...not enough: start at the next page
    and ax, 0xF000
    ret
.ok:
    pop ax
    ret

; =============================================================================
; memory helpers - ES:DI, CX bytes, AX pattern
; =============================================================================
mem_fill:
    push di
    shr cx, 1
    rep stosw
    pop di
    ret

mem_check:                      ; ZF set = every word still holds AX
    push di
    shr cx, 1
.w:
    scasw
    jne .out
    loop .w
    xor cx, cx                  ; ZF set
.out:
    pop di
    ret

; mem_depth - the DEEPEST byte of a filled region that something disturbed.
; in:  ES = segment, DI = the region's TOP offset, CX = bytes filled below it,
;      AX = the pattern
; out: AX = bytes below the top that were disturbed, 0 for none
;
; It walks UP from the bottom, so the first word that differs is the furthest
; one from the top - which for a stack is its high-water mark and for a ROM
; scratch area is its floor. Either way it is the number that matters, and a
; yes/no is the number thrown away.
mem_depth:
    push bx
    push cx
    push di
    mov bx, di                  ; the top, to measure back from
    sub di, cx
    shr cx, 1
.w:
    scasw
    jne .found
    loop .w
    xor ax, ax
    jmp short .out
.found:
    mov ax, bx                  ; scasw has already stepped past the word it
    sub ax, di                  ; stopped on, so the two bytes come back
    add ax, 2
.out:
    pop di
    pop cx
    pop bx
    ret

mem_probe:                      ; ES:7C00 - is there RAM there at all?
    mov di, 0x7C00
mem_probe_at:                   ; ...or at ES:DI
    push di
    mov word [es:di], 0x1234
    mov word [es:di + 2], 0x5678
    cmp word [es:di], 0x1234    ; a read-back that fails is ROM, a hole, or an
    jne .out                    ; alias of somewhere else
    cmp word [es:di + 2], 0x5678
.out:
    pop di
    ret

; =============================================================================
; cpu_tier - 0 = 8086/8088, 1 = 80286, 2 = 80386 or later
;
; The `push sp` half is boot/boot2.asm's own gate (SPEC.md 18.93.2), which is
; what decides whether the loader asks for a whole cylinder or a whole track.
; A machine that fails read 6 above and reports 286 or later here is taking
; the safe bound already; one that fails it and reports 8086 is not.
; =============================================================================
cpu_tier:
    mov byte [d_cpu], 0
    push sp                     ; the 8086 and 8088 push SP as it is AFTER the
    pop ax                      ; decrement; every later part pushes it as it
    cmp ax, sp                  ; was before
    jne .out
    mov byte [d_cpu], 1         ; ...286 unless the next test says otherwise
    pushf                       ; 286 against 386: TRY TO SET bits 12..14.
    pop cx                      ; A 286 in real mode forces them to zero and a
    mov ax, cx                  ; 386 keeps them - where CLEARING them and
    or ax, 0x7000               ; reading back, which is the 8086 test, gives
    push ax                     ; zero on both and called every 386 a 286.
    popf
    pushf
    pop ax
    push cx                     ; ...and the machine's own flags go back
    popf
    and ax, 0x7000
    jz .out
    mov byte [d_cpu], 2
.out:
    ret

; =============================================================================
; output
; =============================================================================
cls:
%ifndef COMFILE
    mov ah, 0x0F                ; whatever mode the BIOS left - a monochrome
    int 0x10                    ; card has no mode 3 and setting one is a
    xor ah, ah                  ; blank screen
    int 0x10
%endif
    mov byte [d_line], 0
    ret

; page - one line has just gone out. Stop for a key when the screen is full.
;
;        Called from putc on every LINE FEED, which is the only place that
;        knows about all of them: half the lines here come out of a string
;        with an embedded 13,10 and the other half out of crlf, and counting
;        in both double-counted every header. Under DOS the report redirects
;        to a file, so it never pauses at all.
;
;        THE GUARD IS NOT DECORATION: the pause prints, printing goes through
;        putc, and putc is what calls this. One level is all it could ever
;        recurse - `-- more --` has no line feed in it - but a message that
;        grew one would take the machine down rather than misprint.
page:
%ifndef COMFILE
    cmp byte [d_paging], 0
    jne .out
    inc byte [d_line]
    cmp byte [d_line], PAGELEN
    jb .out
    mov byte [d_paging], 1
    push cx
    push dx
    push si
    push di
    mov si, s_more
    call puts
    call getkey
    call cls
    pop di
    pop si
    pop dx
    pop cx
    mov byte [d_paging], 0
.out:
%endif
    ret

; newpage - break HERE rather than wherever the line count happens to fall.
;           The report is read off a photograph, and a section split across
;           two of them is a section somebody has to hold side by side.
newpage:
%ifndef COMFILE
    cmp byte [d_line], 0
    je .out
    mov byte [d_line], PAGELEN - 1
    mov al, 10                  ; ...through putc, so the one place that
    call putc                   ; knows about the pause is still the only
.out:                           ; place that takes it
%endif
    ret

getkey:
    push ax
    xor ax, ax
    int 0x16
    pop ax
    ret

putc:
    push ax
    push bx
    push dx
    mov dl, al                  ; DL, not AL: int 10h AH=0Eh is not required
%ifdef COMFILE                  ; to preserve AL and the line count below
    mov ah, 0x02                ; needs to know what was just printed
    int 0x21
%else
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
%endif
    cmp dl, 10
    jne .out
    call page
.out:
    pop dx
    pop bx
    pop ax
    ret

putprint:
    push ax
    cmp al, 0x20
    jb .dot
    cmp al, 0x7E
    jbe .ok
.dot:
    mov al, '.'
.ok:
    call putc
    pop ax
    ret

puts:
    push ax
    push si
.l:
    lodsb
    or al, al
    jz .out
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

puthex8:
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

puthex16:
    push ax
    mov al, ah
    call puthex8
    pop ax
    call puthex8
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
    or ax, ax
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
; state
; =============================================================================
d_bootdl    db 0                ; DL as the BIOS handed it to the boot sector
d_usedl     db 0                ; ...and what the loader ended up using
d_fellback  db 0
d_loader    db 0                ; 0 shipped stage 1, 1 bdboot, 2 DOS
d_ldst      db 0
d_ldretry   db 0
d_cpu       db 0
d_eot       db 0
d_dptkept   db 0
d_scrok     db 0
d_rstst     db 0
d_replay    db 0
d_line      db 0
d_vidx      db 0
d_paging    db 0
d_p8st      db 0
d_p8bl      db 0
d_p15st     db 0
d_stkst     db 0
d_c0mm      dw 0
d_c0rf      dw 0
d_p8cx      dw 0
d_p8dx      dw 0
d_kb        dw 0
d_equip     dw 0
d_scr       dw 0
d_topseg    dw 0
d_relseg    dw 0
d_b2seg     dw 0
d_pat       dw 0
d_stmask    db 0
d_badmask   dw 0

g_spt       dw 9
g_heads     dw 2
g_lba0      dw 12
g_secs      dw SECS

t_lba       dw 0
t_n         dw 0
t_seg       dw 0
t_idx       dw 0
t_nbad      dw 0
t_cyl       db 0
t_hd        db 0
t_sec       db 0
t_st        db 0
t_al        db 0
t_cf        db 0
t_try       db 0
t_bad       db 0

d_savesp    dw 0
d_savess    dw 0
r_left      dw 0
r_dest      dw 0

; =============================================================================
; strings
; =============================================================================
s_h1        db 'os8088 bootdiag - SPEC.md 2.9.10', 13, 10
            db '[1] MACHINE', 13, 10, 0
s_biosdate  db '  BIOS  date ', 0
s_model     db '  model ', 0
s_sub       db '  sub/rev/feat ', 0
s_noc0      db '  (no int 15h C0h)', 0
s_ident     db '  ROM   ', 0
s_noident   db '(no printable run in F000)', 0
s_cpu       db '  CPU   ', 0
s_cpu86     db '8088/8086 - the loader asks for a whole CYLINDER', 0
s_cpu286    db '80286 - the loader asks for a whole TRACK', 0
s_cpu386    db '80386+ - the loader asks for a whole TRACK', 0
s_mem       db '  RAM   ', 0
s_kb        db ' KB  equip ', 0
s_fdd       db '  floppies ', 0
s_vid       db '  video ', 0

s_h2        db '[2] HANDOVER', 13, 10, 0
s_ldr       db '  loaded by  ', 0
s_ldrx      db 'boot/boot.asm - THE SHIPPED STAGE 1', 0
s_ldrb      db 'bdboot.asm - the paranoid loader', 0
s_ldrd      db 'DOS (BOOTDIAG.COM)', 0
s_dl        db '  DL at boot ', 0
s_dlused    db '  used ', 0
s_dlfell    db '  <== THE BIOS DID NOT SET DL. Fell back to 0.', 0
s_dlhd      db '  <== a HARD DISK unit for a floppy boot', 0
s_dlclamp   db '               ...but stage 1 has already clamped it '
            db '(SPEC.md 2.9.11):', 13, 10
            db '               boot the OTHER disk for what the BIOS '
            db 'really said', 0
s_load      db '  payload    ', 0
s_loadsec   db ' sectors  retries ', 0
s_loadst    db '  last status ', 0
s_geom      db '  geometry   spt ', 0
s_geomh     db '  heads ', 0
s_geoml     db '  data area LBA ', 0

s_h3        db '[3] DISKETTE PARAMETER TABLE (int 1Eh)', 13, 10, 0
s_dptown    db '  as this program found it', 13, 10, 0
s_vdl       db '  BD dl ', 0
s_vused     db '  used ', 0
s_veot      db '  eot ', 0
s_vdpt      db '  dptkept ', 0
s_vscr      db '  scrok ', 0
s_vst       db '  st ', 0
s_vbad      db '  bad ', 0
s_dptours   db '  after os8088 installs its copy at 0000:0580 and resets',
            db 13, 10, 0
s_dptvec    db '    vector ', 0
s_inrom     db ' (ROM)', 13, 10, 0
s_inram     db ' (RAM)', 13, 10, 0
s_dptb      db '    bytes  ', 0
s_eot       db '    EOT ', 0
s_eotbad    db " <== not this disk's SPT", 0
s_dptrst    db '    reset AH ', 0
s_dptkept   db '   ours kept: ', 0
s_dptscr    db '   0580 untouched: ', 0

s_h4        db '[4] DRIVE', 13, 10, 0
s_p8        db '  AH=08h st ', 0
s_p8c       db '  cyls ', 0
s_p8h       db '  heads ', 0
s_p8s       db '  spt ', 0
s_p8n       db '  drives ', 0
s_p8t       db '  type ', 0
s_p8no      db '  <== not supported (an original PC/XT ROM has no AH=08h)', 0
s_p15       db '  AH=15h ', 0
s_p15t      db 'type ', 0
s_p15n      db ' (no change line)', 0
s_p15c      db ' (change line - status 06 until a reset)', 0
s_p15no     db 'not supported', 0

s_h5        db '[5] READS - the eight shapes the loader uses', 13, 10, 0
s_rhdr      db '    what                      c/h/s   n  st al try  data',
            db 13, 10
            db '    st is the FIRST non-zero status any attempt returned.',
            db '  data: . right', 13, 10
            db '    X wrong with no error reported, ? no stamp on that sector',
            db 13, 10, 0
s_r1        db '  1 boot sector           ', 0
s_r2        db '  2 one payload sector    ', 0
s_r3        db '  3 first on HEAD 1       ', 0
s_r4        db '  4 first on CYLINDER 1   ', 0
s_r5        db '  5 a whole track         ', 0
s_r6        db '  6 CROSSES A HEAD        ', 0
s_r7        db '  7 crosses a cylinder    ', 0
s_r8        db '  8 into KERNEL_SEG 0060  ', 0
s_r8skip    db 'skipped - this build IS loaded there', 0
s_rp        db '  R stage-1 blob run      ', 0
s_rx        db ' x', 0
s_rst       db '  ', 0
s_rdat      db '  ', 0
s_rbad      db ' <== ', 0
s_rbad2     db ' BAD, CF=0 ANYWAY', 0
s_dsig      db '  sig ', 0

s_h6        db '[6] MEMORY - the top of RAM, where stage 1 relocates to',
            db 13, 10, 0
s_mtop      db '  top of conventional ', 0
s_mrel      db '  stage 1 at ', 0
s_mb2       db '  stage 2 at ', 0
s_mthere    db '  RAM really exists at stage 1: ', 0
s_mthere2   db '   at stage 2: ', 0
s_mstay     db "  on stage 1's own stack: read AH ", 0
s_mstay1    db ', ROM used ', 0
s_mstay2b   db ' bytes below SP', 0
s_mdeep     db 13, 10, '  <== DEEPER THAN THE 2048 STAGE 1 RESERVES', 0
s_mstay2    db "  stage 2's 5.5KB survives the same: ", 0
s_mdos      db '  skipped - the top of memory belongs to DOS in this build',
            db 13, 10, 0

s_h7        db '[7] THE STAGE-1 REPLAY - if the machine stops here, THAT IS',
            db ' THE ANSWER', 13, 10
            db '  pass 1 reads the sectors stage 1 reads (this code: no data',
            db ' column)', 13, 10, 0
s_rp2       db '  pass 2 is the same shape over stamped sectors, so it can be',
            db ' checked', 13, 10, 0

s_h8        db '[8] VERDICT', 13, 10, 0
s_vkey      db "  st is every status any attempt returned, OR'd; bad has one"
            db ' bit', 13, 10
            db '  per checked run, in the order they printed.', 13, 10
            db '  PHOTOGRAPH THIS PAGE AND THE ONES BEFORE IT.', 13, 10, 0

s_yes       db 'yes', 0
s_no        db 'NO', 0
s_more      db '-- more --', 0
%ifndef COMFILE
s_done      db 13, 10, 'Press a key to reboot.', 13, 10, 0
%endif
