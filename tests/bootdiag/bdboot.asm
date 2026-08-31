; =============================================================================
; tests/bootdiag/bdboot.asm - the PARANOID boot sector (SPEC.md 2.9.10)
;
; It exists to load one thing - tests/bootdiag/bootdiag.asm - on a machine
; where boot/boot.asm prints `Disk error` and stops. So it must not depend on
; ANY of the things that loader does, because every one of them is a suspect:
;
;   boot/boot.asm does                        this sector does
;   ---------------------------------------   ------------------------------
;   relocates itself to the top of RAM,       stays at 0x7C00 and loads the
;   computed from int 12h                     payload LOW, at 0x0800:0000
;   copies and patches the int 1Eh diskette   TOUCHES NOTHING. The table the
;   parameter table at 0000:0580              BIOS booted with is the table
;                                             it keeps
;   reads a whole track (up to 18 sectors)    ONE SECTOR AN int 13h, always
;   in one int 13h, and on an 8088 a whole
;   CYLINDER - across the head boundary
;   believes DL                               believes DL, and FALLS BACK TO
;                                             0 when every retry on the first
;                                             sector fails - then says so
;   has SPT and HEADS as build-time           reads them out of the BPB, so
;   immediates, one sector per geometry       ONE binary serves all three
;
; What is left is the smallest int 13h any BIOS since 1981 can be asked for:
; AH=02h, AL=1, to a 512-aligned low buffer, with a controller reset between
; attempts. If THAT fails the machine cannot boot anything, and the line this
; sector prints is already the diagnosis.
;
; ON FAILURE it prints ONE line and halts:
;
;     BD dl=XX/YY st=ZZ lba=WWWW
;
;   XX  the DL the BIOS handed us at 0000:7C00
;   YY  the DL the failing read actually used (differs from XX only when the
;       fallback fired - so `00/00` on a machine whose BIOS said 0 anyway, and
;       `2A/00` on one that handed over garbage and was overridden)
;   ZZ  int 13h's AH status from the LAST attempt, which is the whole
;       diagnosis when it is one of 0C (media the drive cannot identify),
;       04 (sector not found), 06 (the change line), 09 (a 64KB DMA straddle,
;       impossible for a single 512-aligned sector and therefore a lie),
;       80 (the drive never answered) or 20 (controller failure)
;   WWWW the LBA it died on, in hex. `000C` on a 360KB disk is the very first
;       payload sector, so nothing past the boot sector itself was ever read
;
; A dot per sector goes to the screen as it loads, on purpose: a machine that
; HANGS rather than erroring is then placed by counting them, and it proves
; the BIOS teletype works before the payload relies on it.
;
; Assembled with -DSECS=<n>; the geometry comes from the BPB at run time.
; =============================================================================
cpu 8086                        ; SPEC.md 1 - and doubly so here: this sector
                                ; is aimed at machines whose ROM we do not
bits 16                         ; trust, so it cannot want a 286 either
org 0x7C00

%ifndef SECS
%define SECS 40                 ; payload sectors, injected by the Makefile
%endif

PAYLOAD_SEG equ 0x00A0          ; linear 0x00A00 - LOW, and the low end is the
                                ; one that leaves room. Above it the payload
                                ; wants its own image, a stack and an 8KB
                                ; read buffer, and the buffer must not straddle
                                ; a 64KB DMA page or every multi-sector read
                                ; into it answers 09 for a reason that is
                                ; ours. From here the three of them end at
                                ; 0x08E00 and a 64KB machine still holds them;
                                ; from 0x08000 - which is where this started -
                                ; the buffer ended at 0x10400 and straddled.
                                ;
                                ; It clears everything below it that matters:
                                ; the BIOS data area stops at 0x004FF, our
                                ; parameter-table copy is at 0x00580, and
                                ; KERNEL_SEG - which the payload reads one
                                ; sector into - is 0x00600..0x007FF
