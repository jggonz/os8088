; =============================================================================
; os8088 - HDD.DRV / partw.inc
;
; The partition table's WRITE half: the allocator that decides where a slot's
; extent falls (SPEC.md 52.2.1), the CHS fields an entry carries, and the
; commit itself. part.inc keeps the read half - the on-disk shape, the entry
; accessors and the load - because MOUNTING walks the table and mounting is
; the resident driver's job.
;
; The line between the two files is the line between the two IMAGES: nothing
; in here runs unless a human has opened the disk tool or the installer, so
; all of it belongs to HDDTOOL.DRV and none of it need be resident.
; =============================================================================

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
    call hd_cyl_trim            ; EVERY extent this tool commits ends on a
                                ; cylinder boundary (SPEC.md 52.2.5), and this
                                ; is the one place all three cases pass through
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
; hd_cyl_trim - shorten an extent so it ENDS on a cylinder boundary (internal)
; in:  [hd_fbase] (32-bit), [hd_fsecs], DI = the device row
; out: [hd_fsecs] reduced to the largest length whose end is a multiple of
;      heads*spt; 0 when the whole extent sits inside one cylinder
; clobbers: flags
;
; **THE ERA CONVENTION IS A CONTRACT WITH THE BIOS, NOT A TIDINESS RULE**, and
; that is what this routine is for. Every DOS-era partition ends on a cylinder
; boundary, and the reason it matters is the other direction: a BIOS that does
; not know a drive's geometry GUESSES it from the partition table, by taking
; the end-CHS of a partition and reading it as (heads-1, spt). That guess is
; only right if the partition really does end on a boundary.
;
; This tool's own start is the other half of the same convention - LBA = spt,
; head 1 sector 1 cylinder 0 - so the START is deliberately not aligned and
; only the END is.
;
; What went wrong without it: an extent is clipped to HP_MAXSEC (SPEC.md 18.7)
; or to the end of the drive, and neither clip lands on a boundary. A 40MB disk
; got slot 1 = LBA 63 for 65,535 sectors, ending at cylinder 65 head 1 sector
; 15 - so on the NEXT boot the BIOS read that as a 2-head, 15-sector drive.
; The whole disk then measured 1,023 x 2 x 15 = 30,690 sectors, which is
; SMALLER than the partition already on it: the page said `14M` about a 40MB
; drive, the volume was no longer addressable at the CHS it was written at, and
; the only free space the allocator could still see was the 48 sectors between
; the new floor (spt = 15) and the old partition's start at 63. It offered a
; 24KB partition on a disk with 25MB free and was arithmetically right to.
;
; Rounding the LENGTH down fixes all of it at the source: 65,535 becomes 65,457,
; the end lands on cylinder 65 exactly, the last sector is (64, 15, 63), and the
; guess comes back 16 heads / 63 sectors - the geometry it was partitioned with.
; The cost is 78 sectors, 39KB of a 32MB partition.
;
; It also refuses the sliver for the right reason rather than by a size rule:
; an extent that cannot reach a boundary above its own base is inside one
; cylinder, and this answers 0, which .have then reads as "not worth a drive
; letter".
; -----------------------------------------------------------------------------
hd_cyl_trim:
    push ax
    push bx
    push cx
    push dx
    mov ax, [di+HDD_HEADS]
    mul word [di+HDD_SPT]
    mov cx, ax                  ; CX = sectors per cylinder (<= 16,065)
    or cx, cx
    jz .out                     ; a geometry with no cylinder: leave it alone
    mov ax, [hd_fbase]          ; DX:AX = the extent's end, exclusive
    mov dx, [hd_fbase+2]
    add ax, [hd_fsecs]
    adc dx, 0
    cmp dx, cx
    jae .out                    ; the quotient would not fit a word - hd_cyl_up's
                                ; own guard, and only a corrupt table gets here
    div cx                      ; DX = end mod sectors-per-cylinder
    or dx, dx
    jz .out                     ; already on a boundary: the common case
    cmp dx, [hd_fsecs]
    jae .none                   ; the whole extent is inside one cylinder
    mov bx, [hd_fsecs]
    sub bx, dx
    mov [hd_fsecs], bx
    jmp short .out
.none:
    mov word [hd_fsecs], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
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
    call hd_mbr_stamp
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

