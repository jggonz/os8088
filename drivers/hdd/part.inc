; =============================================================================
; os8088 - HDD.DRV / part.inc
;
; The partition table (SPEC.md 52.2) - the on-disk shape, the arithmetic, and
; the allocator that decides what a slot's extent is. The WINDOW is tool.inc's,
; because partitioning and formatting are one operation to the user and one
; window in the driver. The KERNEL never sees an MBR: a driver-backed volume's
; LBAs are volume-relative and the base lives in hd_vols, so everything about
; partitioning is here.
;
; The on-disk shape, at LBA 0:
;
;   0..445    boot code - we write a stub that halts with a message, because
;             os8088 does not boot from a hard disk (SPEC.md 52.9) and a
;             blank 446 bytes is a machine that hangs with no explanation
;   446+16n   four entries:
;               +0  status      80h active / 00h
;               +1  start head
;               +2  start sector in bits 0..5, cylinder bits 8..9 in 6..7
;               +3  start cylinder bits 0..7
;               +4  type        01h FAT12, 04h FAT16 <32MB, 06h FAT16
;               +5  end head / sector / cylinder, the same encoding
;               +8  start LBA, dword
;               +12 sector count, dword
;   510..511  55 AA
;
; Two era conventions are obeyed because DOS reads what we write: the first
; partition starts at LBA = spt (head 1, sector 1, cylinder 0), and every
; partition ends on a cylinder boundary. A partition is capped at 65,535
; sectors by the kernel's own BPB rule 8 - which is also the DOS 3.3 limit -
; so four primaries is the whole offering and an extended chain is not
; implemented.
; =============================================================================

HP_TBL       equ 446            ; where the four entries start
HP_ESZ       equ 16
HP_STATUS    equ 0
HP_SHEAD     equ 1
HP_SSEC      equ 2
HP_SCYL      equ 3
HP_TYPE      equ 4
HP_EHEAD     equ 5
HP_ESEC      equ 6
HP_ECYL      equ 7
HP_LBA       equ 8
HP_SECS      equ 12

HPT_NONE     equ 0x00
HPT_FAT12    equ 0x01
HPT_FAT16S   equ 0x04           ; FAT16 under 32MB - what DOS 3.3 wrote, and
                                ; every partition this driver makes is one
HPT_FAT16B   equ 0x06           ; FAT16 at or over 32MB, and
HPT_FAT16L   equ 0x0E           ; ...the LBA-addressed flavour of it. Both are
                                ; MOUNTABLE even though this driver never
                                ; writes one: another machine's FDISK did, and
                                ; the volume inside is the same FAT16
HP_MAXSEC    equ 65535          ; the kernel's volume ceiling (SPEC.md 18.7)

; -----------------------------------------------------------------------------
; hd_part_load - read the selected device's partition table into hd_mbr
; in:  DI = the device row
; out: CF = 0 and hd_mbr holds a table with a valid signature;
;      CF = 1 = unreadable, and hd_mbr holds a BLANK table instead
; clobbers: flags
;
; A disk with no signature is not an error here - it is a disk nobody has
; partitioned - so the failure path leaves a table the user can edit rather
; than nothing at all. What it must never do is leave the PREVIOUS device's
; table in the buffer, which is why the blank happens before the read.
; -----------------------------------------------------------------------------
hd_part_load:
    push ax
    push bx
    push cx
    push dx
    push es
    call hd_mbr_blank
    push ds
    pop es
    mov bx, hd_mbr
    xor ax, ax
    xor dx, dx
    mov cx, 1
    mov byte [hd_rawop], 0      ; read
    call hd_raw
    jc .bad
    cmp word [hd_mbr+510], 0xAA55
    jne .bad
    mov byte [hd_mbrok], 1
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    call hd_mbr_blank
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_mbr_blank - a table with nothing in it (module internal)
; in:  nothing
; out: hd_mbr = the boot stub, four empty entries and the signature
; clobbers: flags
; -----------------------------------------------------------------------------
hd_mbr_blank:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, hd_mbr
    mov cx, 256
    xor ax, ax
    rep stosw
    mov si, hd_bootstub
    mov di, hd_mbr
    mov cx, hd_bootstub_end - hd_bootstub
    rep movsb
    mov word [hd_mbr+510], 0xAA55
    mov byte [hd_mbrok], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_part_ent - one partition entry's address
