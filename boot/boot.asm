; =============================================================================
; os8088 - boot sector
;
; The BIOS loads this single 512-byte sector to 0000:7C00 and jumps to it with
; DL set to the drive we came from. Our only job is to pull the kernel off the
; floppy into 0060:0000 and hand control over. The loading screen itself lives
; in the kernel (kernel/splash.inc, SPEC.md 15): once its opening SPL_RESIDENT
; sectors are aboard we far-call its pinned 0060:0008 entry after every
; further sector, and it draws the welcome dialog, progress bar and spinning
; 8088. Strictly event-driven - a completed read is the only thing that ever
; advances it, so the animation costs no load time.
;
; **We move out of the way first, to the TOP OF CONVENTIONAL RAM.** The kernel
; lands at linear 0x00600 and runs up past 80KB (SPEC.md 2), so it covers
; 0x7C00 - this sector, and the stack below it - long before the last sector
; arrives. `entry` therefore copies these 512 bytes out of its way and jumps
; there. The copy keeps the SAME OFFSET, so every label in this file still
; resolves at org 0x7C00 and the only thing that changes is which segment the
; segment registers name.
;
; The destination is COMPUTED, from int 12h, and lands the sector in the last
; 512 bytes the machine has (SPEC.md 2.7). It used to be a fixed BOOT_RELOC,
; which meant the kernel's footprint was capped by where this sector happened
; to sit - a constant mirrored in kernel/kernel.asm and asserted there, and
; the guard that eventually bound the whole kernel. Placed at the ceiling
; instead, the two can only meet on a machine too small to run the OS at all,
; so what caps the kernel is now a stated minimum machine and not an address.
;
; Trusting int 12h is not a new dependency on the machine: DOS loads itself
; and everything above it from the same number, so a BIOS that lies about it
; (an XT counts RAM from its DIP switches) is a machine that cannot run DOS
; correctly either. mem_init reads the same call for the top of the heap, so
; both ends of the system agree by construction.
;
; Assembled with -DKERNEL_SECTORS=<n> by the Makefile, which measures the
; built kernel so we never read more sectors than exist.
; =============================================================================

bits 16
org 0x7C00

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 16
%endif

; Floppy geometry. Defaults describe a 1.44MB 3.5" disk; the Makefile
; overrides them to 9/2 for the 360KB 5.25" build that 8086-era machines
; can actually read.
%ifndef SPT
%define SPT 18
%endif
%ifndef HEADS
%define HEADS 2
%endif

KERNEL_SEG   equ 0x0060         ; kernel lands at linear 0x00600, the first
                                ; paragraph above the BIOS data area. Mirrored
                                ; in kernel/kernel.asm, which asserts that the
                                ; kernel ends clear of our relocated stack
STACK_TOP    equ 0x7C00         ; stack grows down from our own base, so it
                                ; relocates with us and stays out of the way
BOOT_STACK   equ 2048           ; stack room below us, and the one figure
                                ; still shared with kernel/kernel.asm - which
                                ; reserves the same 2,048 in guard 5. The two
                                ; are asked about different machines (this
                                ; one; the smallest supported one) but it is
                                ; the same stack, so they are the same number
RELOC_ADJ    equ 0x07E0         ; int 12h answers KB; KB*64 is the paragraph
                                ; ONE PAST the last byte of conventional RAM,
                                ; so the segment we want is that minus our own
                                ; offset (0x7C00 = 0x7C0 paragraphs) and minus
                                ; our own 512 bytes (0x20 more), which is what
                                ; puts our LAST byte on the machine's last
                                ; byte. Getting only the first term right
                                ; boots on any machine with a spare page above
                                ; int 12h's answer and dies on one without
SPLASH_OFF   equ 0x0008         ; the kernel's boot splash far entry (SPEC.md 15)
SPL_RESIDENT equ 8              ; splash is fully aboard after this many
                                ; sectors - must match kernel/splash.inc

BPB_END      equ 62             ; where a DOS BPB stops and our code starts
DPT_AT       equ 0x0580         ; 0000:0580 - where we put our copy of the
                                ; diskette parameter table (SPEC.md 18.93).
                                ; ABOVE the BIOS data area (which ends at
                                ; 0x4FF) and clear of every documented use of
                                ; the 0x500 page - print-screen status at
                                ; 0x500, DOS's single-drive byte at 0x504,
                                ; BASIC at 0x510 - and BELOW KERNEL_SEG, so
                                ; nothing the kernel or its heap can ever
                                ; claim reaches it and it needs no restoring

