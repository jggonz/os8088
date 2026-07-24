; =============================================================================
; jop - boot sector
;
; The BIOS loads this single 512-byte sector to 0000:7C00 and jumps to it with
; DL set to the drive we came from. Our only job is to pull the kernel off the
; floppy into 1000:0000 and hand control over.
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

KERNEL_SEG  equ 0x1000          ; kernel lands at linear 0x10000
STACK_TOP   equ 0x7C00          ; stack grows down, away from our code

; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti
    cld

    mov [boot_drive], dl        ; BIOS told us where we came from; believe it

    mov si, msg_load
    call print

    ; --- reset the drive before the first read ------------------------------
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

    ; --- copy KERNEL_SECTORS sectors, starting at LBA 1, to KERNEL_SEG:0000 --
    ; We bump ES by one sector (0x20 paragraphs) per read and keep BX at zero,
    ; so the destination pointer can never wrap inside a segment.
    mov word [lba], 1
    mov ax, KERNEL_SEG
    mov es, ax
    xor bx, bx
    mov cx, KERNEL_SECTORS

.load_next:
    push cx
    call read_sector
    pop cx

    mov ax, es
    add ax, 0x20
    mov es, ax
    inc word [lba]
    loop .load_next

    mov si, msg_ok
    call print

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
    jmp halt

.done:
    ret

; -----------------------------------------------------------------------------
; print - write the NUL-terminated string at DS:SI via BIOS teletype
; -----------------------------------------------------------------------------
print:
    push ax
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    pop ax
    ret

halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
boot_drive  db 0
lba         dw 0

msg_load    db 'jop: loading', 13, 10, 0
msg_ok      db 'jop: ok', 13, 10, 0
msg_err     db 'jop: disk error', 13, 10, 0

; -----------------------------------------------------------------------------
    times 510 - ($ - $$) db 0
    dw 0xAA55