; in:  AL = slot (0..3)
; out: SI = the entry
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
hd_part_ent:
    push ax
    push dx
    mov ah, HP_ESZ
    mul ah
    add ax, hd_mbr + HP_TBL
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_part_used - is a slot in use?
; in:  SI = the entry
; out: CF = 0 in use (a known type and a non-zero length)
; clobbers: flags
; -----------------------------------------------------------------------------
hd_part_used:
    cmp byte [si+HP_TYPE], HPT_NONE
    je .no
    cmp word [si+HP_SECS], 0
    jne .yes
    cmp word [si+HP_SECS+2], 0
    jne .yes
.no:
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; hd_part_isfat - is this slot a FAT volume we could mount?
; in:  SI = the entry
; out: CF = 0 yes; CF = 1 = free, or a type that is not FAT
; clobbers: flags
;
; The TYPE byte, and the type byte only: whether the volume inside is really
; a FAT one is the mount's question, and `osapi_vol_mount` answering CF=1 is
; how a partition that lies gets dropped again (SPEC.md 52.4). Extended
; (05h/0Fh) and FAT32 (0Bh/0Ch) are deliberately not here - the first is a
; chain this driver does not walk and the second is a format this kernel
; does not read.
; -----------------------------------------------------------------------------
hd_part_isfat:
    push ax
    call hd_part_used
    jc .no
    mov al, [si+HP_TYPE]
    cmp al, HPT_FAT12
    je .yes
    cmp al, HPT_FAT16S
    je .yes
    cmp al, HPT_FAT16B
    je .yes
    cmp al, HPT_FAT16L
    je .yes
.no:
    pop ax
    stc
    ret
.yes:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_part_mb - one entry's size in whole MB
; in:  SI = the entry
; out: AX = megabytes
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hd_part_mb:
    push bx
    push cx
    push dx
    mov ax, [si+HP_SECS]
    mov dx, [si+HP_SECS+2]
    mov bx, ax
    mov cl, 11                  ; 2,048 sectors to the megabyte
.shift:
    shr dx, 1
    rcr bx, 1
    dec cl
    jnz .shift
    mov ax, bx
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_ent_end - one entry's end LBA, exclusive (module internal)
; in:  SI = the entry
; out: [hd_xend] = LBA + sector count, 32-bit
; clobbers: flags
; -----------------------------------------------------------------------------
hd_ent_end:
    push ax
    push dx
    mov ax, [si+HP_LBA]
    mov dx, [si+HP_LBA+2]
    add ax, [si+HP_SECS]
    adc dx, [si+HP_SECS+2]
    mov [hd_xend], ax
    mov [hd_xend+2], dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_cyl_up - round a 32-bit LBA up to a cylinder boundary (module internal)
; in:  DX:AX = an LBA, DI = the device row
; out: DX:AX = the next multiple of heads*spt at or above it
; clobbers: AX, DX (the outputs), flags
; -----------------------------------------------------------------------------
hd_cyl_up:
    push bx
    push cx
    push si
    mov bx, ax                  ; BX:SI = the LBA, banked: both multiplies
    mov si, dx                  ; below write DX
    mov ax, [di+HDD_HEADS]
    mul word [di+HDD_SPT]
    mov cx, ax                  ; CX = sectors per cylinder (<= 16,065)
    or cx, cx
    jz .keep
    mov ax, bx
    mov dx, si
    cmp dx, cx
    jae .keep                   ; the quotient would not fit a word: only a
    div cx                      ; corrupt table reaches this
    or dx, dx
    jz .exact
    inc ax
.exact:
    mul cx                      ; DX:AX = the boundary at or above the LBA
    jmp short .out