; -----------------------------------------------------------------------------
; hd_mbr_stamp - our chain-loader in front of the table, when those bytes are
;                ours to write (module internal)
; in:  nothing
; out: nothing (all registers preserved)
; clobbers: flags
;
; The 446 bytes in front of a partition table are the MBR's boot code, and
; whose they are depends entirely on where the table came from - which is what
; [hd_mbrok] records (part.inc):
;
;   HMB_OK    it came off the disk with a signature, so those bytes are THE
;             DISK'S OWN BOOT LOADER. Leave them alone. Writing over them is
;             how a partition edit silently unbootables somebody else's DOS,
;             and this driver is expected to share a disk.
;   HMB_NONE  it read fine and nobody has partitioned it: zeros, ours to fill.
;   HMB_BAD   the read failed, so hd_mbr is a fabrication - also zeros. The
;             tool refuses to COMMIT a fabrication, so in practice this arm is
;             not reached from a write; it is here because the test is about
;             what is in the buffer, and on that question BAD and NONE agree.
;
; This is the other half of hd_mbr_blank not stamping any more. It reproduces
; the old behaviour exactly - the stub used to be laid down by the blank and
; then overwritten by a successful read, so it survived to the write on
; precisely the two states tested here - and it puts the chain-loader in the
; image that only exists while somebody is partitioning.
;
; hd_inst_mbr (inst.inc) stamps unconditionally and deliberately: making a
; partition bootable IS overwriting whatever loader was there (SPEC.md
; 52.10.4). It runs before this and the two agree byte for byte, so the extra
; copy this may then make is the same 446 bytes twice.
; -----------------------------------------------------------------------------
hd_mbr_stamp:
    cmp byte [hd_mbrok], HMB_OK
    je .out
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, hd_bootstub
    mov di, hd_mbr
    mov cx, hd_bootstub_end - hd_bootstub
    rep movsb
    pop es
    pop di
    pop si
    pop cx
.out:
    ret

; The MBR's boot code (SPEC.md 52.10.1) - a real chain-loader now: it finds
; the active entry, reads that partition's first sector to 0000:7C00 and jumps
; to it. boot/mbr.asm is the source and it is `incbin`ed rather than written
; out as a `db` list, which is what the previous occupant of these bytes was.
;
; That occupant printed `Not bootable` and halted, and it was the truth for as
; long as SPEC.md 52.9 said os8088 does not boot from a hard disk. Its
; REASONING outlives it and is why the loader still carries three messages:
; 446 blank bytes is a machine that hangs with no explanation.
;
; Exactly 446 bytes, asserted at both ends - boot/mbr.asm pads itself to the
; partition table's offset and refuses to overrun it, and the equ below is
; checked against what actually arrived. A short blob here would leave the
; tail of somebody else's boot code in front of our table; a long one would
; eat the table itself.
; -----------------------------------------------------------------------------
hd_bootstub:
    incbin "mbr.bin"
hd_bootstub_end:

HD_MBRCODE   equ 446            ; the space in front of the table
%if hd_bootstub_end - hd_bootstub != HD_MBRCODE
%error "build/mbr.bin is not 446 bytes - see boot/mbr.asm"
%endif

; ...and the VOLUME boot record the installer writes (SPEC.md 52.10.2), the
; same way and for the same reason: it is real assembly in boot/boothd.asm,
; incbin'ed rather than transcribed. BOOTHD_KSECS is the ONE word the
; installer patches - the kernel's length in sectors - and it is pinned at a
; documented offset in that file precisely so this side can find it without
; knowing anything else about the sector's layout.
BOOTHD_KSECS equ 508
hd_bootvbr:
    incbin "boothd.bin"
hd_bootvbr_end:

%if hd_bootvbr_end - hd_bootvbr != 512
%error "build/boothd.bin is not 512 bytes - see boot/boothd.asm"
%endif

; --- state this module owns (moved out of hdd.asm's data block) ---
; -----------------------------------------------------------------------------
hd_xslot:    db 0               ; hd_slot_extent's scan: the slot being placed,
hd_xstart:   dd 0               ; the candidate start,
hd_xlimit:   dd 0               ; how far it may run,
hd_xend:     dd 0               ; one entry's end,
hd_xbest:    dd 0               ; and the largest hole the walk has seen
hd_xblen:    dd 0               ; (52.2.1)
hd_fbase:    dd 0               ; the extent a format will write: its first LBA
hd_fsecs:    dw 0               ; ...and its length
