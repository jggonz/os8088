; =============================================================================
; os8088 - disk.inc
;
; BIOS int 13h floppy reads and the FAT12/FAT16 mount (SPEC.md 18-19). The
; data floppy is a standard FAT volume written by foreign machines, so every
; byte read off it is hostile: the BPB runs the 17-rule validation table
; (SPEC.md 18.2) before any derived number is trusted, the FAT is
; snapshotted into FAT_SEG at mount, and the root directory is re-shaped
; into 32-byte SYNTHESIZED entries (SPEC.md 19) so no consumer ever sees an
; on-disk layout. Only the UI task touches the disk. Task switching is
; paused around all int 13h activity by inc/dec of [sch_lock] - the timer
; ISR still chains the BIOS tick (the floppy motor logic needs it), it just
; stops switching. Interrupts stay ON during int 13h. The gfx lock is NOT
; held here.
;
; Reads go one sector per int 13h call (AH=02, AL=1) so track boundaries
; and DMA alignment never matter; each sector gets up to 3 attempts with
; an AH=00 controller reset between attempts.
;
; The three big buffers - the directory, the icon table and the sector
; scratch - live in .lowbss (LOW_SEG, SPEC.md 2.1), not in the kernel's own
; 64KB window: 3.5KB the kernel no longer spends on them. int 13h writes
; through ES:BX and needs no help. Everything that READS them goes through
; dsk_get_dir / dsk_get_icon below, which stage one entry at a time into a
; kernel-segment buffer, so consumers keep the plain DS:SI pointer they
; always had and no drawing or parsing code learns about segments. The FAT
; snapshot lives in FAT_SEG (SPEC.md 2.1), reached via ES only, never DS;
; dsk_next_clus is its single reader.
; =============================================================================

DSK_DE_SIZE  equ 32              ; staged directory entry stride (SPEC.md 19)
DSK_ICO_SIZE equ 64              ; harvested icon slot stride (SPEC.md 19)

; --- BPB field offsets, valid inside dsk_bpb after staging (SPEC.md 19) ------
DSK_B_JMP    equ 0               ; BS_jmpBoot[0] (byte)
DSK_B_BPS    equ 11              ; BPB_BytsPerSec (word)
DSK_B_SPC    equ 13              ; BPB_SecPerClus (byte)
DSK_B_RSVD   equ 14              ; BPB_RsvdSecCnt (word)
DSK_B_NFATS  equ 16              ; BPB_NumFATs (byte)
DSK_B_ROOTN  equ 17              ; BPB_RootEntCnt (word)
DSK_B_TOT16  equ 19              ; BPB_TotSec16 (word)
DSK_B_MEDIA  equ 21              ; BPB_Media (byte)
DSK_B_FATSZ  equ 22              ; BPB_FATSz16 (word)
DSK_B_SPT    equ 24              ; BPB_SecPerTrk (word)
DSK_B_HEADS  equ 26              ; BPB_NumHeads (word)

; --- raw on-disk root directory entry field offsets (SPEC.md 19) -------------
DSK_R_NAME   equ 0               ; name[8] (space-padded)
DSK_R_EXT    equ 8               ; ext[3] (space-padded)
DSK_R_ATTR   equ 11              ; attribute byte
DSK_R_CLUS   equ 26              ; FstClusLO (FstClusHI @20 is FAT32-only)
DSK_R_SIZE   equ 28              ; file size, dword

; --- module data (initialized: lives in .text, not .bss) ---------------------
disk_drive:  db 1                ; current drive (0 = A:, 1 = B:), init B:
disk_spt:    dw 9                ; sectors per track  } fallback geometry,
disk_heads:  dw 2                ; heads              } replaced at mount
disk_nfiles: dw 0                ; file count, valid after a good mount

; The current directory (SPEC.md 19.2) - the one disk_dir lists and the one
; every dskw_* name resolves inside. 0 = the root. These live HERE, in
; .text, and not in .bss for a reason worth stating: -f bin gives .bss no
; image bytes, so it boots as whatever the RAM held, and the very first
; disk_mount reads [dsk_keepcwd] to decide whether to reset [dsk_cwd].
; Left in .bss, one garbage byte at boot sends the mount walking a garbage
; cluster chain and the volume lists two lines of noise.
dsk_cwd:     dw 0                ; current directory's first cluster
dsk_keepcwd: db 0                ; 1 = this mount is a dsk_chdir: keep it

; The folder icon (SPEC.md 19.2): 16 mask rows then 16 data rows, bit 15 =
; leftmost, the SPEC.md 20.2 body format the harvest copies into a slot. A
; folder has nothing on disk to harvest an icon FROM, so this is the one
; icon body that is built in - hand-authored like the menu-bar logo.
;
;   ..#####.........      tab
;   .#.....#........
;   .##############.      body
;   .#............#.
;   .##############.
dsk_folder_ico:
    dw 0x0000                    ; 16 mask rows (white underlay: the body,
    dw 0x3E00                    ; filled, so the icon reads on the dither)
    dw 0x7F00
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x0000
    dw 0x0000
    dw 0x0000                    ; 16 data rows (black pixels)
    dw 0x3E00
    dw 0x4100
    dw 0x7FFE
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x7FFE
    dw 0x0000
    dw 0x0000

; -----------------------------------------------------------------------------
; disk_read  - read  CX sectors starting at LBA AX to   ES:BX
; disk_write - write CX sectors starting at LBA AX from ES:BX (SPEC.md 18.4)
; in:  AX = start LBA, CX = sector count, ES:BX -> destination / source
;      (the internal pointer advances 512 bytes per sector - the caller's
;       ES:BX budget must cover count*512). Drive from [disk_drive].
; out: CF=1 on unrecoverable error (some sector failed all 3 attempts, or
;      the media is write-protected); [dsk_ioerr] = the last int 13h status
;      byte, which is how diskw.inc tells 03h (write-protected) from a real
;      failure
; clobbers: nothing but CF (and the two module bytes above)
;
; ONE transfer routine, entered with [dsk_op] = the int 13h function: the
; CHS conversion, the cylinder guard and the retry policy exist once and
; cannot drift apart between reading and writing.
;
; LBA -> CHS with 16-bit div (LBA < 2880 so it never overflows):
;   cyl = LBA / (spt*heads); rem = LBA % (spt*heads)
;   head = rem / spt; sector = rem % spt + 1
; int 13h regs: CH = cyl low 8 (cyl < 80, no high bits), CL = sector,
; DH = head, DL = drive.
; -----------------------------------------------------------------------------
disk_read:
    mov byte [dsk_op], 0x02     ; AH=02 read (mov leaves the flags alone)
    jmp short dsk_xfer
