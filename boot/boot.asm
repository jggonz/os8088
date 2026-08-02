; =============================================================================
; os8088 - boot sector
;
; The BIOS loads this single 512-byte sector to 0000:7C00 and jumps to it with
; DL set to the drive we came from. Our only job is to pull the kernel off the
; floppy into 1000:0000 and hand control over. The loading screen itself lives
; in the kernel (kernel/splash.inc, SPEC.md 15): once its opening SPL_RESIDENT
; sectors are aboard we far-call its pinned 1000:0008 entry after every
; further sector, and it draws the welcome dialog, progress bar and spinning
; 8088. Strictly event-driven - a completed read is the only thing that ever
; advances it, so the animation costs no load time.
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

KERNEL_SEG   equ 0x1000         ; kernel lands at linear 0x10000
STACK_TOP    equ 0x7C00         ; stack grows down, away from our code
SPLASH_OFF   equ 0x0008         ; the kernel's boot splash far entry (SPEC.md 15)
SPL_RESIDENT equ 6              ; splash is fully aboard after this many
                                ; sectors - must match kernel/splash.inc

; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, STACK_TOP
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
    ; with BX held at zero, so the pointer can never wrap inside a segment.
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