.keep:
    mov ax, bx
    mov dx, si
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_slot_extent - what a Format of this slot would write (SPEC.md 52.2.1)
; in:  AL = slot (0..3), DI = the device row
; out: CF = 0 and [hd_fbase] (32-bit) / [hd_fsecs] = the extent, which is what
;      both the entry and the formatter take; CF = 1 = there is no room
; clobbers: flags
;
; **A slot is not a 32MB region of the disk, and never was.** The extent comes
; out of the TABLE, so a foreign partition is worked around rather than
; written through - which is the whole safety property here, because an entry
; laid at a fixed 32MB multiple lands inside an 80MB partition that started
; below it and takes the whole thing with it.
;
; Three cases, and the first two are the same one seen twice:
;
;   used, at or under the ceiling   its OWN extent, reused in place. The user
;                                   has already confirmed erasing it.
;   used, over the ceiling          the first HP_MAXSEC of its own extent; the
;                                   rest goes back to being free space. This is
;                                   how a 50MB or 80MB partition is reclaimed
;                                   without touching a neighbour.
;   free                            the scan below.
;
; The scan is `mem_claim`'s (SPEC.md 50): start at the era's floor - LBA = spt,
; head 1 sector 1 cylinder 0 - and while any OTHER used entry contains the
; candidate, restart at that entry's end rounded up to a cylinder. Each bump
; moves strictly past one entry, so four passes is a proof and not a guess.
; The length then runs to the next used entry ABOVE the candidate (or the end
; of the drive) - so a 32MB partition sits directly on top of a foreign 50MB
; one rather than at the next multiple of anything.
;
; **It walks EVERY hole and takes the largest**, rather than the first that
; fits, and that is about the slots being scarce: there are four, and a disk
; another machine has touched has an ALIGNMENT GAP at the front of it (this
; tool's floor is LBA = spt; a modern one 1MB-aligns at LBA 2048, and the 1,985
; sectors between are real, unowned, and a working 970KB volume). First-fit
; spends a whole primary slot on that while 30MB sits free in the middle of the
; disk. The walk is the same bump-and-limit pair in a loop: the candidate moves
; to the limit that bounded it, which the next bump carries past the entry
; sitting there, so it strictly increases and there are at most four holes to
; see. A tie keeps the EARLIER hole, which leaves the later space contiguous.
; -----------------------------------------------------------------------------
hd_slot_extent:
    push ax
    push cx
    push dx
    push si
    mov [hd_xslot], al
    call hd_part_ent
    call hd_part_used
    jc .fresh

    mov ax, [si+HP_LBA]         ; in place: the extent it already claims
    mov [hd_fbase], ax
    mov ax, [si+HP_LBA+2]
    mov [hd_fbase+2], ax
    mov ax, [si+HP_SECS]
    cmp word [si+HP_SECS+2], 0
    je .own                     ; over the ceiling (SPEC.md 18.7): take the
    mov ax, HP_MAXSEC           ; first 32MB and free the rest
.own:
    mov [hd_fsecs], ax
    call hd_dev_secs            ; ...and never past the end of the drive: a
    sub ax, [hd_fbase]          ; table another machine wrote is input, and
    sbb dx, [hd_fbase+2]        ; input is hostile
    jc .no
    or dx, dx
    jnz .have                   ; more room than a word can hold: no clip
    cmp ax, [hd_fsecs]
    jae .have
    mov [hd_fsecs], ax
    jmp .have                   ; NEAR: the hole walk below is well past 127
                                ; bytes, and `jmp short` is how NASM says so

.fresh:
    xor ax, ax                  ; no hole found yet
    mov [hd_xblen], ax
    mov [hd_xblen+2], ax
    mov [hd_xbest], ax
    mov [hd_xbest+2], ax
    mov ax, [di+HDD_SPT]        ; the floor: one track in, which is where
    xor dx, dx                  ; every DOS-era table put the first partition
    mov [hd_xstart], ax
    mov [hd_xstart+2], dx
    mov ch, 5                   ; four entries bound four holes, and a stop
.hole:
    mov cl, 4                   ; each bump consumes one entry
.bump:
    call hd_x_bump
    jnc .clear
    dec cl
    jnz .bump
.clear:
    call hd_x_limit             ; [hd_xlimit] = the next used start above us,
    mov ax, [hd_xlimit]         ; or the end of the drive
    mov dx, [hd_xlimit+2]
    sub ax, [hd_xstart]
    sbb dx, [hd_xstart+2]
    jc .step                    ; the candidate is past the limit: nothing here
    cmp dx, [hd_xblen+2]        ; bigger than the best so far? A tie keeps the
    ja .take                    ; EARLIER hole, which is the one that leaves
    jb .step                    ; the later space contiguous
    cmp ax, [hd_xblen]
    jbe .step
.take:
    mov [hd_xblen], ax
    mov [hd_xblen+2], dx
    mov ax, [hd_xstart]
    mov [hd_xbest], ax
    mov ax, [hd_xstart+2]
    mov [hd_xbest+2], ax