disk_write:
    mov byte [dsk_op], 0x03     ; AH=03 write - the ONLY difference
dsk_xfer:
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
    mov ah, [dsk_op]            ; 02 read / 03 write
    mov al, 1                   ; one sector per call, to ES:BX
    int 0x13
    jnc .ok
    mov [dsk_ioerr], ah         ; keep the status for diskw.inc's mapping
    cmp ah, 0x03
    je .fail                    ; write-protected: retrying cannot help
    mov ah, 0x00                ; failed: reset the controller and retry
    mov dl, [disk_drive]
    int 0x13
    dec bp
    jnz .attempt
    jmp short .fail             ; 3 strikes: unrecoverable

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
; disk_mount - mount a FAT12/FAT16 data floppy (SPEC.md 18.3)
; in:  DL = drive (0 = A:, 1 = B:)
; out: CF=1 if the disk is unreadable, unformatted or fails any SPEC.md 18.2
;      rule (then [disk_nfiles] = 0 and the fallback geometry 9/2 is
;      restored); CF=0 on success with [disk_spt]/[disk_heads]/[disk_nfiles]
;      and the SPEC.md 18.1 variables loaded, the FAT snapshot in FAT_SEG,
;      the synthesized directory in disk_dir and the harvested icons in
;      disk_icons
; clobbers: nothing but CF
;
; A torn mount is a failed mount, and NO cross-mount state survives: every
; open/refresh fully remounts. [ld_pending] is cleared on entry - a queued
; click indexes the directory being replaced (SPEC.md 18.3 step 1). LBA 0 is read with the fallback geometry
; spt=9/heads=2 - that maps to CHS 0/0/1 under any real floppy geometry, so
; the boot sector is readable before the true geometry is known.
; -----------------------------------------------------------------------------
disk_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov [disk_drive], dl
    mov word [disk_spt], 9      ; fallback geometry for the LBA 0 read
    mov word [disk_heads], 2
    mov word [disk_nfiles], 0   ; stays 0 unless the mount fully succeeds
    mov byte [dsk_mntok], 0     ; the write gate (SPEC.md 18.4) closes for
                                ; the whole mount: a torn mount leaves it
                                ; closed, so nothing can write a volume
                                ; whose layout was never fully validated
    mov word [dsk_rover], 2     ; allocator hint + FAT dirty range: no
    mov word [dsk_fatd0], 0xFFFF    ; snapshot state survives a mount
    mov word [dsk_fatd1], 0
    mov word [ld_pending], 0    ; a queued click indexes the OLD directory;
                                ; never let it run against the new one
    cmp byte [dsk_keepcwd], 0   ; a remount always lands at the root, and
    jne .cwd_kept               ; dskw_* resolve there until dsk_chdir
    mov word [dsk_cwd], 0       ; moves - EXCEPT when dsk_chdir is the
.cwd_kept:                      ; caller, which is how navigation reuses
                                ; this whole validated pipeline (19.2)

    ; --- 1. boot sector: read LBA 0, validate the BPB (SPEC.md 18.2) ---
    mov ax, LOW_SEG
    mov es, ax
    xor ax, ax                  ; LBA 0
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .jfail
    call dsk_bpb_check          ; fills the 18.1 vars + disk_spt/disk_heads
    jc .jfail

    ; --- 2. FAT snapshot into FAT_SEG:0 (ES only, never DS) ---
    mov ax, FAT_SEG
    mov es, ax
    mov ax, [dsk_fatlba]
    mov cx, [dsk_fatsz]
    xor bx, bx
    call disk_read
    jnc .fat_ok
    cmp byte [dsk_nfats], 2     ; FAT1 unreadable: FAT2 is the one
    jne .jfail                  ; redundancy FAT gives for free
    mov ax, [dsk_fatlba]
    add ax, [dsk_fatsz]         ; FAT2 = FAT1 + FATSz16
    mov cx, [dsk_fatsz]
    xor bx, bx
    call disk_read
    jnc .fat_ok
.jfail:                         ; relay: short jc's cannot reach .fail
    jmp .fail
.fat_ok:

    ; --- 2a. is the current directory still ALLOCATED? (SPEC.md 19.2) --------
    ; A dsk_chdir remount carries [dsk_cwd] across, but the directory that
    ; cluster names can have stopped existing in the meantime: dskw_rmdir
    ; removed it from another file-manager window, or the foreign machine
    ; that had this floppy last deleted it. Removing a directory frees its
    ; cluster and nothing more - the sectors still hold intact '.' and '..'
    ; and a 0x00 terminator - so the walk below would mount and list it
    ; perfectly happily, and the next write would commit a directory entry
    ; into space the FAT calls free. dskw_alloc would then hand that same
    ; cluster to the next file and the user's folder would silently
    ; evaporate.
    ;
    ; The FAT is the only witness that can tell the difference, and by now
    ; it is resident: a live directory's entry is a link or an EOC mark,
    ; NEVER 0. Falling back to the root is safe in a way that guessing is
    ; not, and it self-corrects the window too - fmv_load (SPEC.md 22.1)
    ; already records what really happened rather than what it asked for,
    ; so the view's FS_CWD follows us here instead of dangling.
    cmp word [dsk_cwd], 0
    je .cwd_ok                  ; the root is not a cluster and cannot go
    mov ax, [dsk_cwd]
    call dsk_next_clus          ; CF = outside [2, dsk_maxclus]
    jc .cwd_lost
    test ax, ax                 ; 0 = free: the directory was removed
    jnz .cwd_ok
.cwd_lost:
    mov word [dsk_cwd], 0
.cwd_ok:

    ; --- 3. root-dir scan -> synthesized directory (SPEC.md 19) ---
    ; One sector at a time through dsk_secbuf. DS never leaves the kernel
    ; segment while parsing: raw fields are read via es:, the staged entry
    ; is built in dsk_ent (kernel scratch) and rep movsw'd out to disk_dir.
    mov ax, LOW_SEG
    mov es, ax
    xor bp, bp                  ; BP = accepted entry count
    mov ax, [dsk_cwd]           ; the current directory (0 = root at mount;
    call dsk_dirw_start         ; dsk_chdir re-enters here, SPEC.md 19.2)
