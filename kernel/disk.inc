; =============================================================================
; jop - disk.inc
;
; BIOS int 13h floppy reads and the jopfs v2 mount (SPEC.md 18-19): the
; superblock (LBA 0), the 32-entry directory (LBA 1-2) and the 32-entry
; icon table (LBA 3-6); file data starts at LBA 7. Only the
; UI task touches the disk. Task switching is paused around all int 13h
; activity by inc/dec of [sch_lock] - the timer ISR still chains the BIOS
; tick (the floppy motor logic needs it), it just stops switching.
; Interrupts stay ON during int 13h. The gfx lock is NOT held here.
;
; Reads go one sector per int 13h call (AH=02, AL=1) so track boundaries
; and DMA alignment never matter; each sector gets up to 3 attempts with
; an AH=00 controller reset between attempts.
; =============================================================================

; --- module data (initialized: lives in .text, not .bss) ---------------------
disk_drive:  db 1                ; current drive (0 = A:, 1 = B:), init B:
disk_spt:    dw 9                ; sectors per track  } fallback geometry,
disk_heads:  dw 2                ; heads              } replaced at mount
disk_nfiles: dw 0                ; file count, valid after a good mount

dsk_magic:   db 'JOPFS2', 0, 0  ; superblock magic, 8 bytes (SPEC.md 19)

; -----------------------------------------------------------------------------
; disk_read - read CX sectors starting at LBA AX to ES:BX
; in:  AX = start LBA, CX = sector count, ES:BX -> destination
;      (the internal pointer advances 512 bytes per sector - the caller's
;       ES:BX budget must cover count*512). Drive from [disk_drive].
; out: CF=1 on unrecoverable error (some sector failed all 3 attempts)
; clobbers: nothing but CF
;
; LBA -> CHS with 16-bit div (LBA < 2880 so it never overflows):
;   cyl = LBA / (spt*heads); rem = LBA % (spt*heads)
;   head = rem / spt; sector = rem % spt + 1
; int 13h regs: CH = cyl low 8 (cyl < 80, no high bits), CL = sector,
; DH = head, DL = drive.
; -----------------------------------------------------------------------------
disk_read:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    inc byte [sch_lock]         ; pause task switching across all int 13h

    mov si, cx                  ; SI = sectors remaining
    mov di, ax                  ; DI = current LBA

.sector:
    test si, si
    jz .success                 ; all sectors read (count 0 reads nothing)

    ; --- LBA (DI) -> CHS in CH/CL/DH, drive in DL ---
    mov ax, [disk_spt]
    mul word [disk_heads]       ; AX = spt*heads (fits: <= 36)
    mov cx, ax
    mov ax, di
    xor dx, dx
    div cx                      ; AX = cylinder, DX = rem
    cmp ax, 80                  ; a corrupt LBA (bad directory entry) must
    jae .fail                   ; fail, not truncate into CH and read the
                                ; wrong sector; both real geometries top
                                ; out below 80 cylinders
    mov ch, al                  ; CH = cyl low 8 bits (cyl < 80)
    mov ax, dx
    xor dx, dx
    div word [disk_spt]         ; AX = head, DX = sector-1
    mov cl, dl
    inc cl                      ; CL = sector, 1-based
    mov dh, al                  ; DH = head
    mov dl, [disk_drive]        ; DL = drive

    mov bp, 3                   ; attempts left for this sector
.attempt:
    mov ax, 0x0201              ; AH=02 read, AL=1 sector -> ES:BX
    int 0x13
    jnc .ok
    mov ah, 0x00                ; failed: reset the controller and retry
    mov dl, [disk_drive]
    int 0x13
    dec bp
    jnz .attempt
    jmp .fail                   ; 3 strikes: unrecoverable

.ok:
    add bx, 512                 ; advance destination
    inc di                      ; next LBA
    dec si
    jmp .sector

.success:
    clc
    jmp .out
.fail:
    stc
.out:
    dec byte [sch_lock]         ; resume switching (dec leaves CF alone)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; disk_mount - mount a jopfs v2 floppy: superblock + directory + icon table
; in:  DL = drive (0 = A:, 1 = B:)
; out: CF=1 if the disk is unreadable or not jopfs (then [disk_nfiles] = 0);
;      CF=0 on success with [disk_spt]/[disk_heads]/[disk_nfiles] loaded,
;      the 32 directory entries (LBA 1-2) in disk_dir and the 32 icon-table
;      entries (LBA 3-6) in disk_icons
; clobbers: nothing but CF
;
; LBA 0 is read with the fallback geometry spt=9/heads=2 - that maps to
; CHS 0/0/1 under any real floppy geometry, so the superblock is readable
; before the true geometry is known.
; -----------------------------------------------------------------------------
disk_mount:
    push ax
    push bx
    push cx
    push si
    push di
    push dx
    push es

    mov [disk_drive], dl
    mov word [disk_spt], 9      ; fallback geometry for the LBA 0 read
    mov word [disk_heads], 2
    mov word [disk_nfiles], 0   ; stays 0 unless the mount fully succeeds

    mov ax, ds                  ; buffers live in the kernel segment
    mov es, ax

    ; --- LBA 0: superblock ---
    xor ax, ax                  ; LBA 0
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .fail

    ; magic must be "JOPFS2" + two zero bytes
    cld
    mov si, dsk_secbuf
    mov di, dsk_magic
    mov cx, 8
    repe cmpsb
    jne .fail

    ; geometry: only the two SPEC 19 layouts exist (18 or 9 spt, 2 heads).
    ; The superblock is untrusted input - anything else is rejected, not
    ; stored: a hostile pair whose product wraps 16 bits (e.g. 0x8000*2)
    ; would make disk_read's CHS divisor zero and divide-fault with
    ; sch_lock held.
    mov ax, [dsk_secbuf+8]      ; sectors per track
    cmp ax, 18
    je .spt_ok
    cmp ax, 9
    jne .fail
.spt_ok:
    mov [disk_spt], ax
    mov ax, [dsk_secbuf+10]     ; heads
    cmp ax, 2
    jne .fail
    mov [disk_heads], ax

    ; file count, clamped to the directory's 32 slots
    mov ax, [dsk_secbuf+12]
    cmp ax, 32
    jbe .count_ok
    mov ax, 32
.count_ok:
    mov [disk_nfiles], ax

    ; --- LBA 1-2: the directory, 32 entries x 32 bytes ---
    mov ax, 1
    mov cx, 2
    mov bx, disk_dir
    call disk_read
    jc .dirfail

    ; --- LBA 3-6: the icon table, 32 entries x 64 bytes ---
    mov ax, 3
    mov cx, 4
    mov bx, disk_icons
    call disk_read
    jc .dirfail                 ; a torn mount is a failed mount

    clc
    jmp .out

.dirfail:
    mov word [disk_nfiles], 0   ; count was already stored: retract it
.fail:
    mov word [disk_spt], 9      ; leave sane fallback geometry behind
    mov word [disk_heads], 2
    stc
.out:
    pop es
    pop dx
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

disk_dir:   resb 1024           ; 32 directory entries x 32 bytes (LBA 1-2)
disk_icons: resb 2048           ; 32 icon-table entries x 64 bytes (LBA 3-6);
                                ; entry i belongs to directory entry i,
                                ; all-zero = no icon (SPEC.md 19)
dsk_secbuf: resb 512            ; superblock scratch sector for disk_mount

section .text