STACK_TOP   equ 0x7C00          ; grows down from our own base, as ever
HAND_AT     equ 0x0010          ; the handover record's offset in the payload
                                ; (see bootdiag.asm's `hand` block) - written
                                ; AFTER the load, or the sectors landing there
                                ; would overwrite it
BPB_END     equ 62

; The BPB. tools/os88disk.py writes all of it (SPEC.md 19.3); we read five
; fields - four to find the data area and two more (+24, +26) for the
; geometry, which is what makes this one binary rather than three.
    jmp short entry
    nop
bpb_oem:     times 8 db 0        ; +3
bpb_bps:     dw 0                ; +11
bpb_spc:     db 0                ; +13
bpb_rsvd:    dw 0                ; +14
bpb_nfat:    db 0                ; +16
bpb_rootent: dw 0                ; +17
bpb_tot16:   dw 0                ; +19
bpb_media:   db 0                ; +21
bpb_fatsz:   dw 0                ; +22
bpb_spt:     dw 0                ; +24
bpb_heads:   dw 0                ; +26
    times BPB_END - ($ - $$) db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax                  ; SS load masks interrupts for one instruction
    mov sp, STACK_TOP           ; on an 8086, so the pair is atomic
    sti
    cld

    mov [drive], dl             ; what the BIOS said...
    mov [drive0], dl            ; ...and a copy the fallback cannot overwrite,
                                ; because the REPORT wants both

    ; --- where does the payload start? (SPEC.md 19.3) -----------------------
    ; It is the first file in the data area, laid down contiguously by
    ; os88disk.py, so its LBA is where the data area begins:
    ;     rsvd + nfat * fatsz + ceil(rootent * 32 / 512)
    mov al, [bpb_nfat]
    xor ah, ah
    mul word [bpb_fatsz]
    add ax, [bpb_rsvd]
    mov bx, ax
    mov ax, [bpb_rootent]
    add ax, 15
    mov cl, 4
    shr ax, cl                  ; 16 directory entries to a sector
    add ax, bx
    mov [lba], ax
    mov [lba0], ax

    ; --- reset, then read SECS sectors, ONE AT A TIME -----------------------
    xor ah, ah
    mov dl, [drive]
    int 0x13

    mov word [dest], PAYLOAD_SEG
    mov cx, SECS
.next:
    push cx
    call read1
    pop cx
    mov al, '.'                 ; one dot a sector: a hang is then placed by
    call putc                   ; counting, and the teletype is proven working
    inc word [lba]
    add word [dest], 0x20       ; 32 paragraphs a sector
    loop .next

    ; --- hand over ----------------------------------------------------------
    ; The record goes into the payload's own image at HAND_AT, so this sector
    ; needs no scratch address anywhere in low memory - which matters, because
    ; whether low memory is safe to scribble on is one of the things the
    ; payload is about to ask.
    mov ax, PAYLOAD_SEG
    mov es, ax
    mov di, HAND_AT
    mov ax, 0x4442
    stosw                       ; +0  'BD' - THE MAGIC, and the whole point of
                                ;     it: the same payload also boots behind
                                ;     the SHIPPED stage 1 (make bootdiagx),
                                ;     which writes nothing here, so its
                                ;     absence is how the report knows which
                                ;     loader carried it
    mov al, [drive0]
    stosb                       ; +2  DL as the BIOS handed it over
    mov al, [drive]
    stosb                       ; +3  DL the load actually used
    mov al, [status]
    stosb                       ; +4  the LAST int 13h status seen, 0 if none -
                                ;     every failure overwrites it, which for
                                ;     the three identical 01s the Packard Bell
                                ;     gave was the same answer either way
    mov al, [retries]
    stosb                       ; +5  attempts beyond the first, all sectors
    mov ax, [bpb_spt]
    stosw                       ; +6  sectors per track, from the BPB
    mov ax, [bpb_heads]
    stosw                       ; +8  heads, from the BPB
    mov ax, [lba0]
    stosw                       ; +10 the payload's first LBA
    mov ax, SECS
    stosw                       ; +12 how many sectors it is
    mov al, [fellback]
    stosb                       ; +14 non-zero if the DL fallback fired

    mov dl, [drive]
    jmp PAYLOAD_SEG:0x0000

; -----------------------------------------------------------------------------
; read1 - the sector in [lba] to [dest]:0000. Three attempts with a controller
;         reset between, then the DL fallback, then the failure line.
;
; The fallback is the whole reason this is not four instructions. A BIOS that
; does not set DL at the boot jump is a documented compatibility trap and it is
; invisible from inside: the loader reads whatever DL happened to hold, every
; attempt fails identically, and `Disk error` is the only thing that reaches
; the screen. Trying 0 once - and REPORTING that it was tried - separates that
; from a drive that genuinely will not read.
; -----------------------------------------------------------------------------
read1:
    mov si, 3
.attempt:
    mov ax, [lba]               ; LBA -> CHS, rebuilt every attempt because a
    xor dx, dx                  ; controller reset owes us nothing
    div word [bpb_spt]
    inc dx
    mov cl, dl                  ; CL = sector (1-based)
    xor dx, dx
    div word [bpb_heads]
    mov ch, al                  ; CH = cylinder (never over 255 on a floppy)
    mov dh, dl                  ; DH = head
    mov es, [dest]
    xor bx, bx
    mov dl, [drive]
    mov ax, 0x0201              ; AH=02 read, AL=1 sector - the smallest
    int 0x13                    ; request any BIOS can be asked for
    jnc .done
    mov [status], ah            ; the reset below destroys it, and it IS the
    inc byte [retries]          ; diagnosis
    xor ah, ah
    mov dl, [drive]
    int 0x13
    dec si
    jnz .attempt

    cmp byte [drive], 0         ; --- the DL fallback, once by construction --
    je .dead                    ; already 0: there is nothing left to try, and
                                ; this is also what stops the retry from
                                ; looping - the switch below can only be taken
                                ; while DL is still non-zero
    mov byte [drive], 0
    mov byte [fellback], 1
    xor ax, ax                  ; AH=00h reset, DL=0 - the drive we are
    xor dx, dx                  ; switching TO, not the one that just failed
    int 0x13
    jmp short read1

.dead:
    mov si, msg_bd
    call puts
    mov al, [drive0]
    call hex8
    mov al, '/'
    call putc
    mov al, [drive]
    call hex8
    mov si, msg_st
    call puts
    mov al, [status]
    call hex8
    mov si, msg_lba
    call puts
    mov ax, [lba]
    call hex8x                  ; high byte...
.stop:
    cli
    hlt
    jmp short .stop
.done:
    ret

; --- the smallest printers that will do --------------------------------------
puts:                           ; DS:SI, NUL-terminated
    lodsb
    test al, al
    jz putc.out
    call putc
    jmp short puts

putc:                           ; AL
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop bx
    pop ax
.out:
    ret

hex8x:                          ; AX as four digits
    push ax
    mov al, ah
    call hex8
    pop ax
hex8:                           ; AL as two digits
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
    add al, 0x90                ; the classic six bytes: 0..15 -> '0'..'F'
    daa
    adc al, 0x40
    daa
    jmp short putc

drive:      db 0
drive0:     db 0
status:     db 0
retries:    db 0
fellback:   db 0
lba:        dw 0
lba0:       dw 0
dest:       dw 0
msg_bd:     db 13, 10, 'BD dl=', 0
msg_st:     db ' st=', 0
msg_lba:    db ' lba=', 0

    times 510 - ($ - $$) db 0
    dw 0xAA55