.rootsec:
    call dsk_dirw_next          ; AX = next sector LBA, CF=1 = end of dir
    jc .scan_done
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .jfail                   ; a torn mount is a failed mount
    mov si, dsk_secbuf          ; ES:SI -> raw 32-byte entry
    mov cx, 16                  ; entries per sector
.entry:
    ; classify, in the binding SPEC.md 19 species order
    mov al, [es:si]             ; name[0]
    test al, al
    jz .scan_done               ; 0x00 = end of directory: stop the scan
    cmp al, 0xE5
    je .skip                    ; deleted
    mov ah, [es:si+DSK_R_ATTR]
    mov bl, ah
    and bl, 0x3F
    cmp bl, 0x0F
    je .skip                    ; LFN entry (mask, then compare, per spec)
    test ah, 0x08
    jnz .skip                   ; volume label
    test ah, 0x06
    jnz .skip                   ; hidden or system (DOS convention)
    cmp al, 0x20
    je .skip                    ; a name may not start with a space (spec)
    cmp al, '.'
    je .skip                    ; a subdirectory's own '.' and '..' links:
                                ; navigation uses dsk_dotdot, not these
                                ; (SPEC.md 19.2) - listing them would put
                                ; two undeletable oddities in every folder
    ; accepted: synthesize in kernel scratch, copy out (DS stays kernel)
    call dsk_synth              ; ES:SI raw entry -> dsk_ent (SPEC.md 19)
    push cx
    push si
    push di
    mov ax, bp
    mov cl, 5
    shl ax, cl                  ; accepted * 32
    add ax, disk_dir
    mov di, ax
    mov si, dsk_ent
    mov cx, DSK_DE_SIZE/2
    cld
    rep movsw                   ; DS=kernel -> ES=LOW_SEG
    pop di
    pop si
    pop cx
    inc bp
    cmp bp, 32
    jae .scan_done              ; 32-entry cap: extras are invisible
.skip:
    add si, 32
    loop .entry
    jmp .rootsec
.scan_done:

    ; disk_icons: blank ALL 32 slots up front so no slot ever carries a
    ; previous mount's bytes (SPEC.md 18.3 step 4: fully rewritten every
    ; mount) - the harvest below then only fills the slots that earn an icon.
    mov di, disk_icons
    mov cx, (32*DSK_ICO_SIZE)/2 ; 1024 words
    xor ax, ax
    cld
    rep stosw                   ; ES = LOW_SEG

    ; --- 4. icon harvest (SPEC.md 18.3 step 4; failures are NON-fatal) ---
    ; Type-1 entries carry a validated first cluster: peek the file's first
    ; sector, and if it is a v2 .o88 with the embedded-icon flag, its bytes
    ; 32..95 are the 16x16 icon body. Everything else - type 0, iconless,
    ; read failure - gets the all-zero slot (the generic-icon sentinel).
    ; disk_icons is fully rewritten every mount (SPEC.md 29.1).
    xor dx, dx                  ; DX = entry index
.harvest:
    cmp dx, bp
    jae .done
    mov ax, dx
    call dsk_get_dir            ; SI -> dsk_ent, the synthesized entry
    cmp word [si+16], 2         ; a folder: the one icon that is not
    je .h_folder                ; harvested off the disk but built in
    cmp word [si+16], 1         ; type word
    jne .h_zero
    mov ax, [si+18]             ; first cluster (validated at synthesis)
    call dsk_clus2lba
    jc .h_zero
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read              ; ES = LOW_SEG
    jc .h_zero
    mov si, dsk_secbuf          ; stage the 32 header bytes; dsk_ent's
    mov di, dsk_ent             ; directory copy is done with above
    mov cx, 32
    call dsk_copy_in
    cmp word [dsk_ent+0], 0x384F    ; .o88 magic 'O','8'
    jne .h_zero
    cmp byte [dsk_ent+2], 3         ; format version (v3, SPEC.md 18.3/20.2:
    jne .h_zero                     ; a v2 file shows the generic icon)
    test byte [dsk_ent+3], 1        ; embedded-icon flag
    jz .h_zero
    mov si, dsk_secbuf+32       ; icon body -> disk_icons + i*64, both
    mov ax, dx                  ; LOW_SEG (dsk_copy_low)
    mov cl, 6
    shl ax, cl
    add ax, disk_icons
    mov di, ax
    mov cx, DSK_ICO_SIZE
    call dsk_copy_low
    jmp .h_next

.h_folder:                      ; every folder gets the same built-in body.
    mov si, dsk_folder_ico      ; The one icon not harvested off the disk,
    mov ax, dx                  ; so it is also the one copied from .text
    mov cl, 6                   ; rather than through dsk_copy_low (which
    shl ax, cl                  ; moves LOW_SEG -> LOW_SEG); ES is already
    add ax, disk_icons          ; LOW_SEG here, so a plain rep movsb does it
    mov di, ax
    mov cx, DSK_ICO_SIZE
    cld
    rep movsb                   ; DS = kernel -> ES = LOW_SEG
    jmp .h_next

.h_zero:                        ; no icon: the slot is already blank
.h_next:
    inc dx
    jmp .harvest

.done:
    mov [disk_nfiles], bp       ; publish the count last
    mov byte [dsk_mntok], 1     ; ...and only now open the write gate
    clc
    jmp .out
.fail:
    mov word [disk_spt], 9      ; leave sane fallback geometry behind
    mov word [disk_heads], 2
    mov word [dsk_cwd], 0       ; a failed chdir must not leave the volume
                                ; pointed inside a directory nothing listed
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_bpb_check - validate the boot sector's BPB (module-internal)
; in:  dsk_secbuf holds LBA 0
; out: CF=0: the SPEC.md 18.2 table passed; the 18.1 variables and
;      disk_spt/disk_heads are filled, dsk_fattype set per the SPEC.md 19
;      detection. CF=1: reject; nothing guaranteed stored (the caller
;      resets the fallback).
; clobbers: nothing but CF
;
; Rule 1 runs via one es: word compare against the sector in low memory;
; the boot sector's first 64 bytes are then staged into dsk_bpb (kernel
; .bss) and every later rule runs on plain DS reads - odd-offset word loads
; are plain 8086 loads, no alignment requirement exists. The BPB is
; HOSTILE: each rule protects a specific downstream consumer (18.2), and
; the two failure modes the table exists to prevent are a divide fault or
; hang with sch_lock held and a read landing outside its buffer.
; -----------------------------------------------------------------------------
dsk_bpb_check:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; rule 1: boot signature 0xAA55 at bytes 510..511
    mov ax, LOW_SEG
    mov es, ax
    cmp word [es:dsk_secbuf+510], 0xAA55
    jne .jfail1

    ; stage the boot-sector head so rules 2..16 read plain DS (SPEC.md 18.2)
    mov si, dsk_secbuf
    mov di, dsk_bpb
    mov cx, 64
    call dsk_copy_in

    ; rule 2: BS_jmpBoot[0] in {0xEB, 0xE9} (MS-spec first validity test)
    mov al, [dsk_bpb+DSK_B_JMP]
    cmp al, 0xEB
    je .r3
    cmp al, 0xE9
    jne .jfail1
.r3:
    ; rule 3: bytes/sector == 512 exactly (disk_read moves exactly 512)
    cmp word [dsk_bpb+DSK_B_BPS], 512
    jne .jfail1
    ; rule 4: sectors/cluster a power of two (0 would divide-fault below)
    mov al, [dsk_bpb+DSK_B_SPC]
    test al, al
    jz .jfail1
    mov ah, al
    dec ah
    test al, ah                 ; x AND (x-1) == 0: {1,2,4,...,128}
    jnz .jfail1
    ; rule 5: reserved sectors >= 1 (the FAT starts at RsvdSecCnt)
    cmp word [dsk_bpb+DSK_B_RSVD], 1
    jb .jfail1
    ; rule 6: FAT copies in {1, 2}
    mov al, [dsk_bpb+DSK_B_NFATS]
    cmp al, 1
    je .r7
    cmp al, 2
    jne .jfail1
.r7:
    ; rule 7: root entries >= 1, <= 512, and a whole-sector count
    ; ((RootEntCnt*32) mod 512 == 0 <=> RootEntCnt mod 16 == 0)
    mov ax, [dsk_bpb+DSK_B_ROOTN]
    test ax, ax
    jz .jfail1
    cmp ax, 512
    ja .jfail1
    test ax, 15
    jnz .jfail1
    ; rule 8: TotSec16 != 0 (0 => the count is 32-bit => LBAs past 16 bits)
    cmp word [dsk_bpb+DSK_B_TOT16], 0
    je .jfail1
    ; rule 9: media byte in {0xF0, 0xF8..0xFF}
    mov al, [dsk_bpb+DSK_B_MEDIA]
    cmp al, 0xF0
    je .r10
    cmp al, 0xF8
    jae .r10
.jfail1:                        ; relay: the early rules' jumps land here
    jmp .fail