; -----------------------------------------------------------------------------
; The first 62 bytes are NOT ours. tools/os88disk.py writes a full FAT12 BPB
; over them when it builds the image (SPEC.md 19.3), because the OS disk is a
; real FAT12 volume: the kernel is an ordinary FILE in the data area, and this
; sector finds it by doing the arithmetic the BPB describes.
;
; **The kernel used to live in the reserved area** - sectors 1..K, covered by
; BPB_RsvdSecCnt - and the read below was a flat `LBA 1..K`. That is a legal
; use of the field and no reader that honours it was ever confused, but DOS
; does not honour it on a floppy: it builds a floppy's BPB from its own table
; of standard formats, puts the FAT and root directory at the standard
; offsets, and reads the kernel as file system. Verified on PC-DOS 3.30 and
; MS-DOS 5.00 alike (SPEC.md 19.3).
;
; The fields below are named rather than left as a hole, because the load
; needs four of them. The jump is `short`, not `near`, because that is what
; BS_jmpBoot's first-byte test looks for (mount rule 2, SPEC.md 18.2) - and
; it is our own mount code that would refuse the disk otherwise.
; -----------------------------------------------------------------------------
    jmp short entry
    nop
bpb_oem:     times 8 db 0        ; +3
bpb_bps:     dw 0                ; +11  bytes per sector (always 512 here)
bpb_spc:     db 0                ; +13
bpb_rsvd:    dw 0                ; +14  reserved sectors - 1 on a normal
                                 ;      volume, and 1 here now
bpb_nfat:    db 0                ; +16
bpb_rootent: dw 0                ; +17
bpb_tot16:   dw 0                ; +19
bpb_media:   db 0                ; +21
bpb_fatsz:   dw 0                ; +22
    times BPB_END - ($ - $$) db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, STACK_TOP

    ; --- t = 0 for the boot timer (SPEC.md 15.4) -----------------------------
    ; The BIOS tick at 0040:006C, read HERE because everything expensive is
    ; below it: this sector's own read of the kernel is most of a boot. BP
    ; carries it through the relocation and the whole load - nothing in this
    ; sector uses BP - and it is handed to the kernel at the far jump.
    mov bp, [0x046C]

    ; --- relocate: same offset, new segment ---------------------------------
    ; Nothing above this point touches memory through a label, so it runs
    ; correctly at 0000:7C00 where the BIOS put it. After the far jump CS is
    ; the top of memory and every org-0x7C00 label below is correct again.
    ;
    ; The three instructions that turn int 12h's KB into a paragraph count are
    ; mem_init's own (kernel/memory.inc), deliberately: the sector and the
    ; heap must agree about where memory ends, and the cheapest way to
    ; guarantee that is for both to ask the same question the same way.
    mov si, STACK_TOP
    mov di, STACK_TOP
%ifdef RAM_KB
    mov ax, RAM_KB              ; make RAMKB=<n>: pretend, because QEMU always
%else                           ; answers 639 and the interesting cases are
    int 0x12                    ; the small ones (docs/TESTING.md)
%endif
                                ; AX = conventional memory, KB
    mov cl, 6
    shl ax, cl                  ; AX = KB*64 = one paragraph past the top
    sub ax, RELOC_ADJ           ; ...and back off our offset and our own size

    ; Would the kernel's own sectors land on us? Then this machine cannot run
    ; the OS (SPEC.md 2.7). Refuse here rather than be overwritten mid-load,
    ; which is a hang with a half-drawn splash and nothing to read. The same
    ; compare catches a BIOS that under-reports (an XT counts RAM from its
    ; DIP switches, so a memory board the switches do not mention is a
    ; machine with plenty of RAM and a small answer) and a shift that
    ; overflowed a claim of 1MB or more: both land below the kernel, which is
    ; exactly what this asks about.
    ;
    ; The bound is our STACK's bottom, not our own base: it is live for the
    ; whole load - every push, every int 13h, every splash call - so the read
    ; has to clear that too, and BOOT_STACK folds into the immediate for
    ; nothing. What it does NOT cover is the kernel's whole span: .lowbss and
    ; the task stacks above the image are only in use after handoff, when we
    ; are dead memory, so they are guard 5's business against the smallest
    ; SUPPORTED machine and not this compare's against the actual one.
    cmp ax, KERNEL_SEG + KERNEL_SECTORS*32 + BOOT_STACK/16
    jb .nomem
    mov es, ax
    mov cx, 256
    cld
    rep movsw
    push es                     ; a computed far jump: the 8086 has no `push
    mov ax, .moved              ; imm` and `jmp seg:off` takes a constant, so
    push ax                     ; the target goes on the stack and comes back
    retf                        ; off it as CS:IP