.step:
    mov ax, [hd_xlimit]         ; on to the entry that bounded this hole; the
    mov [hd_xstart], ax         ; bump carries us past it next time round, so
    mov ax, [hd_xlimit+2]       ; the candidate strictly increases and the walk
    mov [hd_xstart+2], ax       ; terminates
    call hd_dev_secs
    cmp dx, [hd_xstart+2]
    ja .more
    jb .best
    cmp ax, [hd_xstart]
    jbe .best
.more:
    dec ch
    jnz .hole
.best:
    mov ax, [hd_xblen]
    mov dx, [hd_xblen+2]
    or dx, dx
    jnz .cap
    cmp ax, HP_MAXSEC
    jbe .len
.cap:
    mov ax, HP_MAXSEC
.len:
    mov [hd_fsecs], ax          ; zero when no hole was found at all, which
    mov ax, [hd_xbest]          ; .have refuses below one track
    mov [hd_fbase], ax
    mov ax, [hd_xbest+2]
    mov [hd_fbase+2], ax
.have:
    mov ax, [hd_fsecs]
    cmp ax, [di+HDD_SPT]
    jb .no                      ; less than a track: not worth a drive letter
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_x_bump - move the candidate out of any other entry (module internal)
; in:  [hd_xstart], [hd_xslot], DI = the device row
; out: CF = 1 = it moved and the scan must run again; CF = 0 = it is clear
; clobbers: flags
; -----------------------------------------------------------------------------
hd_x_bump:
    push ax
    push cx
    push dx
    push si
    xor cl, cl
.slot:
    cmp cl, [hd_xslot]
    je .next                    ; our own entry is the one being replaced
    mov al, cl
    call hd_part_ent
    call hd_part_used
    jc .next
    mov ax, [hd_xstart+2]       ; candidate >= this entry's start?
    cmp ax, [si+HP_LBA+2]
    jb .next
    ja .above
    mov ax, [hd_xstart]
    cmp ax, [si+HP_LBA]
    jb .next
.above:
    call hd_ent_end
    mov ax, [hd_xstart+2]       ; ...and below its end?
    cmp ax, [hd_xend+2]
    ja .next
    jb .hit
    mov ax, [hd_xstart]
    cmp ax, [hd_xend]
    jae .next
.hit:
    mov ax, [hd_xend]
    mov dx, [hd_xend+2]
    call hd_cyl_up
    mov [hd_xstart], ax
    mov [hd_xstart+2], dx
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret
.next:
    inc cl
    cmp cl, 4
    jb .slot
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_x_limit - how far the candidate may run (module internal)
; in:  [hd_xstart], [hd_xslot], DI = the device row
; out: [hd_xlimit] = the lowest used start above the candidate, or the end of
;      the drive
; clobbers: flags
; -----------------------------------------------------------------------------
hd_x_limit:
    push ax
    push cx
    push dx
    push si
    call hd_dev_secs
    mov [hd_xlimit], ax
    mov [hd_xlimit+2], dx
    xor cl, cl
.slot:
    cmp cl, [hd_xslot]
    je .next
    mov al, cl
    call hd_part_ent
    call hd_part_used
    jc .next
    mov ax, [si+HP_LBA+2]       ; above the candidate? one at or below it has
    cmp ax, [hd_xstart+2]       ; already been stepped past by hd_x_bump
    jb .next
    ja .above
    mov ax, [si+HP_LBA]
    cmp ax, [hd_xstart]
    jbe .next
.above:
    mov ax, [si+HP_LBA+2]       ; ...and nearer than the limit so far?
    cmp ax, [hd_xlimit+2]
    ja .next
    jb .take
    mov ax, [si+HP_LBA]
    cmp ax, [hd_xlimit]
    jae .next
.take:
    mov ax, [si+HP_LBA]
    mov [hd_xlimit], ax
    mov ax, [si+HP_LBA+2]
    mov [hd_xlimit+2], ax
