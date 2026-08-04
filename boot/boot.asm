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
; **We move out of the way first.** The kernel lands at linear 0x00600 and is
; up to 64KB (SPEC.md 2), so it covers 0x7C00 - this sector, and the stack
; below it - long before the last sector arrives. `start` therefore copies
; these 512 bytes to BOOT_RELOC:7C00 and jumps there. The copy keeps the SAME
; OFFSET, so every label in this file still resolves at org 0x7C00 and the
; only thing that changes is which segment the segment registers name.
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
BOOT_RELOC   equ 0x0C00         ; 0x0C00*16 + 0x7C00 = linear 0x13C00: where
                                ; we copy ourselves, above anything the kernel
                                ; can reach. Mirrored in kernel/kernel.asm
STACK_TOP    equ 0x7C00         ; stack grows down from our own base, so it
                                ; relocates with us and stays out of the way
SPLASH_OFF   equ 0x0008         ; the kernel's boot splash far entry (SPEC.md 15)
SPL_RESIDENT equ 6              ; splash is fully aboard after this many
                                ; sectors - must match kernel/splash.inc

BPB_END      equ 62             ; where a DOS BPB stops and our code starts

; -----------------------------------------------------------------------------
; The first 62 bytes are NOT ours. tools/os88disk.py writes a full FAT12 BPB
; over them when it builds the image (SPEC.md 19.3), because the OS disk is a
; real FAT12 volume now: the kernel sits in the RESERVED AREA, sectors 1..K,
; which BPB_RsvdSecCnt covers and no file system structure can ever reach. So
; the raw LBA read below is unchanged and still correct, and at the same time
; DOS, Linux, macOS - and os8088's own file manager and write path - can all
; mount drive A: and see the files after it.
;
; Everything above `entry` therefore has to be exactly the three bytes DOS
; expects (a short jump and a NOP) followed by a hole. The jump is `short`,
; not `near`, because that is what BS_jmpBoot's first-byte test looks for
; (mount rule 2, SPEC.md 18.2) - and it is our own mount code that would
; refuse the disk otherwise.
; -----------------------------------------------------------------------------
    jmp short entry
    nop
    times BPB_END - ($ - $$) db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, STACK_TOP

    ; --- relocate: same offset, new segment ---------------------------------
    ; Nothing above this point touches memory through a label, so it runs
    ; correctly at 0000:7C00 where the BIOS put it. After the far jump CS is
    ; BOOT_RELOC and every org-0x7C00 label below is correct again.
    mov si, STACK_TOP
    mov di, STACK_TOP
    mov ax, BOOT_RELOC
    mov es, ax
    mov cx, 256
    cld
    rep movsw
    jmp BOOT_RELOC:.moved
.moved:
    mov ax, cs
    mov ds, ax
    mov ss, ax                  ; the stack comes with us: same offset, so it
    mov sp, STACK_TOP           ; grows down from linear 0x13400
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

    ; --- copy KERNEL_SECTORS sectors, starting at LBA 1, to KERNEL_SEG:0000 --
    ; The destination segment advances one sector (0x20 paragraphs) per read
    ; with BX held at zero, so the pointer can never wrap inside a segment,
    ; and every transfer is 512 bytes at a 512-aligned linear address - which
    ; is what keeps a single read from ever straddling a 64KB DMA boundary.
    mov word [lba], 1
    mov cx, KERNEL_SECTORS

.load_next:
    push cx
    mov es, [dest_seg]
    xor bx, bx
    call read_sector
    add word [dest_seg], 0x20

    mov ax, [lba]               ; tick the splash once it is fully resident:
    cmp ax, SPL_RESIDENT        ; AX = sectors done, DX = total (SPEC.md 15)
    jb .no_tick
    mov dx, KERNEL_SECTORS
    call KERNEL_SEG:SPLASH_OFF
.no_tick:

    inc word [lba]
    pop cx
    loop .load_next

    ; --- hand off ------------------------------------------------------------
    mov dl, [boot_drive]        ; kernel may want to know the boot drive
    jmp KERNEL_SEG:0x0000

; -----------------------------------------------------------------------------
; read_sector - read the sector in [lba] to ES:BX. Retries three times,
;               resetting the controller between attempts, because floppy
;               reads fail spuriously often enough to matter.
; clobbers: ax, cx, dx, di
; -----------------------------------------------------------------------------
read_sector:
    mov di, 3

.attempt:
    ; LBA -> CHS.  sector = lba % SPT + 1, head = (lba / SPT) % HEADS,
    ;              cylinder = (lba / SPT) / HEADS
    push bx
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
    pop bx

    mov dl, [boot_drive]
    mov ax, 0x0201              ; AH=02 read, AL=1 sector
    int 0x13
    jnc .done

    xor ah, ah                  ; reset and try again
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .attempt

    mov si, msg_err
    call print
.halt:
    cli
    hlt
    jmp .halt

.done:
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
dest_seg    dw KERNEL_SEG

msg_err     db 'os8088: disk error', 13, 10, 0

; -----------------------------------------------------------------------------
    times 510 - ($ - $$) db 0
    dw 0xAA55