.r10:
    ; rule 10: 1 <= FATSz16 <= DSK_FAT_SECS (the snapshot buffer's size)
    mov ax, [dsk_bpb+DSK_B_FATSZ]
    test ax, ax
    jz .jfail1
    cmp ax, DSK_FAT_SECS
    ja .jfail1
    ; rule 11: sectors/track on the real-geometry whitelist - a hostile
    ; spt*heads product past 16 bits would zero disk_read's CHS divisor
    ; and divide-fault with sch_lock held
    mov ax, [dsk_bpb+DSK_B_SPT]
    cmp ax, 8
    je .r12
    cmp ax, 9
    je .r12
    cmp ax, 15
    je .r12
    cmp ax, 18
    je .r12
    cmp ax, 21
    je .r12
    cmp ax, 36
    jne .jfail1
.r12:
    ; rule 12: heads in {1, 2} (same divisor protection)
    mov ax, [dsk_bpb+DSK_B_HEADS]
    cmp ax, 1
    je .r13
    cmp ax, 2
    jne .jfail1
.r13:
    ; rule 13: TotSec16 <= spt*heads*80 - every in-volume LBA stays
    ; CHS-reachable under disk_read's cyl<80 guard
    mov ax, [dsk_bpb+DSK_B_SPT]
    mul word [dsk_bpb+DSK_B_HEADS]  ; AX = spt*heads (<= 72), DX = 0
    mov cx, 80
    mul cx                          ; AX = spt*heads*80 (<= 5760)
    cmp [dsk_bpb+DSK_B_TOT16], ax
    ja .fail

    ; the raw fields are now trusted: stash them (SPEC.md 18.1)
    mov al, [dsk_bpb+DSK_B_SPC]
    mov [dsk_spc], al
    mov al, [dsk_bpb+DSK_B_NFATS]
    mov [dsk_nfats], al
    mov ax, [dsk_bpb+DSK_B_RSVD]
    mov [dsk_fatlba], ax        ; FAT1 starts at RsvdSecCnt
    mov ax, [dsk_bpb+DSK_B_FATSZ]
    mov [dsk_fatsz], ax

    ; derived layout (SPEC.md 19 detection; all sums carry-checked - a
    ; hostile RsvdSecCnt near 0xFFFF must reject, not wrap)
    mov ax, [dsk_bpb+DSK_B_ROOTN]
    mov cl, 4
    shr ax, cl                  ; RootDirSectors = RootEntCnt/16 (rule 7:
    mov [dsk_rootsecs], ax      ; an exact multiple), <= 32
    mov al, [dsk_nfats]
    xor ah, ah
    mul word [dsk_fatsz]        ; AX = NumFATs*FATSz16 (<= 64), DX = 0
    add ax, [dsk_fatlba]
    jc .fail
    mov [dsk_rootlba], ax       ; first root-dir LBA
    add ax, [dsk_rootsecs]
    jc .fail
    mov [dsk_datalba], ax       ; FirstDataSec
    ; rule 14: FirstDataSec + SecPerClus <= TotSec16 (>= 1 cluster exists;
    ; DataSec underflow guard)
    mov bl, [dsk_spc]
    xor bh, bh
    add ax, bx
    jc .fail
    cmp ax, [dsk_bpb+DSK_B_TOT16]
    ja .fail
    ; CountOfClusters = (TotSec16 - FirstDataSec) / SecPerClus
    mov ax, [dsk_bpb+DSK_B_TOT16]
    sub ax, [dsk_datalba]
    xor dx, dx
    div bx                      ; BX = spc >= 1: no divide fault
    ; rule 15: 1 <= CountOfClusters < 65525 (FAT32 is out of scope -
    ; provably dead after rule 13, kept as reordering insurance)
    test ax, ax
    jz .fail
    cmp ax, 65525
    jae .fail
    ; FAT type from the cluster count ALONE (SPEC.md 19): < 4085 = FAT12
    mov byte [dsk_fattype], 0
    cmp ax, 4085
    jb .typed
    mov byte [dsk_fattype], 1
.typed:
    inc ax
    mov [dsk_maxclus], ax       ; CountOfClusters+1 = highest valid cluster
    ; rule 16: the FAT must cover its own cluster count, or dsk_next_clus
    ; would read past the snapshot. entries = CountOfClusters+2.
    inc ax                      ; AX = entries (<= 65,526: no wrap)
    mov bx, [dsk_fatsz]
    mov cl, 9
    shl bx, cl                  ; BX = FAT bytes resident (<= 16,384)
    cmp byte [dsk_fattype], 0
    jne .cap16
    mov dx, ax                  ; FAT12: ceil(entries*1.5)
    inc dx                      ; = entries + ceil(entries/2)
    shr dx, 1                   ; (entries <= 4,086 here: fits 16 bits)
    add ax, dx
    jmp short .capcmp
.cap16:
    shl ax, 1                   ; FAT16: entries*2
    jc .fail                    ; a wrap is certainly past the snapshot
.capcmp:
    cmp ax, bx
    ja .fail

    ; all 16 rules passed: the geometry fields become disk_read's own
    mov ax, [dsk_bpb+DSK_B_SPT]
    mov [disk_spt], ax
    mov ax, [dsk_bpb+DSK_B_HEADS]
    mov [disk_heads], ax
    clc
    jmp short .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_synth - synthesize one staged directory entry (module-internal)
; in:  ES:SI -> an ACCEPTED raw 32-byte root entry (ES = LOW_SEG);
;      the SPEC.md 18.1 variables are valid
; out: dsk_ent holds the 32-byte staged entry (SPEC.md 19): display name
;      @0 (NUL-padded, sanitized), type word @16, first cluster @18, size
;      word @20 (clamped), zero @22..31
; clobbers: flags
;
; DS stays the kernel segment throughout - every raw field is read with an
; es: override, and the entry is built in kernel scratch.
; -----------------------------------------------------------------------------
dsk_synth:
    push ax
    push bx
    push cx
    push dx
    push di

    mov di, dsk_ent             ; zero the staged entry first: the name's
    mov cx, DSK_DE_SIZE/2       ; NUL padding and bytes 22..31 fall out
.zero:
    mov word [di], 0
    inc di
    inc di
    loop .zero

    ; --- display name: stem[.ext], trimmed, sanitized (SPEC.md 19) ---
    mov cx, 8                   ; stem length = last non-space index + 1
.slen:
    mov bx, si
    add bx, cx
    mov al, [es:bx-1]           ; raw name[cx-1]
    cmp al, ' '
    jne .sgot
    loop .slen
.sgot:                          ; CX = stem length (0 iff all spaces)
    mov di, dsk_ent
    jcxz .stem_done
    xor bx, bx
.scopy:
    mov al, [es:bx+si]
    test bx, bx
    jnz .nokanji
    cmp al, 0x05                ; KANJI escape: first byte displays as 0xE5
    jne .nokanji                ; (which the sanitizer then folds to '_')
    mov al, 0xE5
.nokanji:
    call dsk_sanit
    mov [di], al
    inc di
    inc bx
    cmp bx, cx
    jb .scopy
.stem_done:
    mov cx, 3                   ; ext length, same trailing-space trim
.elen:
    mov bx, si
    add bx, cx
    mov al, [es:bx+7]           ; raw ext[cx-1] = raw[8+cx-1]
    cmp al, ' '
    jne .egot
    loop .elen
    jmp short .name_done        ; blank ext: the dot is omitted
.egot:
    mov byte [di], '.'
    inc di
    xor bx, bx
.ecopy:
    mov al, [es:bx+si+DSK_R_EXT]
    call dsk_sanit
    mov [di], al
    inc di
    inc bx
    cmp bx, cx
    jb .ecopy
.name_done:

    ; --- @18: first cluster = raw FstClusLO, verbatim even when type=0 ---
    mov ax, [es:si+DSK_R_CLUS]
    mov [dsk_ent+18], ax
    ; --- @20: size word = raw size dword, clamped to 0xFFFF (display) ---
    mov dx, [es:si+DSK_R_SIZE+2]    ; DX = size high word (held for below)
    mov ax, [es:si+DSK_R_SIZE]
    test dx, dx
    jz .szok
    mov ax, 0xFFFF
.szok:
    mov [dsk_ent+20], ax
    ; --- @16: type 2 = subdirectory, and nothing else about it is trusted:
    ;     a folder's size field is zero by spec, and its first cluster is
    ;     validated exactly like a file's because dsk_chdir will walk it
    ;     (SPEC.md 19.2). Tested first - the ext rule below must never see
    ;     a directory, or a folder literally named "X.O88" would type as a
    ;     package and reach the loader.
    test byte [es:si+DSK_R_ATTR], 0x10
    jz .notdir
    mov ax, [es:si+DSK_R_CLUS]
    cmp ax, 2
    jb .typed                   ; a directory we cannot walk is type 0:
    cmp ax, [dsk_maxclus]       ; listed, inert, never enterable
    ja .typed
    mov word [dsk_ent+16], 2
    mov word [dsk_ent+20], 0    ; folders have no meaningful size
    jmp short .typed
.notdir:

    ; --- @16: type word = 1 iff ext == "O88" AND size fits 16 bits AND
    ;     size >= 1 AND first cluster in [2, dsk_maxclus] (SPEC.md 19) -
    ;     a garbage entry can never reach the loader as type 1
    cmp byte [es:si+DSK_R_EXT], 'O'
    jne .typed
    cmp byte [es:si+DSK_R_EXT+1], '8'
    jne .typed
    cmp byte [es:si+DSK_R_EXT+2], '8'
    jne .typed
    test dx, dx                 ; size high word must be 0
    jnz .typed
    cmp word [es:si+DSK_R_SIZE], 1
    jb .typed                   ; size low word >= 1
    mov ax, [es:si+DSK_R_CLUS]
    cmp ax, 2
    jb .typed
    cmp ax, [dsk_maxclus]
    ja .typed
    mov word [dsk_ent+16], 1
.typed:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_sanit - display-name sanitizer (module-internal)
; in:  AL = raw name byte
; out: AL = the byte, or '_' if outside 0x21..0x7E (OEM-codepage bytes
;      never reach the font renderer - SPEC.md 19)
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
dsk_sanit:
    cmp al, 0x21
    jb .fold
    cmp al, 0x7E
    jbe .ok
.fold:
    mov al, '_'
.ok:
    ret

; -----------------------------------------------------------------------------
; dsk_clus2lba - map a cluster number to its first LBA
; in:  AX = cluster number
; out: CF=0, AX = first LBA of that cluster;
;      CF=1 if AX < 2 or AX > [dsk_maxclus] (AX undefined)
; clobbers: AX (the output), CF
;
; LBA = dsk_datalba + (AX-2) * dsk_spc. The 16-bit mul cannot wrap for a
; valid cluster (rules 14/15 bound it inside TotSec16); DX != 0 after the
; mul is treated as CF=1 anyway, belt and braces.
; -----------------------------------------------------------------------------
dsk_clus2lba:
    push dx
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    sub ax, 2
    mov dl, [dsk_spc]
    xor dh, dh
    mul dx                      ; DX:AX = (cluster-2) * spc
    test dx, dx
    jnz .bad
    add ax, [dsk_datalba]
    jc .bad
    clc
    jmp short .out
.bad:
    stc
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; dsk_next_clus - read one FAT entry from the snapshot
; in:  AX = cluster number
; out: CF=0, AX = raw FAT entry value (FAT12: 12-bit);
;      CF=1 if the input is outside [2, dsk_maxclus]
; clobbers: AX (the output), CF
;
; The SINGLE reader of FAT_SEG (SPEC.md 2.1), via ES only. FAT12 entry:
; off = clus + clus/2, then the word straddling that offset (an unaligned
; word load - legal on 8086; the 3-nibble problem dissolves against a
; resident FAT): odd cluster takes the top 12 bits, even the bottom.
; FAT16 entry: a plain word array. Rule 16 guarantees off+1 stays inside
; the snapshot for every valid cluster. No EOC/bad-mark decoding lives
; here - dsk_read_chain's walk is size-driven (SPEC.md 18).
; -----------------------------------------------------------------------------
dsk_next_clus:
    push bx
    push cx
    push es
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    mov bx, FAT_SEG
    mov es, bx
    cmp byte [dsk_fattype], 0
    jne .fat16
    mov bx, ax                  ; FAT12: off = clus + clus>>1
    shr bx, 1
    add bx, ax
    mov bx, [es:bx]             ; the straddling word
    test ax, 1
    jz .even
    mov cl, 4                   ; odd cluster: top 12 bits
    shr bx, cl
    mov ax, bx
    jmp short .done
.even:
    and bx, 0x0FFF              ; even cluster: bottom 12 bits
    mov ax, bx
    jmp short .done
.fat16:
    mov bx, ax                  ; FAT16: off = clus*2
    shl bx, 1
    mov ax, [es:bx]
.done:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_dirw_start / dsk_dirw_next - iterate a directory's sectors (SPEC 19.2)
;
; A FAT12/16 volume has TWO directory shapes: the root is a fixed region of
; [dsk_rootsecs] sectors at [dsk_rootlba], and every subdirectory is an
; ordinary cluster chain. Every directory scan in the kernel - the mount
; listing (SPEC.md 18.3), the write path's slot search (SPEC.md 18.4) -
; runs through this one iterator, so the two shapes are spelled out once
; and cannot drift apart.
;
; dsk_dirw_start  in: AX = the directory's first cluster, 0 = the root
;                 out: nothing (all registers preserved)
; dsk_dirw_next   out: CF=0 and AX = the next sector's LBA;
;                      CF=1 = the directory ends here
;                 clobbers: AX (the output), CF
;
; End-of-chain needs no EOC constants, exactly as in dsk_read_chain: any
; next-value outside [2, dsk_maxclus] ends the walk, which folds EOC marks,
; free entries and bad marks into one case. A cross-linked FAT could cycle
; forever, though - a directory walk has no file size to bound it the way
; dsk_read_chain does - so DSK_DIRW_MAX caps the sectors any one walk will
; visit. 256 sectors is 4,096 entries, past anything real and far past the
; 32-entry listing cap.
; -----------------------------------------------------------------------------
DSK_DIRW_MAX equ 256             ; hostile-FAT guard: max sectors per walk

dsk_dirw_start:
    mov [dsk_wclus], ax
    mov word [dsk_wsec], 0
    mov word [dsk_wguard], DSK_DIRW_MAX
    ret

dsk_dirw_next:
    push bx
    cmp word [dsk_wguard], 0
    je .end                     ; a looped chain stops here, not never
    dec word [dsk_wguard]
    mov ax, [dsk_wclus]
    or ax, ax
    jnz .sub

    mov ax, [dsk_wsec]          ; the root: a flat run of sectors
    cmp ax, [dsk_rootsecs]
    jae .end
    inc word [dsk_wsec]
    add ax, [dsk_rootlba]
    clc
    jmp short .out

.sub:                           ; a subdirectory: spc sectors per cluster
    mov bl, [dsk_spc]
    xor bh, bh
    cmp [dsk_wsec], bx
    jb .have
    mov ax, [dsk_wclus]
    call dsk_next_clus          ; CF=1 only if the CURRENT link is invalid
    jc .end
    mov [dsk_wclus], ax         ; the new link is validated by clus2lba
    mov word [dsk_wsec], 0
.have:
    mov ax, [dsk_wclus]
    call dsk_clus2lba           ; CF=1 = outside [2, dsk_maxclus]: the end
    jc .end
    add ax, [dsk_wsec]
    inc word [dsk_wsec]
    clc
    jmp short .out
.end:
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_chdir - make a directory the current one and re-list the volume
; in:  AX = the directory's first cluster (0 = the root),
;      DL = drive (0 = A:, 1 = B:)
; out: CF=0 listed; CF=1 = the mount failed (the volume is back at the root
;      and [dsk_mntok] is closed, exactly as after any failed mount)
; clobbers: nothing else (flags)
;
; Navigation is a REMOUNT (SPEC.md 19.2), which is the same choice
; SPEC.md 18.4 already made for metadata writes: rather than a second way
; to fill disk_dir/disk_icons, entering a folder re-runs the one validated
; pipeline - BPB re-checked, FAT re-snapshotted, listing and icon harvest
; rebuilt - with [dsk_keepcwd] the single instruction's worth of difference.
; It costs a floppy's worth of sectors per navigation and buys the property
; that disk_dir is ALWAYS exactly a mount snapshot, with no third staleness
; rule anywhere in the kernel.
;
; DL is an INPUT rather than a read of [disk_drive] so a caller can name the
; drive it means. Every caller today passes [disk_drive] and the two forms
; are identical - but the day a file-manager window carries its own drive,
; the version that read the global would let a background window navigate
; the OTHER drive's directory, and that is a bug you find by data loss.
; -----------------------------------------------------------------------------
dsk_chdir:
    mov [dsk_cwd], ax
    mov byte [dsk_keepcwd], 1
    call disk_mount
    mov byte [dsk_keepcwd], 0
    ret

; -----------------------------------------------------------------------------
; dsk_dotdot - the current directory's parent (SPEC.md 19.2)
; in:  nothing; [dsk_cwd] must be non-zero (inside a subdirectory)
; out: CF=0, AX = the parent's first cluster, 0 = the root; CF=1 = the '..'
;      link is missing or unreadable
; clobbers: AX (the output), CF
;
; Every subdirectory carries '.' and '..' as its first two entries by spec,
; and '..' holds the parent's first cluster - with the FAT convention that
; a parent OF the root is written as cluster 0, which is exactly the value
; this kernel already uses for "the root". So going up needs no path stack
; and no memory of how we got here: the disk itself records it.
; -----------------------------------------------------------------------------
dsk_dotdot:
    push bx
    push cx
    push si
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dsk_cwd]
    call dsk_dirw_start
    call dsk_dirw_next          ; the directory's very first sector
    jc .bad
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .bad
    mov si, dsk_secbuf + 32     ; entry 1 = '..' by spec
    cmp byte [es:si], '.'
    jne .bad
    cmp byte [es:si+1], '.'
    jne .bad
    mov ax, [es:si+DSK_R_CLUS]
    cmp ax, [dsk_maxclus]       ; 0 (the root) or a walkable cluster; a
    ja .bad                     ; corrupt '..' must not become the cwd
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_read_chain - read a file's cluster chain (SPEC.md 18)
; in:  AX = first cluster, DX = sectors to read (= ceil(fsize/512), >= 1),
;      ES:BX = destination (advances 512/sector, exactly DX sectors)
; out: CF=0 all read; CF=1 with AL = 1 (disk error) / 2 (chain corrupt)
; clobbers: CF; AL on failure (everything else preserved)
;
; Size-driven walk with run coalescing: contiguous clusters accumulate into
; one (run_lba, run_len) pending run, flushed through the unmodified
; single-sector disk_read - a freshly built (contiguous) image flushes as
; ONE call. The walk consumes >= 1 sector per pass, so a looped or
; cross-linked chain cannot hang it - it just yields wrong bytes, which
; the loader's in-region header recheck rejects (SPEC.md 21). No EOC
; constants exist: ANY next-value outside [2, dsk_maxclus] while sectors
; remain is corruption (the FAT spec keeps bad/EOC marks clear of valid
; cluster numbers under correct type detection).
; -----------------------------------------------------------------------------
dsk_read_chain:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov si, ax                  ; SI = current cluster
    xor cx, cx                  ; CX = run_len (0 = no pending run)
    xor di, di                  ; DI = run_lba (dead while run_len = 0)
.next:
    mov ax, si
    call dsk_clus2lba
    jc .fail2                   ; out-of-range link = corruption
    mov bp, ax                  ; BP = this cluster's LBA
    mov al, [dsk_spc]           ; take = min(spc, sectors remaining) -
    xor ah, ah                  ; the final cluster may be partial
    cmp ax, dx
    jbe .take
    mov ax, dx
.take:
    jcxz .newrun                ; no pending run yet
    push di
    add di, cx                  ; run end LBA
    cmp di, bp
    pop di
    je .extend                  ; contiguous: grow the run
    call .flush                 ; discontiguous: flush, then start anew
    jc .fail1
.newrun:
    mov di, bp
    mov cx, ax
    jmp short .consume
.extend:
    add cx, ax
.consume:
    sub dx, ax
    jz .final                   ; exactly DX sectors consumed: done
    mov ax, si
    call dsk_next_clus
    jc .fail2
    cmp ax, 2                   ; the next link must itself be a valid
    jb .fail2                   ; cluster - EOC/bad/free while sectors
    cmp ax, [dsk_maxclus]       ; remain is corruption
    ja .fail2
    mov si, ax
    jmp short .next
.final:
    call .flush
    jc .fail1
    clc
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.fail1:
    mov byte [dsk_cherr], 1     ; disk_read failed
    jmp short .failc