.next:
    inc cl
    cmp cl, 4
    jb .slot
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_dev_secs - a device's total sectors
; in:  DI = the device row
; out: DX:AX = cyl * heads * spt
; clobbers: AX, DX (the outputs), flags
; -----------------------------------------------------------------------------
hd_dev_secs:
    push bx
    mov ax, [di+HDD_HEADS]
    mul word [di+HDD_SPT]       ; DX:AX, and DX is 0: 255*63 is 16,065
    mul word [di+HDD_CYL]       ; ...so this is a true 32-bit product
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_part_chs - fill one entry's start and end CHS fields
; in:  SI = the entry, DI = the device row
; out: nothing
; clobbers: flags
;
; The CHS halves must AGREE with the LBA halves under the geometry the drive
; is addressed with, or DOS reads the partition somewhere else entirely. A
; cylinder past 1023 is clamped to the all-ones form every tool of the era
; wrote, which is what says "read the LBA fields instead".
; -----------------------------------------------------------------------------
hd_part_chs:
    push ax
    push bx
    push cx
    push dx
    mov ax, [si+HP_LBA]
    mov dx, [si+HP_LBA+2]
    call hd_lba_chs             ; -> [hd_cyl]/[hd_head]/[hd_sec]
    mov al, [hd_head]
    mov [si+HP_SHEAD], al
    call hd_chs_pack            ; AX = the packed sector/cylinder pair
    mov [si+HP_SSEC], ax
    mov ax, [si+HP_LBA]         ; the LAST sector, not one past it
    mov dx, [si+HP_LBA+2]
    add ax, [si+HP_SECS]
    adc dx, [si+HP_SECS+2]
    sub ax, 1
    sbb dx, 0
    call hd_lba_chs
    mov al, [hd_head]
    mov [si+HP_EHEAD], al
    call hd_chs_pack
    mov [si+HP_ESEC], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_lba_chs - hd_chs against an LBA in registers (module internal)
; in:  DX:AX = the LBA, DI = the device row
; out: [hd_cyl]/[hd_head]/[hd_sec]; a value past the geometry clamps to the
;      1023/255/63 form rather than failing
; clobbers: flags
; -----------------------------------------------------------------------------
hd_lba_chs:
    push ax
    push dx
    mov [hd_lba], ax
    mov [hd_lba+2], dx
    call hd_chs
    jnc .out
    mov word [hd_cyl], 1023
    mov word [hd_head], 254
    mov word [hd_sec], 63
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_chs_pack - [hd_sec] and [hd_cyl] as the two bytes an entry carries
; in:  [hd_cyl], [hd_sec]
; out: AX = low byte the sector|cylinder-high pair, high byte cylinder low
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hd_chs_pack:
    push cx
    mov cl, [hd_cyl+1]
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    and cl, 0xC0
    or cl, [hd_sec]
    mov al, cl
    mov ah, [hd_cyl]
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_part_write - commit the table in hd_mbr to the device
; in:  DI = the device row
; out: CF = 0 written; CF = 1 = the write failed
; clobbers: flags
;
; ONE sector, and it is the commit: everything before this call edits RAM, so
; a user who changes their mind has changed nothing.
; -----------------------------------------------------------------------------
hd_part_write:
    push ax
    push bx
    push cx
    push dx
    push es
    push ds
    pop es
    mov bx, hd_mbr
    xor ax, ax
    xor dx, dx
    mov cx, 1
    mov byte [hd_rawop], 1      ; write
    call hd_raw
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- strings the disk tool and the page share -------------------------------
hd_s_mb:     db 'M', 0
hd_s_close:  db 'Close', 0

; The MBR's boot code. os8088 does not boot from a hard disk (SPEC.md 52.9),
; and 446 blank bytes is a machine that hangs with no explanation - so this
; says so and stops.
hd_bootstub:
    db 0xFA                     ; cli
    db 0xFC                     ; cld
    db 0x31, 0xC0               ; xor ax, ax
    db 0x8E, 0xD8               ; mov ds, ax
    db 0xBE, 0x1B, 0x7C         ; mov si, msg (0x7C00 + 27)
    db 0xAC                     ; lodsb
    db 0x08, 0xC0               ; or al, al
    db 0x74, 0x06               ; jz halt
    db 0xB4, 0x0E               ; mov ah, 0x0E
    db 0xCD, 0x10               ; int 0x10
    db 0xEB, 0xF5               ; jmp lodsb
    db 0xF4                     ; halt: hlt
    db 0xEB, 0xFD               ; jmp halt
    db 'Not bootable', 13, 10, 0
hd_bootstub_end:
