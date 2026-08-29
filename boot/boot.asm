; =============================================================================
; os8088 - boot sector (STAGE 1)
;
; The BIOS loads this single 512-byte sector to 0000:7C00 and jumps to it with
; DL set to the drive we came from. Its whole job is now: move out of the way,
; find the data area, read the first BOOT2_SECS sectors of KERNEL.SYS - which
; are the REST OF THE LOADER, boot/boot2.asm, assembled into the kernel image
; at file offset 0 (SPEC.md 2.9) - and jump into them.
;
; **The loader left because 510 bytes had fifteen spare.** The int 1Eh
; parameter table, read_run's four bounds, the 286 head-cross gate, 18.93.1's
; canary and its shorten-and-reload are all stage 2's now, where there is room
; for them and where BOOTDIAG=1 no longer has to give the canary up for space.
; What is left fits in about two thirds of the sector.
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
; 512 bytes the machine has (SPEC.md 2.7). Trusting int 12h is not a new
; dependency on the machine: DOS loads itself and everything above it from the
; same number, and mem_init reads the same call for the top of the heap, so
; both ends of the system agree by construction.
;
; STAGE 2 LANDS JUST BELOW OUR OWN STACK, at a segment computed from ours - so
; it needs no constant of its own, and the .nomem compare below still bounds
; it: that test is stricter than stage 2's own requirement by 0x780
; paragraphs, because it asks the kernel to clear our BASE and stage 2 sits
; above that.
;
; Assembled with -DKERNEL_SECTORS=<n>, -DBOOT2_SECS=<n> and -DKSIG=<word> by
; the Makefile, which measures the built kernel: the signature in particular
; is read OUT of that kernel, so a kernel carrying it would have to be
; assembled twice to reach a fixed point (SPEC.md 18.93.1). We are built after
; it, so we carry it and hand it over in DI.
; =============================================================================
cpu 8086                        ; SPEC.md 1: this sector runs on the 5150's
                                ; 8088 before anything has probed anything,
                                ; so NASM refuses a 286+ encoding here too.

bits 16
org 0x7C00

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 16
%endif
%ifndef BOOT2_SECS
%define BOOT2_SECS 2            ; must match kernel/kernel.asm's own constant
%endif

; --- FLAT_PAYLOAD: the four diagnostics that boot through this sector --------
; rdiag, comscan, lptlink and dosstub are standalone .bin images loaded whole
; to KERNEL_SEG and run at offset 0 - they have no stage 2 and want none, being
; a handful of sectors each. `-DFLAT_PAYLOAD` is that: the same read loop, told
; to fetch KERNEL_SECTORS instead of BOOT2_SECS and to jump to what it read.
; Without it this sector is the KERNEL's, and the kernel's alone.
%ifndef KSIG
%define KSIG 0                  ; no signature: stage 2 skips the canary
%endif

; Floppy geometry. Defaults describe a 1.44MB 3.5" disk; the Makefile
; overrides them to 9/2 for the 360KB 5.25" build that 8086-era machines
; can actually read. They are handed to stage 2 in CX and DH, because the
; KERNEL is one binary for all three geometries and cannot have them as
; immediates the way this sector does.
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
                                ; reserves the same 2,048 in guard 5
RELOC_ADJ    equ 0x07E0         ; int 12h answers KB; KB*64 is the paragraph
                                ; ONE PAST the last byte of conventional RAM,
                                ; so the segment we want is that minus our own
                                ; offset (0x7C00 = 0x7C0 paragraphs) and minus
                                ; our own 512 bytes (0x20 more), which is what
                                ; puts our LAST byte on the machine's last byte
STAGE2_ADJ   equ 0x07C0 - BOOT_STACK/16 - BOOT2_SECS*32
                                ; ...and stage 2 goes immediately below that
                                ; stack: our segment plus our offset in
                                ; paragraphs, less the stack and less stage 2
                                ; itself, so its LAST byte is the stack's last

DPT_AT       equ 0x0580         ; 0000:0580 - our copy of the diskette
                                ; parameter table, at the address stage 2 uses
                                ; and for its reasons (boot/boot2.asm): above
                                ; the BIOS data area, below KERNEL_SEG, and
                                ; clear of every documented use of the 0x500
                                ; page, so nothing the kernel or its heap can
                                ; claim reaches it

BPB_END      equ 62             ; where a DOS BPB stops and our code starts

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
    ; below it. BP carries it through the relocation and the read - nothing in
    ; this sector uses BP - and stage 2 writes it to 0060:000C after the load,
    ; because the sectors landing there would otherwise overwrite it.
    mov bp, [0x046C]

    ; --- relocate: same offset, new segment ---------------------------------
    ; Nothing above this point touches memory through a label, so it runs
    ; correctly at 0000:7C00 where the BIOS put it. After the far jump CS is
    ; the top of memory and every org-0x7C00 label below is correct again.
    mov si, STACK_TOP
    mov di, si
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
    ; compare catches a BIOS that under-reports and a shift that overflowed a
    ; claim of 1MB or more: both land below the kernel, which is exactly what
    ; this asks about.
    ;
    ; The bound is our STACK's bottom, not our own base: it is live for the
    ; whole load - every push, every int 13h, every splash call - so the read
    ; has to clear that too. Stage 2 sits ABOVE that bound, so this covers it
    ; as well and needs no second term.
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
    jmp .halt                   ; us, so every label here resolves
.moved:
    mov ax, cs
    mov ds, ax
    mov ss, ax                  ; the stack comes with us: same offset, so it
    mov sp, STACK_TOP           ; grows down from our own base, which is now
                                ; 512 bytes below the top of the machine
    sti
    cld

    mov [boot_drive], dl        ; BIOS told us where we came from; believe it

    ; --- TAKE OVER THE DISKETTE PARAMETER TABLE (SPEC.md 18.92, 2.9.8) -------
    ; **BEFORE THE FIRST MULTI-SECTOR READ, AND THIS SECTOR NOW MAKES ONE.**
    ; int 1Eh is a POINTER to an 11-byte table the BIOS re-reads on every
    ; floppy operation, and byte 4 of it is EOT - the last sector number the
    ; FDC may touch on a track. THE IBM PC AND XT ROMS SAY 8, from the
    ; 8-sector diskettes of DOS 1.x, and a 360KB disk has NINE.
    ;
    ; Stage 2 has patched this since SPEC.md 18.92 and that was enough for as
    ; long as this sector read FOUR sectors: LBA 12 on a 360KB volume is
    ; cylinder 0, head 1, sector 4, so a four-sector run ends at sector 7 and
    ; never meets EOT. SPEC.md 2.9.6 made it THIRTEEN, the first run became
    ; sectors 4..9, and on a genuine XT ROM the ninth was silently not
    ; transferred - CF = 0, the full count reported, one sector of the blob
    ; left as whatever was in RAM. Field-reported as `Loader checksum 589C`,
    ; which is 2.9.7's sum less exactly that sector's own.
    ;
    ; The machine's own table is COPIED and one byte patched: the step rate,
    ; head load/unload, motor timings and gap lengths in it belong to these
    ; drives and are not ours to guess. Stage 2 still does the same thing to
    ; the same address, which is now idempotent rather than necessary.
    push ds
    xor ax, ax
    mov es, ax
    lds si, [es:0x0078]         ; DS:SI = the BIOS's table
    mov di, DPT_AT
    mov cx, 11
    rep movsb                   ; ...ES:DI = ours, at 0000:0580
    pop ds
    mov al, SPT
    mov [es:DPT_AT + 4], al     ; EOT = this disk's sectors per track
    mov word [es:0x0078], DPT_AT
    mov [es:0x007A], cx         ; CX is 0: `rep movsb` counted it down

    ; --- reset the drive before the first read ------------------------------
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

    ; --- where does KERNEL.SYS start? (SPEC.md 19.3) ------------------------
    ; It is the first file in the data area, so its LBA is where the data area
    ; begins and the BPB above says where that is:
    ;
    ;     rsvd + nfat * fatsz + ceil(rootent * 32 / 512)
    ;
    ; Derived here rather than injected, so nothing outside os88disk.py has to
    ; know a geometry. os88disk.py allocates the kernel FIRST and CONTIGUOUSLY
    ; (cluster 2 onward, never scrambled), which is what lets a flat run of
    ; reads stand in for walking its cluster chain in 512 bytes.
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
    mov [lba0], ax
    mov [lba], ax
%ifdef FLAT_PAYLOAD
    mov word [left], KERNEL_SECTORS
    mov word [dest], KERNEL_SEG
%else
    mov word [left], BOOT2_SECS

    mov ax, cs                  ; stage 2's home, below our own stack
    add ax, STAGE2_ADJ
    mov [dest], ax
%endif

    ; --- read stage 2 -------------------------------------------------------
    ; TWO bounds where read_run needs four: the TRACK, and the 64KB DMA page.
    ; There is no cylinder run to win - this is the very start of a file - and
    ; nothing to be left over, the loop ending when [left] does. Three
    ; attempts, with a controller reset between, for read_run's reason.
    ;
    ; THE PAGE BOUND IS NOT DECORATION. The blob lands just under the top of
    ; conventional RAM, in [top-8192, top-2560), so a 64KB boundary falls
    ; inside it whenever int 12h's KB total is 3..8 more than a multiple of 64
    ; - and the controller answers a straddling transfer with error 09h, which
    ; through the retry above is "Disk error" and a halt. No machine we support
    ; reports such a total; the window was two values wide when the blob was
    ; 2,560 bytes and is six now that SPEC.md 2.9.6 has put the boot overlay in
    ; it, and twenty bytes of a sector with 132 free is the wrong thing to be
    ; clever about.
.next:
    mov ax, [lba]               ; LBA -> CHS
    xor dx, dx
    mov bx, SPT
    div bx
    inc dx
    mov cl, dl                  ; CL = sector (1-based)
    xor dx, dx
    mov bx, HEADS
    div bx
    mov ch, al                  ; CH = cylinder (never over 255 on a floppy)
    mov dh, dl                  ; DH = head
    mov ax, SPT                 ; ...and the run, bounded by the track
    sub al, cl
    inc al
    cmp ax, [left]
    jbe .run
    mov ax, [left]
.run:
    mov [runsz], ax
    push cx                     ; ...and the 64KB DMA page, read_run's third
    mov ax, [dest]              ; bound in its own words: the low 16 bits of
    mov cl, 4                   ; the physical address, negated, are the bytes
    shl ax, cl                  ; to the page end (0 = a whole page to go).
    neg ax                      ; CX IS LIVE - CL is the sector and CH the
    jz .pgok                    ; cylinder, both set above and both wanted by
    mov cl, 9                   ; the int 13h below - and a shift by more than
    shr ax, cl                  ; one needs CL on an 8086, so it is banked.
    cmp ax, [runsz]             ; The quotient is never zero: [dest] is always
    jae .pgok                   ; 512-aligned here, so the remainder is a
    mov [runsz], ax             ; multiple of 512 and the shift can only reach
.pgok:                          ; 0 from an AX the jz has already taken
    pop cx
    mov si, 3                   ; attempts
.attempt:
    mov es, [dest]
    xor bx, bx
    mov dl, [boot_drive]
    mov ax, [runsz]
    mov ah, 0x02
    int 0x13
    jnc .ok
    xor ah, ah                  ; reset and try again
    mov dl, [boot_drive]
    int 0x13
    dec si
    jnz .attempt
%ifdef BOOT_DIAG
    xor dx, dx                  ; nothing to print after this one (see .tail)
%endif
    mov si, msg_err
    jmp .halt                   ; NEAR: BOOTDIAG=1's checksum report sits
.ok:                            ; between here and .halt (SPEC.md 2.9.7)
    mov ax, [runsz]             ; CF=0 IS the BIOS saying the whole request
    add [lba], ax               ; completed, and AL is not (SPEC.md 18.91)
    sub [left], ax
    mov cl, 5
    shl ax, cl                  ; 0x20 paragraphs a sector
    add [dest], ax              ; ...so the destination follows the run
    cmp word [left], 0          ; ...and the LOOP TEST asks [left] outright.
    jne .next                   ; The sector this replaced banked the flags
                                ; from `sub [left], ax` across its splash call
                                ; with pushf/popf; there is no call here, but
                                ; the two instructions above still clobber
                                ; them - and `add [dest], ax` is non-zero on
                                ; every pass, so a `jne` on ITS flags never
                                ; ends. Three bytes, and the alternative is a
                                ; loader that reads the disk for ever.

%ifndef FLAT_PAYLOAD
%ifdef BLOBSUM
    ; --- IS WHAT WE READ WHAT WE ASKED FOR? (SPEC.md 2.9.7) -----------------
    ; The kernel's own load has had 18.93.1's canary since a BIOS was caught
    ; flipping heads a sector early. THE BLOB'S HAD NOTHING - and since 2.9.6
    ; it is thirteen sectors and two int 13h calls where it was four and one.
    ;
    ; The failure it catches is not a disk error, which is why it needs
    ; catching: stage 2 runs, the loading screen draws, and the machine
    ; executes whatever landed in the sectors that did not arrive - hundreds
    ; of instructions later, in a routine with no connection to the disk, as a
    ; fault that differs from boot to boot because uninitialised RAM does.
    ;
    ; A word sum, not a signature at the end: a signature proves the LAST
    ; sector and says nothing about a missing one in the middle. ~20 ms on a
    ; 4.77MHz machine, once, against a boot that is nine seconds.
    push ds
    mov ax, cs
    add ax, STAGE2_ADJ
    mov ds, ax
    xor si, si
    xor bx, bx
    mov cx, BOOT2_SECS * 256
.sum:
    lodsw
    add bx, ax
    loop .sum
    pop ds
    cmp bx, BLOBSUM
    je .blobok
%ifdef BOOT_DIAG
    ; --- BOOTDIAG=1: say WHAT we got, not only that it was wrong ------------
    ; The sum is over the whole blob, so "wrong" does not say which sector -
    ; and the two causes look completely different from the host: a sector
    ; that never landed leaves the region ZERO, so the shortfall is exactly
    ; that sector's own sum and tools can name it; a region something else
    ; overwrote leaves an unrelated figure. Four digits decide between them.
    ;
    ; DX because int 10h wants BX for the attribute, and BX is the sum.
    mov dx, bx
    mov si, msg_blob
    mov bx, 0x0007
.bmsg:
    lodsb
    test al, al
    jz .bhex
    mov ah, 0x0E
    int 0x10
    jmp short .bmsg
.bhex:
    mov cx, 4
.bdig:
    push cx
    mov cl, 4
    rol dx, cl                  ; the top nibble down into DL...
    pop cx
    mov al, dl
    and al, 0x0F
    add al, 0x90                ; ...and the classic six bytes: 0..15 -> '0'..'F'
    daa
    adc al, 0x40
    daa
    mov ah, 0x0E
    int 0x10
    loop .bdig
    jmp .stop
%else
    mov si, msg_blob
    jmp .halt
%endif
.blobok:
%endif
%endif

%ifdef FLAT_PAYLOAD
    mov ax, KERNEL_SEG          ; the payload keeps the boot timer's word too,
    mov es, ax                  ; on the chance that it reads it
    mov [es:0x000C], bp
    mov dl, [boot_drive]
    jmp KERNEL_SEG:0x0000
%else
    ; --- into stage 2 (SPEC.md 2.9) -----------------------------------------
    ; BP is still t=0. The geometry goes in CX and DH because the kernel is
    ; one binary for three disks; the signature in DI because it is read out
    ; of that kernel and cannot be inside it.
    mov cx, SPT
    mov dh, HEADS
    mov dl, [boot_drive]
    mov si, [lba0]
    mov di, KSIG
    mov ax, cs
    add ax, STAGE2_ADJ
    push ax                     ; the computed far jump again: `jmp seg:off`
    xor ax, ax                  ; takes a constant and stage 2's segment is
    push ax                     ; not one
    retf
%endif

.halt:                          ; write the NUL-terminated string at DS:SI and
    mov bx, 0x0007              ; STOP. Neither way in ever comes back, so
.pnext:                         ; nothing is saved or restored.
    lodsb
    test al, al
    jz .stop
    mov ah, 0x0E
    int 0x10
    jmp short .pnext
.stop:
    cli
    hlt
    jmp short .stop

boot_drive: db 0
lba0:       dw 0
lba:        dw 0
left:       dw 0
dest:       dw 0
runsz:      dw 0
msg_mem:    db 'RAM', 0
msg_err:    db 'Disk error', 0
%ifdef BLOBSUM
%ifdef BOOT_DIAG
msg_blob:   db 'Sum ', 0        ; ...and the four digits follow it. SHORTER
                                ; THAN THE PLAIN ONE ON PURPOSE: the digits
                                ; are what the diagnostic build is for, and
                                ; eleven bytes of prose is what they cost in a
                                ; 512-byte sector with seven free
%else
msg_blob:   db 'Loader checksum', 0
%endif
%endif

    times 510 - ($ - $$) db 0
    dw 0xAA55