.fail2:
    mov byte [dsk_cherr], 2     ; chain corrupt
.failc:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, [dsk_cherr]
    stc
    ret
.flush:                         ; read the pending run (DI = LBA, CX = len)
    push ax                     ; to ES:BX; advance BX; CF through. AX is
    mov ax, di                  ; the caller's take count - preserved
    call disk_read              ; in: AX=LBA, CX=count, ES:BX
    jc .flret
    mov ax, cx                  ; BX += len*512 (disk_read preserves BX;
    push cx                     ; only its internal pointer advanced)
    mov cl, 9
    shl ax, cl
    pop cx
    add bx, ax
    clc
.flret:
    pop ax                      ; pop leaves CF alone
    ret

; -----------------------------------------------------------------------------
; dsk_copy_seg - copy CX bytes between two arbitrary segments
;
; The one body that reaches outside the kernel segment for data. DS is the
; SOURCE segment for the duration, so NO kernel variable is readable between
; the segment loads - keep the body to the string move, and pass everything
; in registers. The three wrappers below name the segment pairs the kernel
; actually uses; nothing else may call this with DS left dangling.
;
; in:       DX = source segment, AX = destination segment,
;           SI = source offset, DI = destination offset, CX = bytes (even)
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
dsk_copy_seg:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov es, ax
    mov ds, dx                  ; last: no kernel variable is legible below
    shr cx, 1
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_copy_in - copy CX bytes from LOW_SEG:SI into the kernel segment at DI
; in:       SI = LOW_SEG offset, DI = kernel offset, CX = byte count (even)
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
dsk_copy_in:
    push ax
    push dx
    mov dx, LOW_SEG
    mov ax, cs                  ; destination = the kernel segment (this code
    call dsk_copy_seg           ; is in .text, so CS is it)
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_copy_low - copy CX bytes between two LOW_SEG offsets (module-internal)
;
; Used by the icon harvest (dsk_secbuf -> disk_icons, both LOW_SEG). Writing
; disk_dir FROM kernel scratch needs no helper: DS stays kernel, ES = LOW_SEG.
;
; in:       SI = source LOW_SEG offset, DI = dest LOW_SEG offset,
;           CX = byte count (even)
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
dsk_copy_low:
    push ax
    push dx
    mov dx, LOW_SEG
    mov ax, dx
    call dsk_copy_seg
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_copy_out - copy CX bytes from LOW_SEG:SI into an arbitrary segment
;
; The file manager's cache filler (SPEC.md 22.1): disk_dir / disk_icons live
; in LOW_SEG and the per-window slots live in VIEW_SEG, so neither end is
; addressable through DS and a plain rep movsw will not do.
;
; in:       AX = destination segment, SI = LOW_SEG offset, DI = dest offset,
;           CX = byte count (even)
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
dsk_copy_out:
    push dx
    mov dx, LOW_SEG
    call dsk_copy_seg
    pop dx
    ret