.nomem:
    mov si, msg_mem             ; DS is 0 and we are still where the BIOS put
    call print                  ; us, so every label here resolves
    jmp read_run.halt
.moved:
    mov ax, cs
    mov ds, ax
    mov ss, ax                  ; the stack comes with us: same offset, so it
    mov sp, STACK_TOP           ; grows down from our own base, which is now
                                ; 512 bytes below the top of the machine
    sti
    cld

    mov [boot_drive], dl        ; BIOS told us where we came from; believe it

    int 0x11                    ; clean, cursorless text screen while the
    and al, 0x30                ; first sectors land; the kernel splash takes
    cmp al, 0x30                ; over in graphics the moment it is resident.
    mov ax, 0x0003              ; A monochrome card has no mode 3 - equipment
    jne .tmode                  ; word bits 5:4 = 11b means mode 7 instead.
    mov al, 0x07
.tmode:
    int 0x10
    mov ah, 0x01
    mov cx, 0x2000
    int 0x10

    ; --- reset the drive before the first read ------------------------------
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

    ; --- take over the diskette parameter table (SPEC.md 18.92/18.93) -------
    ; int 1Eh is a POINTER, not a handler: it names an 11-byte table the BIOS
    ; re-reads on every floppy operation, and byte 4 of it is EOT - the last
    ; sector number the FDC may touch on a track. The IBM PC and XT ROMs say
    ; 8, from the 8-sector diskettes of DOS 1.x, and the BIOS issues READ with
    ; the multi-track bit set, so a run reaching sector 9 does not stop - it
    ; flips to the other head and returns ITS sectors, with CF=0 and the full
    ; count. Every DOS since 1982 has replaced this table at boot.
    ;
    ; The machine's own table is COPIED and one byte patched: the step rate,
    ; head load/unload, motor timings and gap lengths in it belong to these
    ; drives and are not ours to guess. With EOT correct the track bound in
    ; read_run IS the EOT bound, which is why that routine needs no test of
    ; its own - and why a 9-sector track costs one command instead of two.
    ;
    ; No guard on the vector, deliberately: the BIOS read THIS SECTOR through
    ; it, so a machine that could not use it is a machine that never got here.
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, DPT_AT
    lds si, [0x0078]            ; DS:SI = the BIOS's table
    mov cx, 11
    rep movsb                   ; ...ES:DI = ours
    xor ax, ax
    mov ds, ax
    mov word [0x0078], DPT_AT   ; and the vector names it from here
    mov [0x007A], ax
    mov byte [es:DPT_AT + 4], SPT   ; EOT = this disk's sectors per track
    pop es
    pop ds

    ; --- copy KERNEL_SECTORS sectors, the kernel FILE, to KERNEL_SEG:0000 ---
    ; It starts at the first data cluster, so its LBA is where the data area
    ; begins and the BPB above says where that is:
    ;
    ;     rsvd + nfat * fatsz + ceil(rootent * 32 / 512)
    ;
    ; Derived here rather than injected, so nothing outside os88disk.py has to
    ; know a geometry - the same reason the sector count is measured from the
    ; kernel rather than guessed. os88disk.py allocates the kernel FIRST and
    ; CONTIGUOUSLY (cluster 2 onward, never scrambled), which is what lets a
    ; flat run of reads stand in for walking its cluster chain in 512 bytes.
    ;
    ; The destination segment advances 0x20 paragraphs per SECTOR with BX held
    ; at zero, so the pointer can never wrap inside a segment and every
    ; transfer starts at a 512-aligned linear address. A RUN can still cross a
    ; 64KB DMA page, though, which is one of the four bounds read_run applies.
    mov al, [bpb_nfat]
    xor ah, ah
    mul word [bpb_fatsz]        ; AX = both FATs (DX is 0: 2 x 9 at the most)
    add ax, [bpb_rsvd]
    mov bx, ax
    mov ax, [bpb_rootent]       ; 32 bytes an entry, 16 to a sector
    add ax, 15
    mov cl, 4
    shr ax, cl
    add ax, bx
    mov [lba], ax
    mov word [left], KERNEL_SECTORS

.load_next:
    mov es, [dest_seg]
    xor bx, bx
    call read_run               ; AX = sectors this call actually moved
    add [lba], ax
    add [done], ax
    sub [left], ax
    mov cl, 5
    shl ax, cl                  ; 0x20 paragraphs a sector...
    add [dest_seg], ax          ; ...so the destination follows the run

    mov ax, [done]              ; tick the splash once it is fully resident:
    cmp ax, SPL_RESIDENT        ; AX = sectors done, DX = total (SPEC.md 15).
    jb .no_tick                 ; Counted on its own, NOT from [lba] - that is
    mov dx, KERNEL_SECTORS      ; an absolute LBA into the data area now, and
    call KERNEL_SEG:SPLASH_OFF  ; it starts past SPL_RESIDENT on both
.no_tick:                       ; geometries, so the splash would be called
                                ; before a byte of it had landed. ONE call a
                                ; run, not one a sector: the bar is an
                                ; absolute position, so it needs no repeats
    cmp word [left], 0
    jne .load_next

    ; --- hand off ------------------------------------------------------------
    mov ax, KERNEL_SEG          ; the boot timer's t=0, into the fixed word the
    mov es, ax                  ; kernel keeps for it (SPEC.md 15.4). It has to
    mov [es:0x000C], bp         ; be written AFTER the load, or the sectors
                                ; landing here would overwrite it
    mov dl, [boot_drive]        ; kernel may want to know the boot drive
    jmp KERNEL_SEG:0x0000

; -----------------------------------------------------------------------------
; read_run - read as many sectors from [lba] to ES:0000 as one int 13h may
;            carry (SPEC.md 18.93). Retries three times, resetting the
;            controller between attempts, because floppy reads fail
;            spuriously often enough to matter.
; in:  ES = destination segment, [left] = sectors still wanted
; out: AX = sectors actually transferred, 1..run
; clobbers: ax, bx, cx, dx, si, di
;
; This used to be read_sector, AL=1, and on a real drive that costs a whole
; REVOLUTION per sector: by the time the next command reaches the controller
; the sector after it has already passed the head. 131 sectors at the
; pre-AL-fix 238ms
; (PERFORMANCE.md) is over thirty seconds of the boot, and it is the single
; largest cost in it.
;
; A run stops at the first of three bounds, and the third is the one that is
; easy not to think of:
;   1. the end of the track - a CHS call must not cross one, and since entry
;      set the diskette parameter table's EOT to SPT (SPEC.md 18.92/18.93)
;      this is the FDC's bound too, so there is no separate test for it
;   2. the sectors still wanted
;   3. the 64KB DMA page - a single 512-aligned sector cannot cross one, a
;      run can, and the controller answers a straddle with error 09h
; -----------------------------------------------------------------------------
read_run:
    ; --- 1 and 2: the track, and what is left to read -----------------------
    ; The track bound is ALSO the EOT bound: entry above set the table's EOT
    ; to SPT, so the FDC stops exactly where a CHS call had to anyway.
    mov ax, [lba]
    xor dx, dx
    mov bx, SPT
    div bx                      ; DX = sector - 1, its index in the track
    mov di, SPT
    sub di, dx                  ; DI = sectors to the end of this track
    cmp di, [left]
    jbe .page
    mov di, [left]

    ; --- 3: the 64KB DMA page ----------------------------------------------
.page:
    mov ax, es
    mov cl, 4
    shl ax, cl                  ; the low 16 bits of the physical address
    neg ax                      ; bytes to the page end (0 = a whole page)
    jz .have
    mov cl, 9
    shr ax, cl
    cmp ax, di
    jae .have
    or ax, ax
    jz .one                     ; unreachable from a 512-aligned base, which
    mov di, ax                  ; every destination here is
    jmp short .have
