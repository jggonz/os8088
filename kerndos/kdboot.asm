; =============================================================================
; kerndos/kdboot.asm - wave 3's gate loader, and nothing else
;
; It reads the kern_dos blob off THIS disk's sectors 1..N into KD_SEG and far
; jumps to it.  It is deliberately NOT boot/boot.asm: that one is a two-stage
; loader with a BPB, a relocation, an int 1Eh patch and a canary, and every
; one of those is a thing that could fail and be blamed on the shim under
; test.  tests/bootdiag/bdboot.asm is the same argument one subject along.
;
; A: is THIS disk and carries no file system at all; B: is the FAT12 volume
; the gate mounts, because a FAT volume's sectors 1..n are its FATs and the
; blob cannot ride there.
; =============================================================================
cpu 8086
bits 16
org 0x7C00

KD_SEG      equ 0x0060
BOOT_SPT    equ 9               ; a 360KB image, which is all this row builds
BOOT_HEADS  equ 2

; **IT RELOCATES ITSELF FIRST, and that is not tidiness.** The BIOS puts a
; boot sector at 0000:7C00, which is 31KB up - and the kern_dos blob is 45KB
; loaded at KD_SEG, so the load WALKS OVER THE LOADER a third of the way
; through. What that looks like is a machine that prints `loading` and then
; nothing at all, with no read error, because the code that would have
; printed one is no longer there. boot/boot.asm relocates for the same
; reason; this is the two-instruction version of it.
RELOC_SEG   equ 0x9000          ; ...well above anything the blob reaches

start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ax, RELOC_SEG           ; **THE SAME OFFSET IN ANOTHER SEGMENT**, so
    mov es, ax                  ; every label below still resolves: a copy to
    mov si, 0x7C00              ; offset 0 would need the whole file re-orged
    mov di, 0x7C00
    mov cx, 256                 ; one sector, as words
    cld
    rep movsw
    jmp RELOC_SEG:.high
.high:
    mov ax, cs
    mov ds, ax                  ; ...and DS follows, every string and variable
    mov es, ax                  ; below being ours
    mov ss, ax                  ; **AND THE STACK, which is the half that is
    mov sp, 0x7C00              ; easy to forget**: the BIOS's 0000:7C00 is
    sti                         ; linear 31,744, and the blob spans 1,536 to
                                ; 46,247 - so the load walks over the return
                                ; addresses as surely as it walked over the
                                ; code, and the machine stops in the middle of
                                ; a `call` with nothing on the screen to say so
    mov [drive], dl             ; the ROM's, and not an assumption - SPEC.md
                                ; 2.9.11 is what happens when it is assumed
    mov si, s_load
    call puts

    mov ax, KD_SEG
    mov es, ax
    xor bx, bx
    mov word [lba], 1           ; sector 0 is this one
    mov cx, [blobsec]
    jcxz .go
.next:
    push cx
    call read1
    jc .err
    pop cx
    add bx, 512
    jnc .same
    mov ax, es                  ; a blob crossing a 64KB boundary steps ES
    add ax, 0x1000              ; rather than letting BX wrap onto itself
    mov es, ax
    xor bx, bx
.same:
    inc word [lba]
    loop .next
.go:
    mov si, s_go
    call puts
    jmp KD_SEG:0x0000
.err:
    pop cx
    mov si, s_err
    call puts
.stop:
    hlt
    jmp .stop

; --- one sector at [lba] into ES:BX ------------------------------------------
; sector = lba % SPT + 1, head = (lba / SPT) % HEADS, cyl = (lba / SPT) / HEADS
read1:
    push ax
    push cx
    push dx
    mov ax, [lba]
    xor dx, dx
    mov cx, BOOT_SPT
    div cx                      ; AX = lba / SPT, DX = lba % SPT
    inc dl
    mov [sect], dl              ; BANKED, because the second div wants CX
    xor dx, dx
    mov cx, BOOT_HEADS
    div cx                      ; AX = cylinder, DX = head
    mov ch, al                  ; CH = cylinder low 8 (40 on a 360KB disk, so
    mov cl, [sect]              ; CL's top two bits stay clear)
    mov dh, dl                  ; DH = head
    mov dl, [drive]
    mov ax, 0x0201              ; AH = 02h read, AL = one sector
    int 0x13                    ; ...into the caller's ES:BX, untouched above
    pop dx                      ; `pop` writes no flag, so int 13h's CF is
    pop cx                      ; still the answer when this returns
    pop ax
    ret

puts:
    push ax
    push bx
    push si
.c:
    mov al, [si]
    or al, al
    jz .out
    inc si
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .c
.out:
    pop si
    pop bx
    pop ax
    ret

s_load:  db 'kdboot: loading', 13, 10, 0
s_go:    db 'kdboot: go', 13, 10, 0
s_err:   db 'kdboot: READ FAILED', 13, 10, 0
drive:   db 0
lba:     dw 0
blobsec: dw 0                   ; patched by the build: how many sectors
sect:    db 0