; -----------------------------------------------------------------------------
; dsk_get_dir - stage one synthesized directory entry in the kernel segment
; in:       AX = entry index (0..31; the caller validates against disk_nfiles)
; out:      SI -> dsk_ent, holding that entry's 32 bytes
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
dsk_get_dir:
    push ax
    push cx
    push di
    mov cl, 5                   ; index * 32 (index <= 31: no overflow)
    shl ax, cl
    add ax, disk_dir
    mov si, ax
    mov di, dsk_ent
    mov cx, DSK_DE_SIZE
    call dsk_copy_in
    pop di
    pop cx
    pop ax
    mov si, dsk_ent
    ret

; -----------------------------------------------------------------------------
; dsk_get_icon - stage one harvested icon slot in the kernel segment
; in:       AX = entry index (0..31)
; out:      SI -> dsk_ico, holding that entry's 64 bytes (all-zero = no icon)
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
dsk_get_icon:
    push ax
    push cx
    push di
    mov cl, 6                   ; index * 64
    shl ax, cl
    add ax, disk_icons
    mov si, ax
    mov di, dsk_ico
    mov cx, DSK_ICO_SIZE
    call dsk_copy_in
    pop di
    pop cx
    pop ax
    mov si, dsk_ico
    ret

; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG: written by int 13h through ES:BX
                                ; (or rep movsw/stosw from the mount), read
                                ; only through dsk_get_* above
disk_dir:   resb 1024           ; 32 SYNTHESIZED entries x 32 bytes, built
                                ; by disk_mount from the FAT root directory
                                ; (SPEC.md 19) - never a raw on-disk image
disk_icons: resb 2048           ; 32 harvested icon slots x 64 bytes; slot i
                                ; belongs to directory entry i, all-zero =
                                ; no icon; fully rewritten every mount
dsk_secbuf: resb 512            ; sector scratch: BPB read, root-dir scan,
                                ; icon harvest, and the loader's header peek

section .bss

dsk_ent:    resb DSK_DE_SIZE    ; one staged directory entry (dsk_get_dir);
                                ; also the mount's synthesis/harvest scratch
dsk_ico:    resb DSK_ICO_SIZE   ; one staged icon slot (dsk_get_icon)

; mount-derived FAT state (SPEC.md 18.1) - valid only after a successful
; mount; every consumer is already gated by disk_nfiles != 0
dsk_bpb:      resb 64           ; staged boot-sector head (mount scratch)
dsk_fattype:  resb 1            ; 0 = FAT12, 1 = FAT16 (SPEC.md 19)
dsk_spc:      resb 1            ; BPB_SecPerClus (validated power of two)
dsk_fatlba:   resw 1            ; = RsvdSecCnt (FAT1 start LBA)
dsk_fatsz:    resw 1            ; = FATSz16 (<= DSK_FAT_SECS)
dsk_nfats:    resb 1            ; 1 or 2
dsk_rootlba:  resw 1            ; first root-dir LBA
dsk_rootsecs: resw 1            ; root-dir sector count (<= 32)
dsk_datalba:  resw 1            ; FirstDataSec
dsk_maxclus:  resw 1            ; CountOfClusters+1 = highest valid cluster
dsk_cherr:    resb 1            ; dsk_read_chain: failure code across the
                                ; register-restore epilogue

; --- the directory walker's scratch (SPEC.md 19.2) --------------------------
; Safe in .bss: dsk_dirw_start writes all three before any walk reads them.
; [dsk_cwd] and [dsk_keepcwd] are NOT - see the module data block up top.
dsk_wclus:    resw 1            ; dirw: the cluster being walked, 0 = root
dsk_wsec:     resw 1            ; dirw: sector index inside it
dsk_wguard:   resw 1            ; dirw: sectors left before the walk gives up
dsk_op:       resb 1            ; int 13h function for dsk_xfer: 02h / 03h
dsk_ioerr:    resb 1            ; status (AH) of the last FAILED transfer;
                                ; 03h = write-protected media (SPEC.md 18.4)

; --- write-path state (SPEC.md 18.4), all reset by disk_mount ---------------
dsk_mntok:    resb 1            ; 1 only between a fully successful mount and
                                ; the next mount attempt - the write gate
dsk_wpad:     resb 1            ; keeps the words below even-aligned
dsk_rover:    resw 1            ; next cluster the allocator examines
dsk_fatd0:    resw 1            ; dirty FAT sector range [lo, hi] inclusive,
dsk_fatd1:    resw 1            ; sector indices within ONE FAT; lo = FFFFh
                                ; means clean

section .text