.one:
    mov di, 1
.have:
%ifdef FLOPPY_ONE
    mov di, 1                   ; FLOPPY1=1: one sector a call, the transfer
%endif                          ; as it was before any of this
    mov [run], di
    mov si, 3                   ; attempts

.attempt:
    ; LBA -> CHS, rebuilt every attempt because a controller reset owes us
    ; nothing.  sector = lba % SPT + 1, head = (lba / SPT) % HEADS,
    ;                                cylinder = (lba / SPT) / HEADS
    mov ax, [lba]
    xor dx, dx
    mov bx, SPT
    div bx
    inc dx
    mov cl, dl                  ; CL = sector (1-based)
    xor dx, dx
    mov bx, HEADS
    div bx
    mov ch, al                  ; CH = cylinder (low 8 bits; we never exceed 255)
    mov dh, dl                  ; DH = head
    xor bx, bx                  ; ES:BX - the offset is always zero
    mov dl, [boot_drive]
    mov ax, [run]               ; AL = the run...
    mov ah, 0x02                ; ...AH = 02 read
    int 0x13
    jnc .done

%ifdef BOOT_DIAG
    mov [diag_ah], ah           ; BOOTDIAG=1: the reset below destroys the
%endif                          ; status, and the status IS the diagnosis
    xor ah, ah                  ; reset and try again
    mov dl, [boot_drive]
    int 0x13
    dec si
    jnz .attempt

%ifdef BOOT_DIAG
    mov al, [diag_ah]           ; two hex digits and nothing else: 0C is a
    mov cl, 4                   ; media type the drive could not identify
    shr al, cl                  ; (a 360KB disk in a 1.2MB drive), 04 a
    call .nib                   ; sector the FDC never found (EOT / the
    mov al, [diag_ah]           ; multi-track flip), 09 a transfer that
    and al, 0x0F                ; crossed a 64KB DMA page, 80 a drive that
    call .nib                   ; never answered
    jmp short .halt
.nib:
    add al, 0x90                ; the classic six bytes: 0..15 -> '0'..'F'
    daa
    adc al, 0x40
    daa
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    ret
%else
    mov si, msg_err
    call print
%endif
.halt:
    cli
    hlt
    jmp .halt

.done:
    ; CF=0 IS the BIOS saying the whole request completed, and AL is not -
    ; the same reading dsk_xfer takes (SPEC.md 18.91, PERFORMANCE.md Part 9
    ; Set 16). This used to advance by AL, and on the IBM 5150 a nine-sector
    ; read MOVES ALL NINE and answers AL = 1: the kernel's own transfer was
    ; measured at 148 sectors for a 32-sector file that way, and fixing it
    ; took a 16KB read from 8.29 s to 2.09. THIS loop has the same bug and it
    ; is the whole boot - 140 sectors at a revolution each is 33 of the 40
    ; seconds a 5150 spends booting.
    ;
    ; `make DISKAL=1` restores the old reading, in both loops together.
%ifdef DISK_TRUST_AL
    xor ah, ah
    or al, al
    jnz .clamp                  ; 0 with CF=0 is trusted for one, so the loop
    inc ax                      ; always progresses
.clamp:
    cmp ax, [run]
    jbe .out
%endif
    mov ax, [run]               ; the whole run: CF=0 is the contract
.out:
    ret

; -----------------------------------------------------------------------------
; print - write the NUL-terminated string at DS:SI via BIOS teletype. BL
;         carries the colour so it stays legible if the splash already
;         switched us into mode 12h.
; -----------------------------------------------------------------------------
print:
    push ax
    push bx
    mov bx, 0x000F
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .next
.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
boot_drive  db 0
lba         dw 0
done        dw 0
left        dw 0
run         dw 0
dest_seg    dw KERNEL_SEG

%ifdef BOOT_DIAG
diag_ah     db 0                ; the int 13h status, banked before the reset
%else
msg_err     db 'os8088: disk error', 13, 10, 0
%endif
msg_mem     db 'RAM', 13, 10, 0  ; three characters because three is what is
                                 ; left in 512 bytes, and a machine that says
                                 ; RAM and stops is diagnosable where a black
                                 ; screen is not

; -----------------------------------------------------------------------------
    times 510 - ($ - $$) db 0
    dw 0xAA55
