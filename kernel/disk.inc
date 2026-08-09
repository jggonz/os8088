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
DSK_FIND_SZ  equ 24              ; dsk_find's answer (SPEC.md 19.7.1): a
                                 ; 13-byte NUL name, the attribute, the type
                                 ; word, the first cluster, a 32-bit size and
                                 ; one reserved word
DSK_ICO_SIZE equ 64              ; harvested icon slot stride (SPEC.md 19)

; How many entries a listing holds. DSK_NENT is the FLOOR - the .lowbss pair
; every floppy uses, and the fallback for a driver-backed volume whose driver
; could not fund a bigger one. DSK_VENT is what a hard disk gets: 96 bytes an
; entry (32 of directory + 64 of icon), so 64 entries is a 6KB claim, made by
; the driver at mount and mirrored per open Disk window by the file manager
; (SPEC.md 22.6). Both numbers are here so nothing can disagree about them.
DSK_NENT     equ 32
DSK_VENT     equ 64
DSK_VBYTES   equ DSK_VENT * (DSK_DE_SIZE + DSK_ICO_SIZE)    ; 6,144 = 6KB
DSK_VKB      equ 6

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

; --- the volume table (SPEC.md 18.7) -----------------------------------------
; A VOLUME IS A SMALL INDEX - 0 = A:, 1 = B:, 2 = C:, 3 = D: - and never an
; int 13h drive number. Everything that already turns a drive into a letter
; (`add al, 'A'` in files.inc), into a desktop zone (desk.inc) or into a
; window's FS_DRV keeps working untouched, which is the whole reason for the
; choice: putting 80h in [disk_drive] would have broken all three, silently.
;
; The int 13h drive number, or a driver's own volume handle, lives in the row.
; Rows 0 and 1 are the BIOS floppies and are never taken away; rows 2 and up
; are registered by a DRVC_DISK driver (osapi_vol_add) and dropped when it
; unloads.
;
; **The kernel knows nothing about partitions.** A driver-backed volume's LBAs
; are VOLUME-RELATIVE and 16-bit, exactly like a floppy's, and the driver adds
; its own 32-bit partition base. So the whole FAT layer - dsk_clus2lba,
; dsk_read_chain's run coalescer, dskw_flush - is untouched by capacity, and
; a volume is capped at 65,535 sectors (31.99MB) by BPB rule 8, which is also
; the DOS 3.3 partition limit these machines ran.
DVOL_MAX     equ 6               ; A:, B:, and one per PARTITION a disk
                                 ; driver can mount: four primaries is the
                                 ; whole of what a partition table holds
                                 ; (SPEC.md 52.2), and each is its own volume,
                                 ; its own drive letter and its own desktop
                                 ; icon - so C: through F:
DV_KIND      equ 0               ; db: 0 = BIOS int 13h, 1 = driver, 0xFF free
DV_UNIT      equ 1               ; db: the int 13h DL, or the driver's handle
DV_FLAGS     equ 2               ; db: bit 0 = show a desktop zone (SPEC 26)
DV_PAD       equ 3               ; db: (the stride stays a power of two)
                                 ; Every kind-1 row belongs to the ONE
                                 ; published DRVC_DISK driver - there is one
                                 ; publication slot per class (SPEC.md
                                 ; 51.2.1) - so nothing has to record WHICH
                                 ; driver a volume came from
DV_SECS      equ 4               ; dw: sectors in the volume, 0 = unknown.
                                 ;     BPB rule 13's replacement reads it
DV_SEG       equ 6               ; dw: the listing claim its driver donated,
                                 ;     0 = use the .lowbss floor (22.6)
DV_LBL       equ 8               ; db[8]: the desktop label, NUL-terminated
                                 ;     and INLINE - a desktop label is six
                                 ;     glyphs (the 48px zone), and inline
                                 ;     means `lea si, [bx+DV_LBL]` is already
                                 ;     the DS pointer font_str wants, with no
                                 ;     staging buffer behind it
DV_SIZE      equ 16

DVK_BIOS     equ 0
DVK_DRV      equ 1
DVK_FREE     equ 0xFF

; .text and not .bss for the reason every other boot-reachable table is: -f bin
; zeroes nothing, and the first disk_mount runs before anything could.
dsk_vtab:
    db DVK_BIOS, 0, 0, 0         ; volume 0 = A:
    dw 0, 0
    db 'Disk A', 0, 0
    db DVK_BIOS, 1, 0, 0         ; volume 1 = B:
    dw 0, 0
    db 'Disk B', 0, 0
    times DVOL_MAX - 2 db DVK_FREE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

; --- the BOOT VOLUME's partition base (SPEC.md 52.10.3) ----------------------
; A SIDE TABLE, for inst_fdrv's reason (SPEC.md 19.2.1): DV_SIZE is 16 and the
; row is full - DV_LBL runs to the last byte of it - and index*DV_SIZE with a
; power-of-two stride is what makes a row cheap to reach everywhere else.
;
; It is non-zero for exactly ONE row and usually for none: the volume the
; machine booted from when that was a hard-disk partition. Every driver-backed
; volume adds its own base inside the driver (SPEC.md 18.7), and a floppy has
; none - so a machine that boots from a floppy carries 24 zero bytes and runs
; the arithmetic below on a base of 0, which is the identical transfer it made
; before this existed.
;
; .text with real initialisers, like dsk_vtab above it and for the same
; reason: -f bin zeroes nothing and the first disk_mount runs before any init
; routine of ours could.
dsk_vbase:   times DVOL_MAX dd 0

; Which volume the system files are on - what drv_mounted mounts (SPEC.md
; 51.5.1) and what the desktop calls the system disk. 0 = A:, which is every
; floppy machine and was hardcoded as `xor dl, dl` until an installed hard
; disk could be the answer instead.
dsk_bootvol: db 0

; The boot drive's own geometry, probed once (SPEC.md 52.10.3). Zero on a
; floppy machine and unread there. disk_mount needs it for the ONE read that
; cannot get a geometry from the BPB - the read that fetches the BPB - and
; there is nowhere else to get it: the volume table carries a unit and a base,
; and the drive's shape belongs to the drive.
dsk_bootspt: dw 0
dsk_boothds: dw 0

; --- module data (initialized: lives in .text, not .bss) ---------------------
; 0040:003F's motor bit per int 13h drive (SPEC.md 18.9.1). A table because
; dsk_here_ok preserves every register and `shl al, cl` would spend CX.
dsk_mbit:    db 1, 2, 4, 8

disk_drive:  db 1                ; current VOLUME (0 = A:, 1 = B:), init B:
disk_spt:    dw 9                ; sectors per track  } fallback geometry,
disk_heads:  dw 2                ; heads              } replaced at mount
disk_nfiles: dw 0                ; file count, valid after a good mount

; The diskette parameter table (SPEC.md 18.92) - int 1Eh's, ours from
; dsk_dpt_init onward. In .text because the BIOS reads it on EVERY floppy
; operation and dsk_xfer writes byte 4 before every transfer; the values here
; are the fallback for a machine whose vector is null, and are the IBM 5.25"
; ones with the ONE byte this exists for already right.
dsk_dpt:
    db 0xDF                      ; +0  step rate / head unload
    db 0x02                      ; +1  head load, DMA
    db 0x25                      ; +2  motor-off delay, ticks
    db 0x02                      ; +3  bytes/sector code: 2 = 512
    db 9                         ; +4  EOT - THE one dsk_xfer rewrites
    db 0x2A                      ; +5  gap length, read/write
    db 0xFF                      ; +6  DTL
    db 0x50                      ; +7  gap length, format
    db 0xF6                      ; +8  format fill byte
    db 0x0F                      ; +9  head settle, ms
    db 0x08                      ; +10 motor start, 1/8 s
DPT_LEN equ 11

%ifdef DISK_COUNTERS
; The measurement instruments of docs/DISK-PERF-PLAN.md 2, compiled in only by
; `make DISKCNT=1` and never shipped. Read over QMP with `xp /3xh <linear>`;
; derive the offset from `nasm -l` after EVERY rebuild, because anything added
; to an include before this one moves it. They are never reset: take a reading,
; do the thing, take another, subtract.
;
; They are also PUBLISHED, at the fixed offset 0060:000E, so a test package can
; read them without an API slot (SPEC.md 18.94). That is what makes them an
; answer to PERFORMANCE.md Part 9 Set 14 rather than a debugging aid: the BIOS
; reads a whole 9-sector track in 1.92 revolutions and dsk_xfer takes 1.34 a
; SECTOR, so the question is whether the run splitter is issuing 4 calls for a
; 32-sector run or 32, and dsk_dbg_i13 against dsk_dbg_sec says which.
dsk_dbg_blk:
dsk_dbg_mag: dw 0x4444           ; +0  'DD' - a package checks this before it
                                 ;     trusts an offset it read out of a word
dsk_dbg_mnt: dw 0                ; +2  disk_mount calls - mechanism A's meter
dsk_dbg_sec: dw 0                ; +4  sectors transferred - the work asked for
dsk_dbg_i13: dw 0                ; +6  int 13h DATA calls (02h/03h, retries
                                 ;     included; not the 00h resets) -
                                 ;     mechanism C's meter. Phase 1 must drive
                                 ;     this DOWN while dsk_dbg_sec stays put;
                                 ;     if both move, the splitter is dropping
                                 ;     work. sec/i13 is SECTORS PER CALL, which
                                 ;     is the whole question
dsk_dbg_max: dw 0                ; +8  the largest AL ever passed to one call:
                                 ;     a mean can hide a splitter that manages
                                 ;     one long run and then gives up
dsk_dbg_rst: dw 0                ; +10 controller resets (the 00h after a
                                 ;     failure). A read that is quietly
                                 ;     retrying is a read that looks slow for
                                 ;     a reason no timing row can show
dsk_dbg_ent: dw dsk_dbg_raw      ; +12 near offset of the FAR entry below
dsk_dbg_tn:  dw 0                ; +14 how many trace slots are filled; a
                                 ;     reader ZEROES this to start a trace
dsk_dbg_tp:  dw dsk_dbg_trc      ; +16 ...and where they are

DSK_DBG_TRC equ 40               ; (lba, run) pairs. 40 covers a 16KB read on
                                 ; the machine that needed it - 34 calls -
                                 ; and stops rather than wrapping, because a
                                 ; wrapped trace is indistinguishable from a
                                 ; short one and this exists to be READ
dsk_dbg_trc: times DSK_DBG_TRC*2 dw 0

; -----------------------------------------------------------------------------
; dsk_dbg_raw - one raw int 13h, from the KERNEL, with sch_lock held
; FAR entry: far-call KERNEL_SEG:[dsk_dbg_blk+12]
; in:  AL = sectors, CH = cylinder, CL = sector, DH = head, DL = drive,
;      ES:BX = the buffer. AH is supplied here.
; out: AH = the BIOS status, AL = sectors moved, CF as int 13h left it
; clobbers: AX, CF (DS, SI and everything else preserved)
;
; This exists because tests/sysbench called int 13h ITSELF and hard froze the
; 5150 (docs/FIELD-NOTES.md 10). A package cannot make that call safe: a BIOS
; runs its disk handler and its IRQ6 nesting on whichever 256-byte task stack
; is current (SPEC.md 8), on top of the harness's own frames, and every other
; int 13h in this system holds sch_lock across the call so nothing switches
; underneath one. From here it is exactly as safe as dsk_xfer's own transfer,
; because it is the same two lines around the same instruction.
;
; It deliberately does NOT count itself: the counters above measure os8088's
; transfer path, and a row that prices the BIOS must not move them.
; -----------------------------------------------------------------------------
dsk_dbg_raw:
    push ds
    push si
    mov si, KERNEL_SEG
    mov ds, si
    inc byte [sch_lock]         ; pause task switching, exactly as dsk_xfer
    mov ah, 0x02                ; does - the tick still runs, the motor needs
    int 0x13                    ; it
    dec byte [sch_lock]
    pop si
    pop ds
    retf
%endif

; The listing's home. A floppy uses the .lowbss buffers below and a cap of
; DSK_NENT; a driver-backed volume uses the claim its driver donated, with a
; bigger cap (SPEC.md 22.6). ONE code path, two configurations - every reader
; goes through dsk_get_dir / dsk_get_icon, which already staged an entry into
; the kernel segment and now just take their segment from a word.
dsk_dseg:    dw LOW_SEG          ; the segment the listing lives in
dsk_doff:    dw disk_dir         ; entry 0's offset in it
dsk_ioff:    dw disk_icons       ; icon slot 0's offset in it
dsk_nmax:    dw DSK_NENT         ; how many entries fit

; The current directory (SPEC.md 19.2) - the one disk_dir lists and the one
; every dskw_* name resolves inside. 0 = the root. These live HERE, in
; .text, and not in .bss for a reason worth stating: -f bin gives .bss no
; image bytes, so it boots as whatever the RAM held, and the very first
; disk_mount reads [dsk_keepcwd] to decide whether to reset [dsk_cwd].
; Left in .bss, one garbage byte at boot sends the mount walking a garbage
; cluster chain and the volume lists two lines of noise.
dsk_fatw0:   dw 0xFFFF           ; the FAT WINDOW (SPEC.md 18.8): the first
                                 ; resident FAT sector, 0xFFFF = none
; The per-volume FAT windows (SPEC.md 18.8.1/18.8.2). ALL THREE ARE .text
; WITH REAL INITIALISERS, and that is load-bearing rather than tidy: -f bin
; zeroes no .bss, dsk_fatw_pick reads dsk_fatwc as a SEGMENT, and volumes 0
; and 1 are reached before anything could have written them. Garbage there
; aims the FAT window at whatever paragraph the bytes happen to name. It has
; never bitten because a cold machine's RAM is usually zero and only driver
; volumes had claims - both of which stop being true below.
; ADJACENT AND IN THIS ORDER: dsk_fatw_slot answers the first and reaches the
; second at +DVOL_MAX*2 and the third at +DVOL_MAX*4.
dsk_fatww:   times DVOL_MAX dw 0xFFFF   ; the FAT sector each private window
                                 ; holds, 0xFFFF = nothing resident...
dsk_fatwc:   times DVOL_MAX dw 0        ; ...the claim it lives in, 0 = none,
                                 ; so this volume shares the window below the
                                 ; stacks exactly as it always did...
dsk_fatwsig: times DVOL_MAX dw 0        ; ...and the SIGNATURE of the disk
                                 ; those bytes came off (18.8.2), 0 = none
                                 ; banked, so a fresh row can never match
dsk_sigcur:  dw 0                ; the signature of the volume mounted NOW,
                                 ; computed once per mount off the boot sector
                                 ; that mount already reads
dsk_fatd0:   dw 0xFFFF           ; the dirty range's low end, 0xFFFF = clean
                                 ; (high end: dsk_fatd1 in .bss). BOTH ARE IN
                                 ; .text and not .bss, because dsk_fatw_park
                                 ; runs at the top of the machine's very FIRST
                                 ; mount and a zeroed word there reads as
                                 ; "sector 0 resident and dirty" - which would
                                 ; flush an unloaded window over a real FAT
dsk_fatseg:  dw FAT_SEG          ; WHERE the FAT window lives (SPEC.md 18.8.1).
                                 ; The shared one below the stacks by default;
                                 ; a driver-backed volume that could claim its
                                 ; own points this at that instead, so
                                 ; alternating between two of them reloads
                                 ; nothing. In .text with a real initialiser
                                 ; because drv_boot's very first mount reads it
dsk_cwd:     dw 0                ; current directory's first cluster
dsk_keepcwd: db 0                ; 1 = this mount is a dsk_chdir: keep it
dsk_quiet:   db 0                ; 1 = this mount is a VOLUME switch, not a
                                 ; listing (SPEC.md 18.9) - here for
                                 ; dsk_keepcwd's reason, a garbage byte at
                                 ; boot would skip the boot volume's scan
dsk_lstale:  db 0                ; 1 = the global listing is not this volume's

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
; -----------------------------------------------------------------------------
; dsk_dpt_init - take ownership of the diskette parameter table (SPEC.md 18.92)
; in:  nothing;  DS = KERNEL_SEG.  Called once from kmain, before any transfer
; out: nothing (all registers preserved)
;
; int 1Eh is a FAR POINTER to an 11-byte table the BIOS re-reads on every
; floppy operation, and byte 4 of it is EOT - the last sector number the FDC
; may touch. The IBM PC and XT ROMs ship EOT = 8, from the 8-sector diskettes
; of DOS 1.x, and every DOS since has replaced the table at boot for exactly
; this reason. An OS that issues multi-sector transfers and leaves the ROM's
; table alone is reading a number that has been wrong since 1982.
;
; It is COPIED and patched rather than replaced outright: the other ten bytes
; are the step rate, head load/unload, motor timings and gap lengths of THIS
; machine's drives, and a Tandon TM100 is not a place to substitute guesses.
; Only a machine with no vector at all falls back to the values above.
; -----------------------------------------------------------------------------
dsk_dpt_init:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, dsk_dpt             ; ES:DI = our copy
    xor ax, ax
    mov ds, ax
    lds si, [0x0078]            ; DS:SI = the BIOS's table (int 1Eh's vector)
    mov ax, ds
    or ax, si
    jz .keep                    ; no vector: the built-in values stand
    mov cx, DPT_LEN
    cld
    rep movsb
.keep:
    xor ax, ax
    mov ds, ax
    pushf
    cli                         ; the vector is two words and an ISR between
    mov word [0x0078], dsk_dpt  ; them would hand the BIOS half an address
    mov word [0x007A], KERNEL_SEG
    popf
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

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

    push bx                     ; the volume's transport: a BIOS floppy falls
    mov bl, [disk_drive]        ; into the code below unchanged, and a
    call dsk_vol_row            ; driver-backed volume goes out to its driver
    jc .jbadvol                 ; with the SAME volume-relative LBA (18.7)
    mov al, [bx+DV_UNIT]        ; THE ROW'S UNIT, not the volume index
    mov [dsk_unit], al          ; (SPEC.md 52.10.3). This was [disk_drive] and
                                ; was right only because A: and B: are volumes
                                ; 0 and 1 AND drives 0 and 1; a boot partition
                                ; is volume 2 and drive 80h and ends that
    cmp byte [bx+DV_KIND], DVK_DRV
    pop bx
    je .drv
    call dsk_geom_pick          ; [dsk_base] and [dsk_cylmax] for this volume
    jmp short .sector
.jbadvol:
    pop bx
    jmp .fail

.drv:                           ; DI = LBA, SI = count, ES:BX = the buffer.
    mov dx, es                  ; DSV_BLK takes DX:BX and not ES:BX, because
    mov cx, si                  ; the buffer is in LOW_SEG, the FAT window or
    mov si, di                  ; a heap claim and NEVER in the driver's
    push bx                     ; segment or the kernel's
    mov bl, [disk_drive]
    call dsk_vol_row            ; BX = the row - and BX is also the buffer
    mov ah, [bx+DV_UNIT]        ; offset, so bank it across the lookup
    pop bx
    mov al, [dsk_op]
    sub al, 2                   ; the int 13h function -> 0 read / 1 write
    mov bp, [drv_svc+DSV_SIZE+DSV_BLK]  ; the DISK class's published copy
    call drv_blk_call
    jnc .success
    mov [dsk_ioerr], al         ; the driver reports int 13h's status byte,
    jmp .fail                   ; so 03h still means write-protected (a NEAR
                                ; jump: the batching below pushed .fail out
                                ; of short range)

.sector:
    test si, si
    jz .success                 ; all sectors read (count 0 reads nothing)

    ; --- LBA (DI) + the partition base -> CHS in CH/CL/DH, drive in DL ---
    ; The base is 0 on every floppy and on every driver-backed volume (the
    ; driver adds its own, SPEC.md 18.7), so what follows is arithmetically
    ; the transfer this always made except on a BOOT PARTITION reached through
    ; the BIOS, which is the one row that carries one (SPEC.md 52.10.3).
    mov ax, [disk_spt]
    mul word [disk_heads]       ; AX = spt*heads (<= 16,065 on a hard disk)
    mov cx, ax
    mov ax, di                  ; the volume-relative LBA...
    xor dx, dx
    add ax, [dsk_base]          ; ...made absolute
    adc dx, [dsk_base+2]
    cmp dx, cx                  ; ONE div pair and no 32/16 two-step, on
    jae .fail                   ; hd_chs's proof: the highest LBA CHS can name
                                ; is 1024*255*63 and the quotient - the
                                ; cylinder - is under 1024, so DX is far below
                                ; the divisor. A dividend that fails this is a
                                ; corrupt LBA and must fail, not wrap
    div cx                      ; AX = cylinder, DX = rem
    cmp ax, [dsk_cylmax]        ; a corrupt LBA (bad directory entry) must
    jae .fail                   ; fail, not truncate into CH and read the
                                ; wrong sector. 80 on a floppy - all three
                                ; geometries end at cylinder 79 or below - and
                                ; CHS's own 1024 on a hard disk
    mov ch, al                  ; CH = cyl low 8 bits
    mov [dsk_cylhi], ah         ; ...and bits 9:8, which ride in CL below on a
                                ; hard disk and are always 0 on a floppy
    mov ax, dx
    xor dx, dx
    div word [disk_spt]         ; AX = head, DX = sector-1

    ; --- how many sectors this ONE call may carry (SPEC.md 18.91) ---
    ; The track first, because a CHS call must not cross one - and this is
    ; the only place that knows the sector's index within it (DX). AX holds
    ; the head and CH the cylinder, so both are banked across the sum.
    push ax                     ; the head, wanted for DH below
    mov ax, [disk_spt]
    sub ax, dx                  ; sectors left in THIS track (>= 1)
    cmp ax, si
    jbe .runtrk
    mov ax, si                  ; ...and never more than were asked for
.runtrk:
    mov bp, ax
    call dsk_runcap             ; BP = min(BP, what the BUFFER allows)
%ifdef FLOPPY_ONE
    mov bp, 1                   ; FLOPPY1=1: one sector a call, the pre-18.91
                                ; transfer, for A/B-ing this on real iron
%endif
    mov [dsk_run], bp
%ifdef DISK_COUNTERS
    add [dsk_dbg_sec], bp
%endif
    pop ax                      ; the head again

    push ax                     ; ...and away again: AL is wanted for the
    mov al, [dsk_cylhi]         ; shift. Cylinder bits 9:8 ride in CL bits 7:6
    mov cl, 6                   ; (SPEC.md 52.10.3) - the standard CHS packing,
    shl al, cl                  ; inert on a floppy where the byte is 0. The
                                ; count goes through CL because an 8086 has no
                                ; `shl reg, imm` other than 1
    or al, dl                   ; ...over the sector index, still 0-based
    mov cl, al
    inc cl                      ; CL = sector (1-based) | cylinder high
    pop ax
    mov dh, al                  ; DH = head
    mov dl, [dsk_unit]          ; DL = THE ROW'S int 13h drive

    mov bp, 3                   ; attempts left at this run length
.attempt:
%ifdef DISK_COUNTERS
    inc word [dsk_dbg_i13]
    mov al, [dsk_run]           ; ...and the longest run anything managed
    xor ah, ah
    cmp ax, [dsk_dbg_max]
    jbe .dbgnomax
    mov [dsk_dbg_max], ax
.dbgnomax:
    push bx                     ; ...and the (LBA, run) of this very call, so
    mov bx, [dsk_dbg_tn]        ; a reader can see whether the SAME sectors
    cmp bx, DSK_DBG_TRC         ; are being fetched more than once - which is
    jae .dbgnotrc               ; the shape 148 sectors for a 32-sector file
    add bx, bx                  ; has, and which no total can distinguish
    add bx, bx                  ; from "reads too much"
    mov [dsk_dbg_trc+bx], di    ; DI is the LBA this pass is issuing
    mov [dsk_dbg_trc+bx+2], ax
    inc word [dsk_dbg_tn]
.dbgnotrc:
    pop bx
%endif
    mov al, [disk_spt]          ; the FDC stops a multi-sector transfer at EOT
    mov [dsk_dpt+4], al         ; and the ROM's is 8 (SPEC.md 18.92) - so this
                                ; volume's real spt goes in before every call,
                                ; because the table is one and the mounted
                                ; volume is not
    mov ax, [dsk_run]           ; AL = the run (<= 128, so AH is 0)...
    mov ah, [dsk_op]            ; ...AH = 02 read / 03 write
    int 0x13
    jnc .ok
    mov [dsk_ioerr], ah         ; keep the status for diskw.inc's mapping
    cmp ah, 0x03
    je .fail                    ; write-protected: retrying cannot help
    mov ah, 0x00                ; failed: reset the controller and retry
    mov dl, [dsk_unit]          ; the ROW's drive (SPEC.md 52.10.3): resetting
    int 0x13                    ; drive 2 because the volume index is 2 resets
                                ; a floppy that is not in this transfer
%ifdef DISK_COUNTERS
    inc word [dsk_dbg_rst]
%endif
    dec bp
    jnz .attempt
    ; Three strikes at this run length. If it WAS a run, drop to a single
    ; sector and try again (SPEC.md 18.91): one bad sector must cost its own
    ; 512 bytes, not the seven good ones beside it. The retry unit widened
    ; with the transfer, so the fallback is what keeps ageing media readable.
    cmp word [dsk_run], 1
    jbe .fail                   ; already per-sector: unrecoverable
    mov word [dsk_run], 1
    mov bp, 3
    jmp short .attempt

.ok:
    ; CF=0 IS the BIOS saying the whole request completed, and AL is not.
    ;
    ; This used to advance by AL, on the reasoning that a real BIOS reports
    ; what the FDC really moved and a multi-sector read terminated early comes
    ; back CF=0 and SHORT. That reasoning is sound and the case never
    ; occurred; what happens instead on the IBM 5150 is that a nine-sector
    ; read MOVES ALL NINE and answers AL = 1. Believing it, dsk_xfer advanced
    ; one sector and re-asked for the other eight, then seven, then six - the
    ; DATA stayed correct, because the sectors it re-read were the sectors it
    ; had already read, and CF stayed 0 and nothing retried - so the only
    ; symptom was that every sector cost its own revolution. Measured: a 16KB
    ; file is 32 sectors and cost 34 int 13h calls with a run sum of 148
    ; (PERFORMANCE.md Part 9 Set 16, docs/FIELD-NOTES.md 7). It is also why
    ; 18.91's batching measured SLOWER than one-sector-per-call on that
    ; machine: the same call count plus the run arithmetic, for nothing.
    ;
    ; SeaBIOS returns the full count, so QEMU can show none of it, and the
    ; trace of (LBA, run) pairs that 18.94 publishes is what made it visible.
    ;
    ; `make DISKAL=1` restores the old behaviour, for A/B-ing this on iron.
%ifdef DISK_TRUST_AL
    xor ah, ah
    or al, al
    jnz .moved
    mov al, 1                   ; a BIOS answering 0 with CF=0 is trusted for
                                ; one sector: the loop must always progress
.moved:
    cmp ax, [dsk_run]
    jbe .count
%endif
    mov ax, [dsk_run]           ; the whole run: CF=0 is the contract
.count:
    mov cx, ax                  ; the boot bar counts SECTORS, and a call now
.notch:                         ; carries a run of them (SPEC.md 18.91) - so
    call spl_step               ; it is stepped per sector rather than per
    call fpg_step               ; call, or the bar would under-count exactly
    loop .notch                 ; as far as the batching won. BOTH preserve
                                ; every register and the flags - CX is this
                                ; loop's counter and CF is dsk_xfer's answer
                                ; in flight - and both are a compare and a
                                ; return when their bar is not on screen:
                                ; spl_step after spl_finish (SPEC.md 15.3),
                                ; fpg_step whenever no file operation armed
                                ; it (SPEC.md 12.8). The two are never both
                                ; live, the splash owning the screen being
                                ; one of the things fpg_begin refuses on.
    add di, ax                  ; next LBA
    sub si, ax                  ; sectors still wanted
    mov cl, 9
    shl ax, cl                  ; run * 512...
    add bx, ax                  ; ...advances the destination
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
; dsk_runcap - cap a transfer run to what the BUFFER allows (SPEC.md 18.91)
; in:  BP = the run the track and the caller allow, ES:BX = the buffer
; out: BP = that, capped by the 64KB DMA page and by BX's own 16 bits; never 0
; clobbers: nothing but BP (the output) and flags
;
; Two limits, and the first is the one this optimisation INTRODUCES: a single
; 512-aligned sector cannot straddle a 64KB physical page, a run can, and the
; DMA controller answers a straddle with error 09h - the transfer-side twin of
; mem_claim_dma's rule (SPEC.md 50.3). The second is that the BIOS advances
; ES:BX itself and must not wrap it.
;
; Both are "bytes left before a 16-bit quantity wraps", which is its own
; negation - and a value that is already at the boundary negates to 0, which
; means a WHOLE window (128 sectors) and not none. That is why each has a
; zero test rather than a plain shift.
; -----------------------------------------------------------------------------
dsk_runcap:
    push ax
    push cx
    push dx
    mov ax, es                  ; the low 16 bits of the physical address:
    mov cl, 4                   ; the carry out of this is the page number,
    shl ax, cl                  ; which is exactly what we do not want
    add ax, bx
    neg ax                      ; bytes to the next 64KB page
    jz .pgfull                  ; page-aligned: the whole page is ours
    mov cl, 9
    shr ax, cl                  ; whole sectors to the page end
    jmp short .bx
.pgfull:
    mov ax, 128                 ; a full 64KB page
.bx:
    mov dx, bx                  ; the same sum for BX alone
    neg dx
    jz .bxfull
    mov cl, 9
    shr dx, cl
    jmp short .pick
.bxfull:
    mov dx, 128
.pick:
    cmp dx, ax
    jae .capped
    mov ax, dx                  ; whichever bites first
.capped:
    or ax, ax
    jnz .cmp
    mov ax, 1                   ; only reachable from a base that is not
                                ; 512-aligned, which SPEC.md 2.4 forbids -
                                ; and one sector is what that buffer got
                                ; before this optimisation existed
.cmp:
    cmp ax, bp
    jae .out                    ; the track was the tighter bound
    mov bp, ax
.out:
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; The volume table (SPEC.md 18.7)
; =============================================================================

; -----------------------------------------------------------------------------
; dsk_vol_row - a volume index's row
; in:  BL = volume index
; out: CF = 0 and BX = the row; CF = 1 = out of range or a free row
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
dsk_vol_row:
    call dsk_vol_slot
    jc .bad
    cmp byte [bx+DV_KIND], DVK_FREE
    je .bad
    clc
    ret
.bad:
    xor bx, bx
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_boot_from - adopt the volume the boot sector loaded us from
; in:  DL = the BIOS drive it read with; BX:CX = the partition base (both 0
;      from boot/boot.asm, which does not set them and does not need to)
; out: nothing; every register preserved
;
; Called from kmain before anything touches a disk (SPEC.md 52.10.3).
;
; A FLOPPY BOOT CHANGES NOTHING AT ALL: DL is 0 or 1, that is already a live
; row whose unit matches, and [dsk_bootvol] is the index - so this stores the
; byte that drv_mounted was hardcoding and returns. That is the common path
; and it stays free.
;
; A HARD DISK BOOT claims the first free row for the partition, as DVK_BIOS -
; the transport really is the BIOS, which is the entire point: rung 0 answers
; for drive 80h with no driver in the machine, so the kernel can read
; SYSTEM.CFG and load HDD.DRV off the very volume the driver would otherwise
; have been needed to reach.
;
; The label is left EMPTY so desk_zone_label derives one from the index, which
; is the same courtesy dsk_vol_add extends a driver: the drive letter is the
; kernel's to assign (SPEC.md 52.4) and this row has no more right to name
; itself than a driver's does.
;
; DV_SECS is left 0 - "unknown" - because the boot sector does not know it and
; could not tell us in the registers it has. Rule 13's replacement reads that
; word, and 0 means it does not run; what stands in its place for this volume
; is rule 16, which proves the declared FAT covers its own cluster count. The
; row is describing a volume the machine has just successfully read a kernel
; out of, which is a stronger statement than any of the bounds would make.
; -----------------------------------------------------------------------------
dsk_boot_from:
    push ax
    push bx
    push si
    test dl, 0x80               ; a floppy boot: DL is already the volume
    jnz .hard
    mov [dsk_bootvol], dl
    jmp short .out
.hard:
    mov si, bx                  ; the base is banked BEFORE BX becomes the row
                                ; walker - SI:CX for the rest of the routine

    ; --- the drive's geometry, while we still know which drive it is --------
    ; int 13h AH=08h, the same question boot/boothd.asm asks and for the same
    ; reason: every CHS read of this volume needs it, and the FIRST of them
    ; fetches the BPB, so it cannot come from there. AH=08h spends BX, CX, DX,
    ; DI and ES, so everything live crosses it on the stack.
    push cx
    push dx
    push di
    push es
    mov ah, 0x08
    int 0x13
    jc .nogeom
    and cl, 0x3F                ; sectors per track, bits 0..5
    xor ch, ch
    jcxz .nogeom                ; a zero would divide by zero in dsk_xfer
    mov [dsk_bootspt], cx
    mov al, dh                  ; heads = last head + 1
    xor ah, ah
    inc ax
    jz .nogeom                  ; DH = 0FFh is not a geometry
    mov [dsk_boothds], ax
.nogeom:
    pop es
    pop di
    pop dx
    pop cx

    mov bx, dsk_vtab + 2*DV_SIZE    ; the row walks, as dsk_vol_add's does
.scan:
    cmp bx, dsk_vtab + DVOL_MAX*DV_SIZE
    jae .out                    ; no free row cannot happen at boot, nothing
                                ; having run - but a scan that falls off its
                                ; own table must not write past the end of it
    cmp byte [bx+DV_KIND], DVK_FREE
    je .got
    add bx, DV_SIZE
    jmp short .scan
.got:
    mov [bx+DV_UNIT], dl
    mov word [bx+DV_SECS], 0    ; unknown - see the header
    mov word [bx+DV_SEG], 0     ; no donated listing claim: the .lowbss floor
    mov byte [bx+DV_FLAGS], 1   ; shown on the desktop, like any volume
    mov byte [bx+DV_LBL], 0     ; ...and named by the kernel, not by us
    mov ax, bx
    sub ax, dsk_vtab
    push cx                     ; the base's high word, and shr wants CL
    mov cl, 4                   ; DV_SIZE = 16: the index back out of the row
    shr ax, cl
    pop cx
    mov [dsk_bootvol], al
    mov byte [bx+DV_KIND], DVK_BIOS ; published LAST, dsk_vol_add's discipline:
                                    ; the row is live from this store on
    mov bl, al                  ; the base goes in the side table, which is
    xor bh, bh                  ; indexed by VOLUME and not by row
    add bx, bx
    add bx, bx
    mov [dsk_vbase+bx], si
    mov [dsk_vbase+bx+2], cx
.out:
    call dsk_bootltr            ; and the messages that NAME the system disk
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_bootltr - stamp the boot volume's drive letter into the messages
; in:  [dsk_bootvol] settled
; out: nothing (all registers preserved)
;
; Three kernel messages say which disk the system files are on and all three
; said A: whatever the machine had booted from - so an installed machine
; reported "No system disk in A:" about a driver sitting on C:, which points
; the user at a drive that is not even involved.
;
; STAMPED once rather than composed per use, because the boot volume is
; settled here and cannot change afterwards: the alternative is a buffer and
; a format call at three sites, for one byte. The table holds the address of
; the letter BYTE, and each of those is its own LABEL inside its string -
; an offset would silently drift the first time somebody reworded a message.
;
; Called from dsk_boot_from's .out, so a floppy machine runs it too and
; writes back the 'A' that was already there.
; -----------------------------------------------------------------------------
dsk_bootltr:
    push ax
    push bx
    push cx
    push si
    mov al, [dsk_bootvol]
    add al, 'A'
    mov bx, dsk_ltrtab
    mov cx, DSK_LTRN
.next:
    mov si, [bx]
    mov [si], al
    add bx, 2
    loop .next
    pop si
    pop cx
    pop bx
    pop ax
    ret

dsk_ltrtab:                     ; every message that names the system disk
    dw ui_s_nodisk_l            ; the Task Manager's notice (SPEC.md 28.3)
    dw cp_s_dsave_l             ; the Control Panel's save refusal (51.5.1)
    dw drv_e1_l                 ; DRVE_DISK, the Drivers page's status (51)
DSK_LTRN equ 3

; -----------------------------------------------------------------------------
; dsk_geom_pick - bank the current volume's partition base and cylinder bound
; in:  [disk_drive] = the volume, [dsk_unit] already set from its row
; out: [dsk_base] (32-bit) and [dsk_cylmax]; every register kept, FLAGS NOT -
;      the caller is dsk_xfer's entry sequence and .sector opens with its own
;      `test si, si`, so there is nothing in flight to preserve
;
; Called once per dsk_xfer, not once per sector: both answers are properties
; of the VOLUME and the run loop below rereads them from memory. On a floppy
; it stores a zero base and 80, which is what the transfer assumed when both
; were constants (SPEC.md 52.10.3).
;
; The cylinder bound is taken from the UNIT and not from the kind, because
; that is the thing it is a fact about: 80 is a floppy's last cylinder plus
; one, and int 13h drive numbers >= 80h are the fixed disks. A driver-backed
; volume never reaches here at all - it left through .drv.
; -----------------------------------------------------------------------------
dsk_geom_pick:
    push ax
    push bx
    mov bl, [disk_drive]
    xor bh, bh
    add bx, bx
    add bx, bx                  ; volume * 4: dsk_vbase is dwords
    mov ax, [dsk_vbase+bx]
    mov [dsk_base], ax
    mov ax, [dsk_vbase+bx+2]
    mov [dsk_base+2], ax
    mov ax, 80                  ; the floppy bound, and the corrupt-LBA net
    test byte [dsk_unit], 0x80  ; that has caught bad directory entries since
    jz .set                     ; before there was a hard disk to ask about
    mov ax, 1024                ; CHS's own ceiling: 10 bits of cylinder
.set:
    mov [dsk_cylmax], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_vol_slot - a volume index's row, live or not (module internal)
; in:  BL = volume index
; out: CF = 0 and BX = the row; CF = 1 = out of range
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
dsk_vol_slot:
    cmp bl, DVOL_MAX
    jae .bad
    mov bh, 0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1                   ; * DV_SIZE (16)
    add bx, dsk_vtab
    clc
    ret
.bad:
    xor bx, bx
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_vol_kind - what kind of volume is mounted right now?
; in:  nothing
; out: AL = DVK_*, or DVK_FREE if [disk_drive] names no live row
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
dsk_vol_kind:
    push bx
    mov bl, [disk_drive]
    call dsk_vol_row
    mov al, DVK_FREE
    jc .out
    mov al, [bx+DV_KIND]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_vol_add - register a driver-backed volume (osapi_vol_add, SPEC.md 18.7)
; in:  AL = the driver's own volume handle, CX = the volume's sector count,
;      ES:SI = a NUL label in the CALLER's segment (0 = derive one),
;      DX = a listing claim's segment (0 = none, use the .lowbss floor)
; out: CF = 0 and AL = the volume index; CF = 1 = no free row
; clobbers: AL (the output), flags
;
; The desktop zone and the letter both fall out of the index, so this is the
; whole of "mount": what follows is an ordinary disk_mount on that index.
; -----------------------------------------------------------------------------
dsk_vol_add:
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the handle for the duration
    mov bx, dsk_vtab + 2*DV_SIZE    ; the ROW walks, not an index: the index
.scan:                          ; is derivable from the pointer, and the
    cmp bx, dsk_vtab + DVOL_MAX*DV_SIZE     ; version that kept it in BL had
    jae .full                   ; to bank it across dsk_vol_slot - which it
    cmp byte [bx+DV_KIND], DVK_FREE ; did on the stack, and popped BH over
    je .got                     ; the HANDLE. A volume whose handle is
    add bx, DV_SIZE             ; garbage answers no sector the kernel asks
    jmp short .scan             ; for, and the mount fails as "not a FAT
.got:                           ; volume"
    mov [bx+DV_UNIT], ah
    mov [bx+DV_SECS], cx
    mov [bx+DV_SEG], dx         ; the donated listing claim, 0 = the floor
    mov byte [bx+DV_FLAGS], 1   ; shown on the desktop
    lea di, [bx+DV_LBL]         ; the label is INLINE, so this is the copy
    mov cx, 7                   ; and there is nothing to point at
    or si, si
    jz .noname
.copy:
    mov ah, [es:si]
    mov [di], ah
    or ah, ah
    jz .named
    inc si
    inc di
    loop .copy
.named:
    mov byte [di], 0
    jmp short .live
.noname:
    mov byte [di], 0            ; no label: desk_zone_label derives 'Disk X'
.live:
    mov byte [bx+DV_KIND], DVK_DRV      ; published LAST: the row is now live
    call desk_zmark             ; the desktop owes a zone - and a driver adds
                                ; a volume from a page click, with the gfx
                                ; lock HELD, so the repaint is deferred to
                                ; ui_task's idle pass exactly as the Control
                                ; Panel defers its own (SPEC.md 31.2). Its own
                                ; flag and not [cp_dirty]: that one means the
                                ; SCHEDULER MODE changed, which every window
                                ; quoting it has to be told about, and a drive
                                ; zone is one icon (SPEC.md 26.3).
                                ;
                                ; AFTER the flag and the kind are set, so the
                                ; mark counts the zone being added - that is
                                ; what sizes the damage rect, and the delete
                                ; side marks BEFORE clearing for the same
                                ; reason (SPEC.md 26.3)
    mov ax, bx                  ; the index, back out of the row pointer
    sub ax, dsk_vtab
    mov cl, 4                   ; / DV_SIZE
    shr ax, cl
    call dsk_fatw_claim         ; and its own FAT window, if the heap will
                                ; fund one (SPEC.md 18.8.1) - a refusal just
                                ; leaves it sharing, as every volume did
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.full:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_vol_del - unregister one driver-backed volume (SPEC.md 18.7)
; in:  AL = volume index
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dsk_vol_del:
    push ax
    push bx
    mov bl, al
    call dsk_vol_row
    jc .out
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .out                    ; a BIOS floppy is not the driver's to drop
    call desk_zmark             ; ...and owes the zone's removal likewise -
                                ; BEFORE the flag goes, so the mark counts the
                                ; zone that is about to vanish. Its pixels are
                                ; at the ordinal nothing will occupy after
                                ; this, and they are exactly what the repaint
                                ; has to reach (SPEC.md 26.3)
    mov byte [bx+DV_KIND], DVK_FREE
    mov byte [bx+DV_FLAGS], 0
    mov word [bx+DV_SEG], 0
    mov byte [bx+DV_LBL], 0
    call dsk_fatw_drop          ; AL is still the index; it also puts the
                                ; live window back if this volume owned it
    cmp al, [disk_drive]
    jne .out
    mov byte [disk_drive], 0    ; the mounted volume just went away: fall back
    mov byte [dsk_mntok], 0     ; to A: with the write gate shut, exactly as a
    mov word [disk_nfiles], 0   ; failed mount leaves things (SPEC.md 18.3)
    mov word [dsk_fatw0], 0xFFFF
    call dsk_list_floor
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_vol_drop_drv - drop every driver-backed volume (drv_release, SPEC 51.8)
; in:  nothing
; out: nothing (all registers preserved)
;
; Called BEFORE the driver's claims are freed: a mounted volume whose
; transport has been freed is a read into whatever claims that memory next.
; -----------------------------------------------------------------------------
dsk_vol_drop_drv:
    push ax
    mov al, 2
.next:
    call dsk_vol_del
    inc al
    cmp al, DVOL_MAX
    jb .next
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_list_floor - point the listing at the .lowbss buffers (module internal)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dsk_list_floor:
    mov word [dsk_dseg], LOW_SEG
    mov word [dsk_doff], disk_dir
    mov word [dsk_ioff], disk_icons
    mov word [dsk_nmax], DSK_NENT
    ret

; -----------------------------------------------------------------------------
; dsk_list_pick - choose the listing buffer for the volume being mounted
; in:  BL = volume index
; out: nothing (all registers preserved)
;
; A driver-backed volume whose driver donated a claim lists into it, with
; DSK_VENT entries; everything else uses the floor. The claim's layout is
; DSK_VENT entries followed by DSK_VENT icon slots, which is exactly the
; per-window view cache's layout too (SPEC.md 22.6) - one shape, so fmv_store
; stays a pair of block copies.
; -----------------------------------------------------------------------------
dsk_list_pick:
    push ax
    push bx
    call dsk_list_floor
    call dsk_vol_row
    jc .out
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .out
    mov ax, [bx+DV_SEG]
    or ax, ax
    jz .out                     ; no claim: the floor, and a 32-entry listing
    mov [dsk_dseg], ax
    mov word [dsk_doff], 0
    mov word [dsk_ioff], DSK_VENT * DSK_DE_SIZE
    mov word [dsk_nmax], DSK_VENT
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; The API slots a DRVC_DISK driver mounts a volume through (SPEC.md 51.8)
;
; The fence on all four is the same one drv_task uses: the caller's segment
; must be the segment of the driver whose DISK services are published. That
; is an identity test rather than an approximation, and it is what stops a
; package from registering a volume the kernel would then read through a
; transport nobody published.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_vol_fence - is the caller the published disk driver? (internal)
; in:  ES = the caller's segment (the slot's X stub, or the caller's own DS)
; out: CF = 0 allowed, CF = 1 refused
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
osapi_vol_fence:
    push bx
    mov bx, es
    or bx, bx
    jz .no
    cmp bx, [drv_fseg2]         ; the DISK class's publication slot
    jne .no
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; osapi_vol_add - slot 0x0270 (X: ES = the caller's segment)
; in:  AL = the driver's volume handle, CX = sectors, DX = a listing claim's
;      segment (0 = the floor), ES:SI = a NUL label or SI = 0
; out: CF = 0 and AL = the volume index; CF = 1 refused
; -----------------------------------------------------------------------------
osapi_vol_add:
    call osapi_vol_fence
    jc .no
    call dsk_vol_add
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; osapi_vol_del - slot 0x0278 (X).  in AL = volume index
; -----------------------------------------------------------------------------
osapi_vol_del:
    call osapi_vol_fence
    jc .out
    call dsk_vol_del
.out:
    ret

; -----------------------------------------------------------------------------
; osapi_vol_mount - slot 0x0280 (X).  in AL = volume index; out CF = 1 refused
;
; The Mount button (SPEC.md 51.8). UI-task context only, exactly like every
; other file slot: it takes [sch_lock] around int 13h and rewrites the one
; global mount snapshot.
; -----------------------------------------------------------------------------
osapi_vol_mount:
    call osapi_vol_fence
    jc .nofence
    push bx
    push dx
    mov bl, al
    call dsk_vol_row            ; the row's own existence is the check...
    jc .no
    mov dl, al                  ; ...and the INDEX is still in AL, which the
    call disk_mount             ; lookup does not touch - BL is the row's low
                                ; byte by the time it returns
    jc .no
    pop dx
    pop bx
    clc
    ret
.no:
    pop dx
    pop bx
.nofence:
    stc
    ret

; -----------------------------------------------------------------------------
; osapi_vol_paint - slot 0x0288: repaint the desktop's drive zones
;
; It takes the gfx lock ITSELF, so a driver must not call it from a page
; click that already holds one - the Control Panel's own deferred-repaint
; idiom ([cp_dirty], SPEC.md 31.2) is the shape to copy.
; -----------------------------------------------------------------------------
osapi_vol_paint:
    call gfx_lock
    call desk_zones_paint
    call gfx_unlock
    ret

; =============================================================================
; The FAT window (SPEC.md 18.8)
;
; FAT_SEG stopped meaning "the FAT" and started meaning "these DSK_FAT_SECS
; FAT sectors". Same 4,608 bytes, same segment, same alignment - what changed
; is that a 32MB FAT16 volume, whose FAT is 64 sectors, is now mountable at
; all, and that a floppy gets the DEGENERATE case: [dsk_fatwn] covers the
; whole FAT, dsk_fat_window returns on its first compare forever, and not one
; sector of I/O moved.
;
; Every entry access goes through dsk_fat_window, and it guarantees the
; sector asked for AND THE ONE AFTER IT are resident - because a FAT12 entry
; is 12 bits at 1.5 bytes and can straddle the 512-byte boundary. That is the
; case DOS special-cases with two buffers; nine sectors makes it disappear.
; =============================================================================

; -----------------------------------------------------------------------------
; dsk_fat_ofs - where a cluster's FAT entry lives (module internal)
; in:  AX = cluster (already range-checked by the caller)
; out: BX = its byte offset INSIDE THE WINDOW, DX = its absolute FAT sector
; clobbers: BX, DX (the outputs), flags
;
; The two FAT types are split here rather than sharing one byte offset, and
; that is not tidiness: a FAT16 entry's absolute byte offset is clus*2, which
; for the 65,524 clusters the format allows is 131,048 - PAST 16 BITS. The
; sector and the offset within it both fit; their product does not.
; -----------------------------------------------------------------------------
dsk_fat_ofs:
    push ax
    push cx
    cmp byte [dsk_fattype], 0
    jne .fat16
    mov bx, ax                  ; FAT12: off = clus + clus>>1, and the WHOLE
    shr bx, 1                   ; offset fits - the format's 4,084-cluster
    add bx, ax                  ; ceiling puts it under 6,127
    mov dx, bx
    mov cl, 9
    shr dx, cl                  ; DX = the sector, BX = the offset in it
    and bx, 511
    jmp short .win
.fat16:
    mov dx, ax                  ; FAT16: 256 entries a sector, so the sector
    mov cl, 8                   ; is clus>>8 and the offset (clus & 255)*2 -
    shr dx, cl                  ; neither of which can overflow, while their
    mov bx, ax                  ; product does at 65,524 clusters
    and bx, 255
    shl bx, 1
.win:
    call dsk_fat_window         ; ensure DX and DX+1 are resident
    cmp word [dsk_fatw0], 0xFFFF
    je .bad                     ; the load failed: there is no offset to give
    push dx
    sub dx, [dsk_fatw0]         ; the window-relative sector: at most
    mov cl, 9                   ; [dsk_fatwn]-1, so this shift is bounded by
    shl dx, cl                  ; the WINDOW and never by the FAT's size
    add bx, dx
    pop dx
    clc
.out:
    pop cx
    pop ax
    ret
.bad:
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; dsk_fat_window - make FAT sector DX (and DX+1) resident (SPEC.md 18.8)
; in:  DX = a FAT sector index
; out: nothing (all registers preserved). A read failure leaves the window
;      invalid, which the next access retries - and dsk_next_clus answers
;      CF=1 for it, which every walker already reads as "the chain ends here"
; -----------------------------------------------------------------------------
dsk_fat_window:
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, [dsk_fatw0]
    cmp ax, 0xFFFF
    je .load                    ; nothing resident
    cmp dx, ax
    jb .load
    mov bx, ax
    add bx, [dsk_fatwn]
    dec bx                      ; BX = the LAST resident sector, and the
    cmp dx, bx                  ; straddle needs DX+1 as well - so the last
    jb .out                     ; sector is not good enough unless it is the
    jne .load                   ; last of the FAT
    mov bx, [dsk_fatsz]
    dec bx
    cmp dx, bx
    je .out                     ; the final sector: nothing follows it
.load:
    call dskw_flush             ; anything dirty goes back before it is lost.
                                ; An eviction flush BEFORE the commit leaks
                                ; clusters and can never cross-link (18.4)
    mov ax, dx
    mov bx, [dsk_fatsz]
    sub bx, [dsk_fatwn]         ; the highest start that keeps the window
    cmp ax, bx                  ; whole - so a straddle at the end of the FAT
    jbe .have                   ; still has its second sector
    mov ax, bx
.have:
    mov word [dsk_fatw0], 0xFFFF
    push ax
    mov bx, [dsk_fatseg]
    mov es, bx
    mov cx, [dsk_fatwn]
    xor bx, bx
    add ax, [dsk_fatlba]
    call disk_read
    pop ax
    jnc .ok
    cmp byte [dsk_nfats], 2     ; FAT1 unreadable: FAT2 is the redundancy the
    jne .out                    ; format gives for free (SPEC.md 18.3)
    push ax
    add ax, [dsk_fatlba]
    add ax, [dsk_fatsz]
    mov cx, [dsk_fatwn]
    xor bx, bx
    call disk_read
    pop ax
    jc .out
.ok:
    mov [dsk_fatw0], ax
.out:
    pop es
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
%ifdef DISK_COUNTERS
    inc word [dsk_dbg_mnt]
%endif

    call dsk_fatw_park          ; the OUTGOING volume's window, banked while
                                ; [disk_drive] still says which one it is
    mov [disk_drive], dl
    mov byte [dsk_vkind], DVK_BIOS  ; what the BPB rules are checked against
    mov word [dsk_vsecs], 0         ; (SPEC.md 18.2/18.7)
    mov byte [dsk_vflop], 1
    push bx
    mov bl, dl
    call dsk_vol_row
    jc .kindok
    mov al, [bx+DV_KIND]
    mov [dsk_vkind], al
    mov ax, [bx+DV_SECS]
    mov [dsk_vsecs], ax
    ; --- IS IT A FLOPPY? which is not the same question as DV_KIND ----------
    ; Four of the BPB rules below are floppy limits - a FAT of at most
    ; DSK_FAT_SECS, the two geometry whitelists, and the total-sectors bound -
    ; and every one of them used to ask DVK_BIOS, because for as long as BIOS
    ; transport meant a floppy that was the same question.
    ;
    ; SPEC.md 52.10.3 separated them: a BOOT PARTITION is a DVK_BIOS row, so
    ; that the kernel can read it through int 13h before HDD.DRV exists, and
    ; it is a 32MB FAT16 volume whose FAT is far more than nine sectors. Asked
    ; the old way it failed rule 10 and would not mount - which is the mount
    ; refusing the disk the machine had just booted from.
    ;
    ; DV_KIND stays the TRANSPORT, because that is what dsk_xfer needs it for.
    ; This byte is the MEDIUM, and a floppy is a BIOS row whose unit is below
    ; 80h - the same test dsk_geom_pick uses for the cylinder bound, and a
    ; fact about int 13h drive numbers rather than about this OS.
    cmp al, DVK_BIOS
    jne .nofloppy
    test byte [bx+DV_UNIT], 0x80
    jz .kindok
.nofloppy:
    mov byte [dsk_vflop], 0
.kindok:
    pop bx
    ; --- the geometry for the LBA 0 read, BEFORE the BPB can supply one -----
    ; A floppy's fallback is 9/2 and always was. A BIOS-transported HARD DISK
    ; (SPEC.md 52.10.3's boot partition) needs its OWN, and this is the one
    ; read that cannot get it from the BPB - it is the read that FETCHES the
    ; BPB. Its LBA is not 0 either: a partition's sector 0 is `base` sectors
    ; in, so the wrong geometry does not merely mis-seek by a track, it asks
    ; for a CHS that translates somewhere else entirely. With 9/2 and a base
    ; of 26 the drive was asked for (1,0,9) and answered LBA 112 - inside the
    ; FAT - so the BPB never validated and the volume "was not an os8088
    ; disk". The machine still booted, because the BOOT SECTOR probes AH=08h
    ; and reads the kernel correctly; the kernel simply never mounted the
    ; volume it had booted from, which is why this showed up as a Task
    ; Manager that could not find its file and a Disk window with nothing in
    ; it rather than as a failure to start.
    mov word [disk_spt], 9
    mov word [disk_heads], 2
    push bx
    mov bl, dl
    call dsk_vol_row
    jc .geomok
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .geomok                 ; a driver volume never reaches the CHS code
    test byte [bx+DV_UNIT], 0x80
    jz .geomok                  ; a floppy: 9/2, as before
    mov ax, [dsk_bootspt]       ; the drive's own, probed once at boot
    or ax, ax
    jz .geomok                  ; never probed: leave the floppy fallback and
    mov [disk_spt], ax          ; let the mount fail honestly
    mov ax, [dsk_boothds]
    mov [disk_heads], ax
.geomok:
    pop bx
    mov word [disk_nfiles], 0   ; stays 0 unless the mount fully succeeds
    mov byte [dsk_mntok], 0     ; the write gate (SPEC.md 18.4) closes for
                                ; the whole mount: a torn mount leaves it
                                ; closed, so nothing can write a volume
                                ; whose layout was never fully validated
    mov word [dsk_rover], 2     ; allocator hint + FAT dirty range: no
    mov word [dsk_fatd0], 0xFFFF    ; window state survives a mount, and the
    mov word [dsk_fatd1], 0     ; window itself is invalid until rule 10 has
    mov word [dsk_fatw0], 0xFFFF    ; sized it (SPEC.md 18.8)
    mov word [dsk_fatwn], 1
    push bx
    mov bl, dl                  ; the listing's home follows the VOLUME: the
    call dsk_list_pick          ; .lowbss floor, or the claim its driver
    pop bx                      ; donated (SPEC.md 22.6)
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
    call dsk_bpb_sig            ; [dsk_sigcur] = which disk this is (18.8.2),
                                ; off the sector we have just read anyway

    ; --- 2. the FAT WINDOW's first load (SPEC.md 18.8; ES only, never DS) ---
    ; Not the whole FAT any more - a 32MB FAT16 volume's is 64 sectors and the
    ; window is nine. Sector 0 is read EAGERLY, with its FAT2 fallback, for
    ; one reason: "a torn mount is a failed mount" is a property worth keeping,
    ; and a lazy first fault would move the moment an unreadable disk is
    ; diagnosed to some later cluster walk. Every window after this one faults
    ; in, with the same fallback.
    call dsk_fatw_want          ; ...give a floppy one first, if the machine
                                ; can spare it (SPEC.md 18.8.2)
    call dsk_fatw_pick          ; this volume's own window, if it has one and
    jnc .fat_ok                 ; this is a quiet switch back to it (18.8.1)
    xor dx, dx
    call dsk_fat_window
    cmp word [dsk_fatw0], 0xFFFF
    jne .fat_ok
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

    ; --- 2b. a QUIET mount stops here (SPEC.md 18.9) -------------------------
    ; Everything above is the VOLUME - the BPB, the geometry, the FAT window
    ; and the current directory. Everything below is the LISTING, which a file
    ; operation never looks at: dskw_find and fcp_scan walk directory sectors
    ; themselves. So a caller that is switching volumes to read or write a
    ; file, rather than to show one, stops here and skips the scan, the sort
    ; and the per-file icon harvest.
    ;
    ; [disk_nfiles] goes to 0 rather than being left alone, because the
    ; listing buffer still holds the PREVIOUS volume's entries and a stale
    ; count is the one thing that could make a reader believe they are this
    ; volume's. An empty listing is a state the whole kernel already handles;
    ; a wrong one is not. [dsk_lstale] is the debt, and dsk_relist pays it.
    cmp byte [dsk_quiet], 0
    je .listing
    mov word [disk_nfiles], 0
    mov byte [dsk_lstale], 1
    mov byte [dsk_mntok], 1     ; the volume IS validated - writes are legal
    clc
    jmp .out
.listing:

    ; --- 3. root-dir scan -> synthesized directory (SPEC.md 19) ---
    ; One sector at a time through dsk_secbuf. DS never leaves the kernel
    ; segment while parsing: raw fields are read via es:, the staged entry is
    ; built in dsk_ent (kernel scratch) and copied out through dsk_put_dir -
    ; which knows whether this volume's listing is the .lowbss floor or a
    ; driver's claim, so nothing below has to (SPEC.md 22.6).
    mov ax, LOW_SEG
    mov es, ax
    xor bp, bp                  ; BP = accepted entry count
    cmp word [dsk_cwd], 0
    je .noup
    call dsk_synth_up           ; in a subdirectory the listing OPENS with
    xor ax, ax                  ; '..' (SPEC.md 19.5) - built before the scan
    call dsk_put_dir            ; so the scan's entries land above it and the
    mov bp, 1                   ; sort below never has to keep it in place
.noup:
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
    push ax
    mov ax, bp
    call dsk_put_dir            ; -> the listing, wherever it lives
    pop ax
    inc bp
    cmp bp, [dsk_nmax]
    jae .scan_done              ; the cap: extras are invisible
.skip:
    add si, 32
    loop .entry
    jmp .rootsec
.scan_done:
    mov es, [dsk_dseg]          ; the sort reads and writes the LISTING, so
    mov bx, [dsk_doff]          ; ES leaves the sector buffer for it
    mov cx, bp                  ; alphabetical, and BEFORE the harvest below:
    cmp word [dsk_cwd], 0       ; slot i is then filled for whatever entry
    je .sortall                 ; ended up at i, so the icons never have to be
    add bx, DSK_DE_SIZE         ; permuted alongside the entries (19.4). The
    dec cx                      ; parent link is not IN the sort - it is
.sortall:                       ; always the first entry (19.5)
    call dsk_sortdir

    ; Blank EVERY icon slot up front so none ever carries a previous mount's
    ; bytes (SPEC.md 18.3 step 4: fully rewritten every mount) - the harvest
    ; below then only fills the slots that earn an icon.
    mov di, [dsk_ioff]
    mov cx, [dsk_nmax]
    mov ax, DSK_ICO_SIZE/2
    mul cx
    mov cx, ax
    xor ax, ax
    cld
    rep stosw                   ; ES = [dsk_dseg]
    mov ax, LOW_SEG             ; ...and back to the sector buffer for the
    mov es, ax                  ; harvest's reads

    ; --- 4. icon harvest (SPEC.md 18.3 step 4; failures are NON-fatal) ---
    ; Type-1 entries carry a validated first cluster: peek the file's first
    ; sector, and if it is a v3 .o88 with the embedded-icon flag, its bytes
    ; 32..95 are the 16x16 icon body. Everything else - type 0, iconless,
    ; read failure - gets the all-zero slot (the generic-icon sentinel).
    ; disk_icons is fully rewritten every mount (SPEC.md 29.1).
    call asc_use                ; this volume's ASSOC.DAT (SPEC.md 54.7), so
                                ; the loop below can answer most of its icon
                                ; reads without doing them
    mov ax, LOW_SEG             ; asc_use reads the disk: put ES back
    mov es, ax
    xor dx, dx                  ; DX = entry index
.harvest:
    cmp dx, bp
    jae .harvested
    mov ax, dx
    call dsk_get_dir            ; SI -> dsk_ent, the synthesized entry
    cmp word [si+16], 2         ; a folder or the parent link (SPEC.md 19.5):
    jae .h_folder               ; the one icon that is not harvested off the
    cmp word [si+16], 1         ; disk but built in
    jne .h_zero
    call asc_lookup             ; cached from a previous session or written
    jc .h_read                  ; warm by the image builder? (SPEC.md 54.7)
    call asc_take               ; then this package costs NO sector read
    jmp .h_next
.h_read:
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
    cmp byte [dsk_ent+2], 3         ; format version (SPEC.md 20.2)
    jne .h_zero
    mov al, [dsk_ent+3]             ; BANK the header flags here. dsk_get_dir
    mov [assoc_hflags], al          ; below re-stages the DIRECTORY entry over
                                    ; dsk_ent, so [dsk_ent+3] stops being the
                                    ; header's flags and becomes the fourth
                                    ; character of the file's NAME
    test al, 1                      ; embedded-icon flag
    jz .h_noicon                ; ...and an ICONLESS package still gets the
    mov si, dsk_secbuf+32       ; rest of this: its header may DECLARE
    mov ax, dx                  ; extensions (SPEC.md 54.6), and hanging that
    call dsk_put_icon           ; off the icon flag skipped precisely the
.h_noicon:                      ; case the declaration ships on first
    mov ax, dx                  ; the NAME again - the header staged above
    call dsk_get_dir            ; overwrote dsk_ent's copy of it
    call assoc_note_app         ; this program is HERE, declares THIS, and
    jmp .h_next                 ; looks like THAT (SPEC.md 54.3) - no extra
                                ; I/O: the sector is the one just read

.h_folder:                      ; every folder gets the same built-in body.
    mov si, dsk_folder_ico      ; The one icon not harvested off the disk,
    mov ax, dx                  ; so it is also the one copied from .text
    call dsk_put_icon_k
    jmp .h_next

.h_zero:                        ; no icon: the slot is already blank
.h_next:
    inc dx
    jmp .harvest
.harvested:

    ; --- 4b. document icons (SPEC.md 54.3) ------------------------------
    ; A SECOND pass, and the order is the whole reason it is one: pass A is
    ; what teaches assoc_note_app where each program is and what it looks
    ; like, and the listing is sorted by name (SPEC.md 19.4), so a document
    ; can perfectly well precede its program - BEACH.BMP before PAINT.O88.
    ; Composing in pass A would draw the page bare for everything above the
    ; program in the sort. No I/O here at all: both passes read data already
    ; in hand.
    xor dx, dx
.docs:
    cmp dx, bp
    jae .done
    mov ax, dx
    call dsk_get_dir
    cmp word [si+16], 0         ; type 0 alone: a package draws its own icon
    jne .doc_next               ; and a folder has dsk_folder_ico
    call assoc_docicon          ; CF=0 and dsk_ico holds the composed body
    jc .doc_next
    mov ax, dx
    mov si, dsk_ico
    call dsk_put_icon_k
.doc_next:
    inc dx
    jmp .docs

.done:
    mov [disk_nfiles], bp       ; publish the count last
    mov byte [dsk_lstale], 0    ; ...the listing is this volume's again...
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
    ; rule 4: sectors/cluster a power of two, capped at 64 (0 would
    ; divide-fault below; 128 makes a 65,536-byte cluster, which overflows
    ; dskw_clbytes' word to 0 - a divide fault in fcp_chunkset under the gfx
    ; lock, and fcp_clspan's max() then under-spans a mixed pair so a copy
    ; skips the tail of every source cluster. The MS spec notes >32KB
    ; clusters break most tools; DOS never formats them)
    mov al, [dsk_bpb+DSK_B_SPC]
    test al, al
    jz .jfail1
    cmp al, 64
    ja .jfail1
    mov ah, al
    dec ah
    test al, ah                 ; x AND (x-1) == 0: {1,2,4,...,64}
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
    ; rule 10: FATSz16 >= 1, and on a FLOPPY <= DSK_FAT_SECS. On a
    ; DRIVER-BACKED volume there is no cap at all - the FAT is a WINDOW now
    ; (SPEC.md 18.8), and rule 16 below proves the declared FAT actually
    ; covers its own cluster count, which is the check this cap was standing
    ; in for. A 32MB FAT16 volume declares 64 sectors and mounts.
    mov ax, [dsk_bpb+DSK_B_FATSZ]
    test ax, ax
    jz .jfail1
    cmp byte [dsk_vflop], 1
    jne .r11
    cmp ax, DSK_FAT_SECS
    ja .jfail1
.r11:
    ; rules 11 and 12: the geometry. On a floppy these are the real-geometry
    ; whitelists, and their job is to stop a hostile spt*heads product from
    ; zeroing disk_read's CHS divisor and divide-faulting with sch_lock held.
    ; A driver-backed volume never reaches that divisor - its driver owns the
    ; addressing - so the bound is only that the product cannot overflow.
    mov ax, [dsk_bpb+DSK_B_SPT]
    cmp byte [dsk_vflop], 1
    jne .r11d
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
    jmp short .r12
.r11d:
    test ax, ax                 ; 1..63: the CHS sector field is six bits
    jz .jfail1
    cmp ax, 63
    ja .jfail1
.r12:
    mov ax, [dsk_bpb+DSK_B_HEADS]
    cmp byte [dsk_vflop], 1
    jne .r12d
    cmp ax, 1
    je .r13
    cmp ax, 2
    jne .jfail1
    jmp short .r13
.r12d:
    test ax, ax                 ; 1..255: the CHS head field is eight bits
    jz .jfail1
    cmp ax, 255
    ja .jfail1
.r13:
    ; rule 13: on a floppy, TotSec16 <= spt*heads*80, so every in-volume LBA
    ; stays CHS-reachable under disk_read's cyl<80 guard. On a driver-backed
    ; volume the DRIVER declared the partition's length when it registered
    ; the volume, and the question stops being "is this geometry plausible"
    ; and becomes the sharper "does this volume claim to be bigger than the
    ; partition it lives in" (SPEC.md 18.7).
    cmp byte [dsk_vflop], 1
    jne .r13d
    mov ax, [dsk_bpb+DSK_B_SPT]
    mul word [dsk_bpb+DSK_B_HEADS]  ; AX = spt*heads (<= 72), DX = 0
    mov cx, 80
    mul cx                          ; AX = spt*heads*80 (<= 5760)
    cmp [dsk_bpb+DSK_B_TOT16], ax
    ja .fail
    jmp short .r14
.r13d:
    mov ax, [dsk_vsecs]
    test ax, ax
    jz .r14                     ; the driver did not say: nothing to check
    cmp [dsk_bpb+DSK_B_TOT16], ax
    ja .fail
.r14:

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
    ; would read past the end of it. entries = CountOfClusters+2.
    ;
    ; **In SECTORS, not bytes**, and that is the FAT window's doing: a
    ; FAT16 volume with 65,000 clusters needs 130,004 FAT bytes and declares
    ; 254 FAT sectors, and BOTH `entries*2` and `FATSz16*512` are past 16
    ; bits. Neither product is needed - the comparison is a sector count
    ; against a sector count, and every term of it fits.
    inc ax                      ; AX = entries (<= 65,526: no wrap)
    cmp byte [dsk_fattype], 0
    jne .cap16
    mov dx, ax                  ; FAT12: ceil(entries*1.5) bytes
    inc dx                      ; = entries + ceil(entries/2), and entries
    shr dx, 1                   ; is <= 4,086 here so the bytes fit
    add ax, dx
    add ax, 511
    mov cl, 9
    shr ax, cl                  ; ...as whole sectors
    jmp short .capcmp
.cap16:
    mov dx, ax                  ; FAT16: 256 entries a sector, so the count
    mov cl, 8                   ; is entries>>8 rounded up - written as a
    shr ax, cl                  ; test rather than a +255, which wraps
    and dx, 255
    jz .capcmp
    inc ax
.capcmp:
    cmp ax, [dsk_fatsz]
    ja .fail

    ; the FAT window's size (SPEC.md 18.8): as much of the FAT as fits, which
    ; for every floppy geometry is ALL of it - so the window degenerates to
    ; the snapshot it replaced and no floppy pays a sector of extra I/O.
    mov ax, [dsk_fatsz]
    cmp ax, DSK_FAT_SECS
    jbe .wsz
    mov ax, DSK_FAT_SECS
.wsz:
    mov [dsk_fatwn], ax

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
    ; --- @20: size dword - lo word at 20, hi at 22 (SPEC.md 19). It was one
    ;     word clamped to 0xFFFF, and a 116KB module listed as 65535 ---
    mov dx, [es:si+DSK_R_SIZE+2]    ; DX = size high word (held for below)
    mov ax, [es:si+DSK_R_SIZE]
    mov [dsk_ent+20], ax
    mov [dsk_ent+22], dx
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
    mov word [dsk_ent+20], 0    ; folders have no meaningful size - both
    mov word [dsk_ent+22], 0    ; halves, a hostile entry can carry either
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
; dsk_synth_up - build the parent-link entry '..' (module-internal, 19.5)
; in:       [dsk_cwd] != 0 (the caller has checked it)
; out:      dsk_ent = the 32-byte entry: name '..', TYPE 3, the parent's
;           first cluster at +18, size 0. All registers preserved.
;
; Type 3 is what makes this safe to put in the listing at all. It behaves
; like a folder everywhere that navigates and everywhere that draws, but it
; is NOT one to Rename or Delete, and a name comparison against '..' would be
; a poor way to know that - a volume is allowed to hold a real file called
; '..' precisely because dsk_synth's own species filter drops the on-disk
; dot entries and never re-surfaces them (19.2). The fact belongs in the
; entry, not in a string.
;
; A '..' link that cannot be read gets cluster 0 - the root - rather than no
; entry at all. Losing the row would strand the user in a folder whose only
; way out is Root Folder, and the root is where Root Folder would take them.
; -----------------------------------------------------------------------------
dsk_synth_up:
    push ax
    push cx
    push di
    mov di, dsk_ent             ; zero first: the name's NUL padding and the
    mov cx, DSK_DE_SIZE/2       ; unused tail both fall out of it
.zero:
    mov word [di], 0
    inc di
    inc di
    loop .zero
    mov word [dsk_ent+0], '..'  ; two bytes, then the padding already there
    mov word [dsk_ent+16], 3    ; type 3 = the parent link (SPEC.md 19)
    call dsk_dotdot             ; AX = the parent's first cluster, validated
    jnc .have                   ; against dsk_maxclus by the walker itself
    xor ax, ax
.have:
    mov [dsk_ent+18], ax
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_sortdir - sort synthesized directory entries by display name (19.4)
; in:       BX = the first entry's LOW_SEG offset, CX = how many from there,
;           ES = LOW_SEG
; out:      those entries in ascending name order
; clobbers: flags
;
; BX rather than disk_dir because the parent link (SPEC.md 19.5) is not part
; of the sort: it occupies slot 0 in a subdirectory and stays there, so the
; caller hands this the entry AFTER it.
;
; Called at the END of the directory scan and BEFORE the icon harvest, and
; that order is the whole reason this touches ONE array. disk_icons[i]
; belongs to disk_dir[i], so a sort run after the harvest would have to
; permute a second 2KB array in lockstep - twice the movement and a standing
; invitation for the two to drift apart. Harvesting afterwards fills slot i
; for whatever entry ended up at i, and the question never arises.
;
; Selection sort: n <= 32, so at most 31 exchanges of 32 bytes whatever the
; input, where a bubble sort's worst case is 496. What it spends instead is
; comparisons - 496 of them at 16 bytes - and those are byte loads out of ES
; with nothing copied in or out.
;
; DS never points at LOW_SEG (SPEC.md 2.1), so there is no rep cmpsb or
; movsb anywhere here: those want DS:SI, and this segment is the one thing
; the module refuses to give them. Both operands are read and written with
; es: overrides instead.
; -----------------------------------------------------------------------------
dsk_sortdir:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp cx, 2
    jb .out                     ; 0 or 1 entries are already in order
    mov ax, cx
    mov cl, 5
    shl ax, cl                  ; count * DSK_DE_SIZE
    add ax, bx
    mov dx, ax                  ; DX = one past the last entry
    mov cx, dx
    sub cx, DSK_DE_SIZE         ; CX = the last entry: no choice to make for
                                ; it once everything below is placed
.outer:                         ; BX = the slot being filled
    cmp bx, cx
    jae .out
    mov di, bx                  ; DI = the smallest name seen so far
    mov si, bx
.inner:
    add si, DSK_DE_SIZE         ; SI = the candidate
    cmp si, dx
    jae .placed
    call dsk_ncmp
    jae .inner                  ; not smaller: keep looking
    mov di, si
    jmp short .inner
.placed:
    cmp di, bx
    je .next                    ; already in place: 32 bytes not moved
    mov si, bx
    call dsk_swapent
.next:
    add bx, DSK_DE_SIZE
    jmp short .outer
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_ncmp - compare two synthesized entries' display names (module-internal)
; in:       SI, DI = LOW_SEG offsets of two entries, ES = LOW_SEG
; out:      the flags of an unsigned compare of [SI]'s name against [DI]'s:
;           JB = SI sorts first, JA = DI does, JE = the same 16 bytes
; clobbers: AX, flags (SI, DI and CX preserved)
;
; Case-insensitive, because "alphabetical" is what the user is promised and a
; FAT short name is uppercase only BY CONVENTION - dsk_sanit folds
; unprintables and nothing else, so a volume written by some other tool with
; lowercase bytes in its short names would otherwise sort its whole lowercase
; half after 'Z'.
;
; The 16 bytes are NUL-padded (dsk_synth zeroes the entry before filling it),
; so a short name sorts before a longer one that shares its prefix with no
; length test anywhere: PAINT.O88 before PAINT2.O88, because 0 < '2'.
;
; `loop` does not touch the flags on an 8086, so falling out of the bottom
; means sixteen equal bytes AND that the last `cmp` is still what the caller
; reads.
; -----------------------------------------------------------------------------
dsk_ncmp:
    push cx
    push si
    push di
    mov cx, 16                  ; the display-name field (SPEC.md 19)
.next:
    mov al, [es:di]
    call dsk_upper
    mov ah, al                  ; AH = [DI]'s byte, folded
    mov al, [es:si]
    call dsk_upper              ; AL = [SI]'s byte, folded
    inc si
    inc di
    cmp al, ah
    jne .done
    loop .next
.done:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dsk_upper - fold one name byte to upper case (module-internal)
; in:  AL = a byte
; out: AL = it, with 'a'..'z' folded
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
dsk_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a'-'A'
.out:
    ret

; -----------------------------------------------------------------------------
; dsk_swapent - exchange two synthesized directory entries (module-internal)
; in:       SI, DI = LOW_SEG offsets, ES = LOW_SEG
; out:      the two entries exchanged
; clobbers: flags (AX, CX, SI and DI preserved)
; -----------------------------------------------------------------------------
dsk_swapent:
    push ax
    push cx
    push si
    push di
    mov cx, DSK_DE_SIZE
.next:
    mov al, [es:si]
    mov ah, [es:di]
    mov [es:si], ah
    mov [es:di], al
    inc si
    inc di
    loop .next
    pop di
    pop si
    pop cx
    pop ax
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
; dsk_fatw_park - bank the outgoing volume's window (SPEC.md 18.8.1)
; in:  [disk_drive] still names the volume being left
; out: nothing (all registers preserved)
;
; Called at the very top of disk_mount, before [disk_drive] moves. A volume
; with no window of its own has nothing to bank - its sectors are about to be
; overwritten by whatever mounts next, which is the old behaviour exactly.
;
; **A dirty window is flushed here.** Carrying dirt across a switch would mean
; dskw_flush later writing to a volume that is no longer current, and the LBAs
; are volume-relative (SPEC.md 18.7) - it would land on the wrong disk. In
; practice this never costs an I/O: every commit point already flushes, so the
; window is clean by the time a copy switches away.
; -----------------------------------------------------------------------------
dsk_fatw_park:
    push ax
    push bx
    cmp word [dsk_fatw0], 0xFFFF
    je .bank                    ; nothing resident: nothing to flush - but the
                                ; bank is still written below, because 0xFFFF
                                ; must OVERWRITE whatever the slot held. Skip
                                ; it and a window dskw_refat invalidated
                                ; (rolled-back FAT edits still in the claim's
                                ; bytes) is resurrected by the next quiet
                                ; pick under the sector number of an EARLIER
                                ; park - zombie allocations reach the disk,
                                ; and if the window slid in between, FAT
                                ; sectors are read and flushed under the
                                ; wrong LBA
    cmp word [dsk_fatd0], 0xFFFF
    je .bank
    call dskw_flush             ; CF is not propagated: a flush that fails has
                                ; already lost those bytes, and refusing the
                                ; mount would lose the volume as well
.bank:
    mov bl, [disk_drive]
    call dsk_fatw_slot          ; BX = this volume's saved-sector word
    jc .out
    mov ax, [dsk_fatw0]
    mov [bx], ax                ; unconditional: banking 0xFFFF is what makes
                                ; an invalidate stick across a switch (the
                                ; pick reads it as "owe a load", one 9-sector
                                ; reload at worst)
    mov ax, [dsk_sigcur]        ; ...and WHICH DISK those bytes came off
    mov [bx+DVOL_MAX*4], ax     ; (18.8.2). [dsk_sigcur] still describes the
                                ; OUTGOING volume here - this runs at the top
                                ; of the mount, before the new boot sector is
                                ; read - which is exactly the one we are
                                ; banking
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_fatw_pick - point the window at the incoming volume's (SPEC.md 18.8.1)
; out: CF=0 the window is already loaded and holds a valid sector; CF=1 = the
;      caller owes it a load
; clobbers: flags
;
; Only a QUIET mount (SPEC.md 18.9) may reuse a banked window. A full mount is
; a re-validation of the whole volume - the disk may have been swapped - so it
; takes the load, and "a torn mount is a failed mount" survives untouched.
; -----------------------------------------------------------------------------
dsk_fatw_pick:
    push ax
    push bx
    mov word [dsk_fatseg], FAT_SEG
    mov bl, [disk_drive]
    call dsk_fatw_slot
    jc .load
    mov ax, [bx+DVOL_MAX*2]     ; the claim; the saved sector is at [bx]
    or ax, ax
    jz .load                    ; no claim: the shared window, as before
    mov [dsk_fatseg], ax
    cmp byte [dsk_quiet], 0
    je .load
    cmp word [bx], 0xFFFF
    je .load
    mov ax, [bx+DVOL_MAX*4]     ; IS IT STILL THE SAME DISK? (SPEC.md 18.8.2)
    or ax, ax                   ; 0 = nothing banked
    jz .load
    cmp ax, [dsk_sigcur]        ; the boot sector this mount just read
    jne .load                   ; a different disk: reload, do not trust it
    mov ax, [bx]
    mov [dsk_fatw0], ax         ; still resident, and nothing can have
    pop bx                      ; overwritten it: this window is ours alone
    pop ax
    clc
    ret
.load:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_bpb_sig - which disk is this? (SPEC.md 18.8.2)
; in:  ES = LOW_SEG and dsk_secbuf holds LBA 0, just read and validated
; out: [dsk_sigcur] = a 16-bit signature, never 0
; clobbers: nothing (all registers preserved)
;
; The whole 512 bytes, rotated between adds so the sum is position-sensitive
; rather than a plain checksum. The sector is already in RAM - the mount has
; just read and validated it - so this is arithmetic, not I/O, which is the
; entire reason the revalidation is affordable at every switch.
;
; **What it can and cannot tell apart, stated plainly.** It catches a disk
; swapped for one of a different size, format, label or contents-of-sector-0.
; It CANNOT tell two os8088-built disks of the same geometry apart, because
; tools/os88disk.py pins BS_VolID (0x88000888) and every FAT timestamp to
; keep image builds reproducible - so their boot sectors are byte-identical.
; That residual case (swap one os8088 disk for another between two QUIET
; mounts, then write) is accepted deliberately; a full mount re-validates and
; is what every navigation costs, so only a background operation is exposed.
; -----------------------------------------------------------------------------
dsk_bpb_sig:
    push ax
    push cx
    push si
    xor ax, ax
    mov si, dsk_secbuf
    mov cx, 256
.sum:
    add ax, [es:si]
    rol ax, 1                   ; position-sensitive: two sectors holding the
    inc si                      ; same bytes in a different order differ
    inc si
    loop .sum
    or ax, ax
    jnz .ok
    inc ax                      ; 0 is the "nothing banked" value in
.ok:                            ; dsk_fatwsig, so a real sector must not be it
    mov [dsk_sigcur], ax
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_fatw_want - give a FLOPPY its own FAT window too (SPEC.md 18.8.2)
; in:  [disk_drive] = the volume being mounted
; out: nothing (all registers preserved)
;
; 18.8.1 gave the private window to driver-backed volumes only, on the
; reasoning that a floppy's window is its whole FAT and never moves - true,
; and beside the point: what costs is not the window MOVING, it is the nine
; sectors re-read every time the volume switches. Measured, a floppy mount is
; ~12 sectors of which the FAT is 9, and it does not vary with what is in the
; directory (SPEC.md 54.7's ASSOC.DAT already answers the icon harvest).
;
; Gated on 128KB of FREE HEAP, not on the 4.5KB the claim needs: this is a
; comfort, and a machine that is short of memory should spend what it has on
; the user's application. Retried at every mount rather than latched, so a
; machine that frees the heap later gets its window - one mem_avail walk
; (~1,000 cycles) against a floppy mount measured in seconds.
; -----------------------------------------------------------------------------
DSK_FATW_MINK equ 128            ; free heap KB below which a floppy shares

dsk_fatw_want:
    push ax
    push bx
    push cx
    push dx
    mov bl, [disk_drive]
    cmp bl, 2                   ; volumes 0 and 1 are the BIOS floppies; a
    jae .out                    ; driver volume claims at osapi_vol_add
    call dsk_fatw_slot
    jc .out
    cmp word [bx+DVOL_MAX*2], 0
    jne .out                    ; it already has one
    call mem_avail              ; AX = largest run KB, BX = TOTAL free KB -
    cmp bx, DSK_FATW_MINK       ; and BX is why the slot is read above first
    jb .out
    mov al, [disk_drive]
    call dsk_fatw_claim         ; a refusal is a normal path (SPEC.md 50) and
.out:                           ; leaves this volume sharing, as before
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_fatw_slot - a volume's saved-sector word (module internal)
; in:  BL = a volume index
; out: CF=0 and BX -> dsk_fatww[BL]; the claim is BX + DVOL_MAX*2 away and
;      the disk signature (18.8.2) BX + DVOL_MAX*4
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
dsk_fatw_slot:
    cmp bl, DVOL_MAX
    jae .bad
    xor bh, bh
    shl bx, 1
    add bx, dsk_fatww
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_fatw_claim / dsk_fatw_drop - a volume's own window (SPEC.md 18.8.1)
; in:  AL = a volume index
; out: nothing (all registers preserved)
;
; DSK_FAT_SECS sectors of heap. Per driver-backed volume at osapi_vol_add,
; and per FLOPPY at mount time when the heap can spare it (dsk_fatw_want,
; SPEC.md 18.8.2) - which is where the 128KB gate lives.
;
; A refused claim is not an error. The volume shares the window below the
; stacks and behaves exactly as it did before this existed, which is the
; SPEC.md 50 rule and the reason this is safe to want.
; -----------------------------------------------------------------------------
dsk_fatw_claim:
    push ax
    push bx
    push cx
    push dx
    mov bl, al
    call dsk_fatw_slot
    jc .out
    mov word [bx], 0xFFFF       ; nothing resident in it yet
    add bx, DVOL_MAX*2
    push bx
    mov ax, (DSK_FAT_SECS*512 + 1023) / 1024
    mov bx, MEM_K_FATW
    call mem_claim              ; out: DX = the segment; CF=1 = refused
    pop bx
    jc .none
    mov [bx], dx
    jmp short .out
.none:
    mov word [bx], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dsk_fatw_drop:
    push ax
    push bx
    push dx
    mov bl, al
    call dsk_fatw_slot
    jc .out
    mov word [bx], 0xFFFF
    add bx, DVOL_MAX*2
    mov dx, [bx]
    or dx, dx
    jz .out
    mov word [bx], 0
    cmp dx, [dsk_fatseg]        ; if the window in use was THIS one, the next
    jne .free                   ; mount must not read a freed segment
    mov word [dsk_fatseg], FAT_SEG
    mov word [dsk_fatw0], 0xFFFF
.free:
    mov bx, MEM_K_FATW
    call mem_free               ; DX = the segment, BX = the owner word
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_free_clus - count the volume's free clusters (SPEC.md 18.4)
; in:  nothing (a successful mount)
; out: CF=0, AX = free clusters; CF=1 = no mounted volume
; clobbers: AX (the output), CF
;
; The same answer dskw_dfree's loop gives and about 9x cheaper, which is what
; makes a free-space figure affordable on EVERY mount, which is what let the
; Folder menu's on-demand item go away entirely (SPEC.md 22.7). The loop it replaces called
; dsk_next_clus per cluster, and that call is ~850 clocks of push/pop, window
; revalidation and variable shifts around ~15 clocks of actual FAT read - on
; a 4.77MHz 8088 a 1.44MB floppy's 2,846 clusters is over half a second, all
; of it overhead. Here the window is validated once per SECTOR and the entries
; are read where they lie - a FAT12 pair shares one word load, a FAT16 entry
; is a word out of an array - which is ~93 clocks a cluster (modelled on
; PERFORMANCE.md Part 2's instruction floor; not yet a field number).
;
; Only the FAT12 half is gated on the WHOLE FAT being resident, which is the
; degenerate window every floppy has (SPEC.md 18.8) and is what both shipped
; geometries are; a FAT12 volume whose FAT outruns DSK_FAT_SECS falls back to
; the per-cluster walk, which is correct there and no slower than it was. The
; FAT16 half is NOT gated, because on a hard-disk partition the windowed case
; is the normal one (SPEC.md 22.7).
; -----------------------------------------------------------------------------
dsk_free_clus:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [dsk_mntok], 1
    jne .bad
    xor di, di                  ; DI = free clusters
    mov si, 2                   ; SI = cluster under test
    cmp si, [dsk_maxclus]
    ja .counted                 ; a volume with no data clusters
    mov ax, si                  ; make the window cover cluster 2, so the
    call dsk_fat_ofs            ; residency test below reads a loaded window
    jc .bad                     ; rather than the 0xFFFF of a fresh mount
    cmp byte [dsk_fattype], 0
    jne .f16

    ; --- FAT12: two entries per three bytes, one word load for both. Needs
    ;     the WHOLE FAT resident, because a pair straddles wherever it likes;
    ;     that is the degenerate window every floppy has (SPEC.md 18.8) ------
    mov ax, [dsk_fatwn]
    cmp ax, [dsk_fatsz]
    jb .slow
    cmp word [dsk_fatw0], 0     ; ...and the window actually sitting at 0
    jne .slow
    mov cx, [dsk_fatseg]
    mov es, cx
    mov bx, 3                   ; cluster 2 lives at byte 2 + 2/2
.pair:
    mov ax, [es:bx]             ; b0 | b1<<8 - the even entry's 12 bits are
    mov dx, ax                  ; the bottom of it, the odd entry's low
    and dx, 0x0FFF              ; nibble the top
    jnz .odd
    inc di
.odd:
    inc si
    cmp si, [dsk_maxclus]
    ja .counted                 ; an odd cluster count ends on the even half
    mov dl, [es:bx+2]           ; odd entry = (ax >> 12) | b2 << 4, and it is
    or dl, dl                   ; zero exactly when both halves are
    jnz .pnext
    test ah, 0xF0
    jnz .pnext
    inc di
.pnext:
    inc si
    add bx, 3
    cmp si, [dsk_maxclus]
    jbe .pair
    jmp short .counted

    ; --- FAT16: a plain word array, and NOT gated on residency - a hard-disk
    ;     partition's FAT is 64 sectors against a 9-sector window (SPEC.md
    ;     18.8/52.3), so the windowed case IS the normal one here. An entry
    ;     never straddles, so the window is validated once per FAT SECTOR and
    ;     the 256 entries inside it are read where they lie. Same I/O as the
    ;     per-cluster walk, ~9x less CPU: 16,380 clusters on a 32MB partition
    ;     is the difference between a pause and a stall (SPEC.md 22.7).
.f16:
    mov ax, si
    call dsk_fat_ofs            ; BX = offset in the window, and the sector
    jc .counted                 ; holding cluster SI is now resident
    mov cx, [dsk_fatseg]
    mov es, cx
    mov cx, si                  ; entries from here to the end of the sector
    and cx, 255
    sub cx, 256
    neg cx
    mov ax, [dsk_maxclus]       ; ...but never past the last cluster
    sub ax, si
    inc ax
    cmp cx, ax
    jbe .f16run
    mov cx, ax
.f16run:
    add si, cx                  ; bank the advance: the loop spends CX
.word:
    cmp word [es:bx], 0
    jne .wnext
    inc di
.wnext:
    inc bx
    inc bx
    loop .word
    cmp si, [dsk_maxclus]
    jbe .f16
.counted:
    mov ax, di
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.slow:
    call dsk_free_slow
    jmp short .out
.bad:
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; dsk_free_slow - the per-cluster fallback (module internal)
; in:  a successful mount
; out: CF=0, AX = free clusters
; clobbers: AX (the output), CF
;
; A windowed FAT bigger than DSK_FAT_SECS: dsk_next_clus reloads the window as
; the scan crosses it, which is the I/O this walk exists to keep honest.
; -----------------------------------------------------------------------------
dsk_free_slow:
    push si
    push di
    xor di, di
    mov si, 2
.scan:
    cmp si, [dsk_maxclus]
    ja .done
    mov ax, si
    call dsk_next_clus
    jc .next
    test ax, ax
    jnz .next
    inc di
.next:
    inc si
    jmp short .scan
.done:
    mov ax, di
    clc
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dsk_next_clus - read one FAT entry from the snapshot
; in:  AX = cluster number
; out: CF=0, AX = raw FAT entry value (FAT12: 12-bit);
;      CF=1 if the input is outside [2, dsk_maxclus]
; clobbers: AX (the output), CF
;
; The SINGLE reader of the FAT window (SPEC.md 2.1/18.8), via ES only. FAT12
; entry: off = clus + clus/2, then the word straddling that offset (an
; unaligned word load - legal on 8086; the 3-nibble problem dissolves against
; a windowed FAT because dsk_fat_ofs guarantees the sector after is resident
; too): odd cluster takes the top 12 bits, even the bottom. FAT16 entry: a
; plain word array - LIVE code on a driver-backed hard-disk volume (SPEC.md
; 18.7/52.4), where the window slides over a FAT far bigger than DSK_FAT_SECS;
; only a FLOPPY can never reach it (rule 10 turns a FAT16 floppy away).
; Rule 16 guarantees off+1 names a real FAT sector for every valid cluster.
; No EOC/bad-mark decoding lives here - dsk_read_chain's walk is size-driven
; (SPEC.md 18).
; -----------------------------------------------------------------------------
dsk_next_clus:
    push bx
    push cx
    push dx
    push es
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    call dsk_fat_ofs            ; BX = the offset INSIDE THE WINDOW, and the
    jc .bad                     ; window now holds it and the sector after
    mov cx, [dsk_fatseg]
    mov es, cx
    cmp byte [dsk_fattype], 0
    jne .fat16
    mov bx, [es:bx]             ; the straddling word - legal on 8086 at any
    test ax, 1                  ; offset, and the window guarantees both its
    jz .even                    ; bytes are here
    mov cl, 4                   ; odd cluster: top 12 bits
    shr bx, cl
    mov ax, bx
    jmp short .done
.even:
    and bx, 0x0FFF              ; even cluster: bottom 12 bits
    mov ax, bx
    jmp short .done
.fat16:
    mov ax, [es:bx]
.done:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop dx
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
; dsk_find - one entry of the CURRENT directory, by ordinal (SPEC.md 19.7.1)
; in:  CX = the ordinal to fetch, 0 to start; ES:DI = a DSK_FIND_SZ buffer;
;      AL = 1 if the caller may see hidden and system entries
; out: CF=0 and the buffer filled, CX = the ordinal to ask for next;
;      CF=1 and AX = FERR_NOENT at the end of the directory, or FERR_* on a
;      read that failed
; clobbers: AX, CX (the outputs), flags. ES:DI comes back UNCHANGED, which is
;      what makes SPEC.md 19.7.1's loop - set the buffer once, iterate on CX -
;      correct rather than a walk through the caller's segment
;
; **STATELESS BETWEEN CALLS, AND THAT IS THE WHOLE DESIGN.** The obvious
; shape is find-first / find-next over dsk_dirw_start's cursor, and it would
; be wrong here: that cursor is three module words shared with fcp_scan and
; the file manager, and EVERY file operation walks directories through
; dskw_find. The caller this exists for is an installer, whose loop is
; find -> read -> write -> find, so a cursor living in the kernel would be
; destroyed by the very operations the walk is feeding. Re-seeking from the
; ordinal costs a directory re-read per entry and cannot be corrupted by
; anything the caller does between calls.
;
; A directory here is at most a few hundred entries and each seek reads whole
; sectors without decoding them, so the quadratic term is a few extra sector
; reads over a whole listing - against a copy that is spending 238 ms a sector
; moving file data (PERFORMANCE.md).
;
; The species filter is disk_mount's (SPEC.md 19), one rule at a time: the end
; of the directory, deleted entries, the on-disk dot links, long-name
; fragments and the volume label are never reported. Hidden and system are
; reported only to a DRIVER (AL = 1, set by the API cell's fence, SPEC.md
; 19.6.1) - the same boundary that governs writing one, so a package can
; neither see a system file here nor create one there.
; -----------------------------------------------------------------------------
dsk_find:
    push bx
    push dx
    push si
    push bp
    push di                     ; DI is the caller's BUFFER and not a cursor.
                                ; The .name loop below walks it 14 bytes
                                ; forward, and SPEC.md 19.7.1's published loop
                                ; sets ES:DI once and iterates on CX - so a
                                ; returned DI+14 makes the write cursor march
                                ; through the package's own segment. Every
                                ; in-tree caller reloads DI and so is unharmed;
                                ; this slot's contract freezes at merge, and no
                                ; caller can depend on RECEIVING DI+14
    mov [dsk_fdhid], al
    mov [dsk_fdord], cx
    mov bp, cx                  ; BP counts the ordinals still to skip
    mov ax, [dsk_cwd]
    call dsk_dirw_start
.sector:
    call dsk_dirw_next          ; AX = the next directory sector's LBA
    jc .end
    mov cx, 1
    push di
    mov bx, dsk_secbuf
    push es
    mov dx, LOW_SEG             ; the buffer is in .lowbss, reached through ES
    mov es, dx
    call disk_read
    pop es
    pop di
    jc .ioerr
    mov si, dsk_secbuf
    mov cx, 16                  ; entries to a 512-byte sector
.entry:
    push cx
    push es
    mov dx, LOW_SEG
    mov es, dx
    mov al, [es:si]
    mov ah, [es:si+DSK_R_ATTR]
    pop es
    pop cx
    test al, al
    jz .end                     ; 0x00: the end of the directory, per spec
    cmp al, 0xE5
    je .skip                    ; deleted
    cmp al, '.'
    je .skip                    ; the on-disk dot links (SPEC.md 19)
    cmp al, 0x20
    je .skip
    mov al, ah
    and al, 0x3F
    cmp al, 0x0F
    je .skip                    ; a long-name fragment
    test ah, 0x08
    jnz .skip                   ; the volume label
    test ah, 0x06
    jz .live                    ; neither hidden nor system: everybody sees it
    cmp byte [dsk_fdhid], 1
    jne .skip                   ; ...and only a driver sees the rest
.live:
    or bp, bp
    jz .take
    dec bp                      ; still seeking: this one is somebody else's
.skip:
    add si, DSK_DE_SIZE         ; the on-disk stride, which is the staged
    loop .entry                 ; one's too - both are 32 bytes
    jmp short .sector

.take:
    ; dsk_synth stages the display name, the type and the cluster into the
    ; kernel's own dsk_ent (SPEC.md 19) - the dsk_get_dir idiom, so the copy
    ; out to the caller is a plain move with no raw-field decoding here.
    push di
    push es
    mov dx, LOW_SEG
    mov es, dx
    call dsk_synth              ; ES:SI = the raw entry -> dsk_ent
    mov bx, si                  ; ...and the raw entry again, for the 32-bit
    mov ax, [es:bx+DSK_R_SIZE]      ; size, which dsk_ent CLAMPS to a word
    mov dx, [es:bx+DSK_R_SIZE+2]    ; and a copy cannot work from
    mov cl, [es:bx+DSK_R_ATTR]      ; ...and the attribute, for the caller
    pop es
    pop di
    push ax
    push dx
    push cx
    mov si, dsk_ent             ; the name, 13 bytes with its NUL
    mov cx, 13
.name:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .name
    pop cx
    mov [es:di], cl             ; +13 the attribute byte
    inc di
    mov ax, [dsk_ent+16]
    mov [es:di], ax             ; +14 the type word (1 file, 2 dir, 3 parent)
    mov ax, [dsk_ent+18]
    mov [es:di+2], ax           ; +16 the first cluster
    pop dx
    pop ax
    mov [es:di+4], ax           ; +18 the size, all 32 bits of it
    mov [es:di+6], dx
    mov word [es:di+8], 0       ; +22 reserved: a later field goes here and a
                                ; caller that ignores it today still works
    mov cx, [dsk_fdord]
    inc cx                      ; the ordinal to ask for next
    pop di
    pop bp
    pop si
    pop dx
    pop bx
    clc
    ret
.ioerr:
    mov ax, FERR_IO
    jmp short .fail
.end:
    mov ax, FERR_NOENT
.fail:
    pop di
    pop bp
    pop si
    pop dx
    pop bx
    stc
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
; dsk_chdir_q - dsk_chdir without rebuilding the listing (SPEC.md 18.9)
; in:  DL = the volume index, AX = the directory's first cluster
; out: CF as dsk_chdir; [dsk_lstale] = 1
; clobbers: flags
;
; For a caller that is going to READ OR WRITE A FILE here, not show one. It
; leaves the global snapshot empty and owed - dsk_relist is the payment, and
; every path that can return to the event loop must make it.
; -----------------------------------------------------------------------------
dsk_chdir_q:
    call dsk_here_ok            ; already standing here, on media that cannot
    jnc .here                   ; have changed under us? (SPEC.md 18.9.1)
    mov byte [dsk_quiet], 1
    call dsk_chdir
    mov byte [dsk_quiet], 0
    ret
.here:
    clc                         ; nothing read, nothing published, no debt
    ret                         ; added: the listing keeps whatever state it
                                ; already had, which is what it describes

; -----------------------------------------------------------------------------
; dsk_here_ok - is a quiet chdir to (DL, AX) a no-op? (SPEC.md 18.9.1)
; in:  DL = volume, AX = the directory cluster
; out: CF=0 = we are already there AND the media cannot have changed, so the
;      mount may be skipped entirely; CF=1 = mount
; clobbers: CF only (every register preserved, including the flags' other bits
;      being irrelevant to the one caller)
;
; The compare alone is NOT the test, and that is the whole of why this is a
; routine rather than three lines inside dsk_chdir_q. "We are already on this
; volume" says nothing about whether it is the same DISK: an app writes a
; file, the user swaps the floppy, the app writes another - both writes are
; "already here" and the second one lands on the new disk with the old FAT.
; What makes it safe is the second half.
;
; A DRIVER volume (SPEC.md 18.7) is not removable, so the question does not
; arise: an ST-225 cannot be swapped between two writes.
;
; A FLOPPY is answered by the BIOS's own MOTOR TIMEOUT, which is a physical
; fact rather than a heuristic. 0040:0040 is the motor-off countdown, ticked
; down by int 08h - which our own ISR chains precisely so this keeps running
; - and the DPT's byte 2 sets it to 0x25 = 37 ticks = 2.03 seconds. So a
; non-zero count means a floppy motor is STILL SPINNING, and nobody opens a
; drive door, swaps a disk and closes it inside two seconds while the spindle
; is turning. That is exactly the batch case: consecutive reads of one
; operation are milliseconds apart and the motor never stops, while two
; operations separated by a human are always a fresh validation.
;
; It fails SAFE in the direction that matters: a BIOS that does not maintain
; the byte reads 0, which means "validate", which is what we do today.
; -----------------------------------------------------------------------------
dsk_here_ok:
    push ax
    push bx
    push es
    cmp byte [dsk_mntok], 0     ; the last mount of this volume worked...
    je .no
    cmp dl, [disk_drive]        ; ...it is this volume...
    jne .no
    cmp ax, [dsk_cwd]           ; ...and this directory
    jne .no
    mov bl, dl
    call dsk_vol_row
    jc .no
    cmp byte [bx+DV_KIND], DVK_DRV
    je .yes                     ; not removable: nothing to re-validate
    mov bl, [bx+DV_UNIT]        ; the int 13h drive, for the motor bit
    test bl, 0x80               ; ...and a FIXED disk reached through the BIOS
    jnz .yes                    ; is not removable either (SPEC.md 52.10.3's
                                ; boot partition). DV_KIND says the TRANSPORT
                                ; and only the UNIT says the medium - they
                                ; were the same question until a hard disk
                                ; became a DVK_BIOS row, and asking the kind
                                ; alone sends an ST-225 to the floppy motor
                                ; test below. It FAILS SAFE (drive 0x80 has no
                                ; bit in 0040:003F, so `cmp bl, 4` refuses and
                                ; it mounts) - the cost is correctness kept
                                ; and the whole optimisation lost on the one
                                ; volume an installed machine uses most
    cmp bl, 4
    jae .no                     ; only drives 0..3 have a bit in 0040:003F
    xor bh, bh                  ; BL was the row pointer's low half a moment
                                ; ago, so the index needs its top clearing
    mov al, [dsk_mbit+bx]       ; a TABLE, not `shl al, cl`: this preserves
                                ; every register by contract and CX is not
                                ; ours to spend (SPEC.md 18.9.1)
    mov bx, 0x0040
    mov es, bx
    cmp byte [es:0x0040], 0     ; the motor-off countdown: 0 = spun down, so
    je .no                      ; the last two seconds are unaccounted for
    test [es:0x003F], al        ; is THIS drive's motor one of the running
    jz .no                      ; ones? (a two-drive machine may be mid-copy)
.yes:
    pop es
    pop bx
    pop ax
    clc
    ret
.no:
    pop es
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_relist - make the global snapshot a real mount again (SPEC.md 18.9)
; in:  nothing
; out: nothing (all registers preserved); CF is NOT propagated, for
;      dskw_sync's reason - the work already happened
; clobbers: nothing
;
; Cheap and idempotent when nothing is owed, which is what lets the copy
; engine call it on every exit path without asking whether it switched.
; -----------------------------------------------------------------------------
dsk_relist:
    cmp byte [dsk_lstale], 0
    je .out
    mov byte [dsk_lstale], 0    ; cleared FIRST: a mount that fails published
                                ; an empty listing, which is exactly what the
                                ; flag exists to stop being wrong about, so
                                ; there is nothing left to owe
    jmp dskw_remount            ; the remount-where-we-stand half of dskw_sync
                                ; (18.4) - a tail call, and it preserves
                                ; everything.
                                ;
                                ; dskw_remount and NOT dskw_sync, which is the
                                ; whole reason those two are separate routines:
                                ; dskw_sync DEFERS when nothing on screen is
                                ; drawn from the global listing, so this pair
                                ; would clear the debt and then decline to pay
                                ; it - and this is the payment.
.out:
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
; out: CF=0 all read, and [dsk_chain_end] = the LAST cluster it touched;
;      CF=1 with AL = 1 (disk error) / 2 (chain corrupt)
; clobbers: CF; AL on failure (everything else preserved)
;
; [dsk_chain_end] is what makes this RESUMABLE, and it is a variable rather
; than a register because the loader - the caller this was written for -
; reads a whole file in one go and would only have to preserve it. A caller
; that reads a file in chunks (fcp_rdnext, SPEC.md 22.5) steps it once with
; dsk_next_clus and calls again. The last cluster and not the next one,
; because stepping costs a FAT lookup that a whole-file read never needs.
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

    xchg ax, dx                 ; the whole file's sectors ARE the widget's
    call fpg_begin              ; scale, and this is the only place that knows
    xchg ax, dx                 ; them: dsk_xfer sees one coalesced run at a
                                ; time (SPEC.md 12.8). One byte each way, and
                                ; AX is the first cluster either side of it.

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
    mov [dsk_chain_end], si     ; where a chunked caller resumes from
    clc
    call fpg_end                ; preserves CF, so it sits either side of the
                                ; clc without comment (SPEC.md 12.8)
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
    call fpg_end                ; a failed read owes the bar back exactly as
    pop bp                      ; a successful one does
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
    push dx
    push di
    call dsk_ent_ofs            ; SI = the entry's offset in [dsk_dseg]
    mov di, dsk_ent
    mov cx, DSK_DE_SIZE
    mov dx, [dsk_dseg]
    mov ax, cs
    call dsk_copy_seg
    pop di
    pop dx
    pop cx
    pop ax
    mov si, dsk_ent
    ret

; -----------------------------------------------------------------------------
; dsk_find_name - a display name's index in the mount snapshot (SPEC.md 18.3)
; in:  SI -> a NUL display name, upper case as dsk_synth writes them
; out: CF = 0 and AX = the directory index; CF = 1 not found
; clobbers: AX (the output), flags
;
; The snapshot, not a directory walk: disk_mount has already synthesized every
; entry in the current directory and dsk_get_dir stages one back into the
; kernel segment, so this is a string compare against memory we own - no
; floppy I/O at all.
;
; It answers the INDEX, because that is the currency every consumer speaks:
; ld_run_body loads by index, dsk_get_dir reads by index, and a caller that
; wants the size or the first cluster gets them by asking for the entry. The
; callers are drv_load, finding a .DRV by the name in its table row; the chip
; menu, finding SYSTEM and then TASKMGR.O88 inside it (SPEC.md 28.3); and the
; file dialog, finding MEDIA to open on (SPEC.md 38.10). The last two want a
; FOLDER, and take its first cluster out of the entry to navigate into.
; -----------------------------------------------------------------------------
dsk_find_name:
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, si                  ; DX = the name wanted (SI is the walker's)
    xor bx, bx                  ; BX = entry index
.next:
    cmp bx, [disk_nfiles]
    jae .no
    mov ax, bx
    call dsk_get_dir            ; SI -> dsk_ent, in our own segment
    mov di, dx
.cmp:
    mov al, [si]
    mov ah, [di]
    cmp al, ah
    jne .nextent
    inc si
    inc di
    or al, al
    jnz .cmp
    mov ax, bx                  ; matched to the NUL
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.nextent:
    inc bx
    jmp short .next
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_ent_ofs / dsk_ico_ofs - one listing slot's offset (module internal)
; in:       AX = entry index
; out:      SI = its offset inside [dsk_dseg]
; clobbers: SI (the output), flags
;
; A multiply and not a shift: DSK_ICO_SIZE is 64 and the index now runs to
; DSK_VENT, so index*64 is still a word - but writing it as one operation is
; what stops the two halves of the pair from disagreeing when a cap changes.
; -----------------------------------------------------------------------------
dsk_ent_ofs:
    push ax
    push cx
    push dx
    mov cx, DSK_DE_SIZE
    mul cx
    add ax, [dsk_doff]
    mov si, ax
    pop dx
    pop cx
    pop ax
    ret

dsk_ico_ofs:
    push ax
    push cx
    push dx
    mov cx, DSK_ICO_SIZE
    mul cx
    add ax, [dsk_ioff]
    mov si, ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_put_dir - dsk_ent -> one listing slot (module internal, the mount's)
; in:       AX = entry index
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
dsk_put_dir:
    push ax
    push cx
    push dx
    push si
    push di
    call dsk_ent_ofs
    mov di, si
    mov si, dsk_ent
    mov cx, DSK_DE_SIZE
    mov ax, [dsk_dseg]
    mov dx, cs
    call dsk_copy_seg
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_put_icon / dsk_put_icon_k - one 64-byte icon body -> a listing slot
; in:       AX = entry index; SI = the body, in LOW_SEG (dsk_put_icon) or in
;           the kernel segment (dsk_put_icon_k - the built-in folder body)
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
dsk_put_icon:
    push dx
    mov dx, LOW_SEG
    jmp short dsk_put_ico_body
dsk_put_icon_k:
    push dx
    mov dx, cs
dsk_put_ico_body:
    push ax
    push cx
    push si
    push di
    push si
    call dsk_ico_ofs
    mov di, si
    pop si
    mov cx, DSK_ICO_SIZE
    mov ax, [dsk_dseg]
    call dsk_copy_seg
    pop di
    pop si
    pop cx
    pop ax
    pop dx
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
    push dx
    push di
    call dsk_ico_ofs
    mov di, dsk_ico
    mov cx, DSK_ICO_SIZE
    mov dx, [dsk_dseg]
    mov ax, cs
    call dsk_copy_seg
    pop di
    pop dx
    pop cx
    pop ax
    mov si, dsk_ico
    ret

; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG: written by int 13h through ES:BX
                                ; (or rep movsw/stosw from the mount), read
                                ; only through dsk_get_* above
disk_dir:   resb DSK_NENT*DSK_DE_SIZE      ; DSK_NENT SYNTHESIZED entries,
                                ; built by disk_mount from the FAT directory
                                ; (SPEC.md 19) - never a raw on-disk image.
                                ; The FLOOR listing: a driver-backed volume
                                ; lists into its driver's claim instead, and
                                ; [dsk_dseg]/[dsk_doff] say which is in use
disk_icons: resb DSK_NENT*DSK_ICO_SIZE     ; harvested icon slots; slot i
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
dsk_fatsz:    resw 1            ; = FATSz16 - capped at DSK_FAT_SECS only on
                                ; a BIOS floppy (SPEC.md 18.2 rule 10)
dsk_fatwn:    resw 1            ; ...and how many are resident. On a floppy
                                ; this is the whole FAT and the window never
                                ; moves - the degenerate case, byte for byte
dsk_vkind:    resb 1            ; the mounted volume's DVK_* (SPEC.md 18.7):
                                ; its TRANSPORT, which is what dsk_xfer needs
dsk_vflop:    resb 1            ; ...and whether it is a FLOPPY, which is what
                                ; BPB rules 10..13 need and is no longer the
                                ; same question (SPEC.md 52.10.3): a boot
                                ; partition is a DVK_BIOS row with a 64-sector
                                ; FAT, and rule 10 caps a floppy's at 9
dsk_vsecs:    resw 1            ; ...and the sector count its driver declared,
                                ; which is rule 13's replacement
dsk_nfats:    resb 1            ; 1 or 2
dsk_rootlba:  resw 1            ; first root-dir LBA
dsk_rootsecs: resw 1            ; root-dir sector count (<= 32)
dsk_datalba:  resw 1            ; FirstDataSec
dsk_maxclus:  resw 1            ; CountOfClusters+1 = highest valid cluster
dsk_chain_end: resw 1           ; dsk_read_chain: the last cluster it touched,
                                ; so a chunked read can resume (SPEC.md 18)
dsk_cherr:    resb 1            ; dsk_read_chain: failure code across the
                                ; register-restore epilogue

; --- the directory walker's scratch (SPEC.md 19.2) --------------------------
; Safe in .bss: dsk_dirw_start writes all three before any walk reads them.
; [dsk_cwd] and [dsk_keepcwd] are NOT - see the module data block up top.
dsk_wclus:    resw 1            ; dirw: the cluster being walked, 0 = root
dsk_wsec:     resw 1            ; dirw: sector index inside it
dsk_wguard:   resw 1            ; dirw: sectors left before the walk gives up
dsk_op:       resb 1            ; int 13h function for dsk_xfer: 02h / 03h
dsk_run:      resw 1            ; sectors in the int 13h call being issued
                                ; (SPEC.md 18.91). A module word for the same
                                ; reason dsk_op is one: dsk_xfer holds
                                ; sch_lock across the whole transfer, so
                                ; nothing can be inside it twice
dsk_ioerr:    resb 1            ; status (AH) of the last FAILED transfer;
                                ; 03h = write-protected media (SPEC.md 18.4)
; --- the transfer's volume, banked once per dsk_xfer (SPEC.md 52.10.3) -------
; Module scratch on dsk_op's terms exactly: sch_lock is held across the whole
; transfer, so nothing can be inside dsk_xfer twice and one copy of each is
; enough. .bss and not .text because every one of them is WRITTEN at the top
; of the transfer that reads them.
dsk_fdhid:    resb 1            ; dsk_find: may this caller see hidden and
                                ; system entries? (SPEC.md 19.6.1's fence)
dsk_fdord:    resw 1            ; ...and the ordinal it was asked for
dsk_unit:     resb 1            ; the row's int 13h drive - 0/1 for A:/B:, 80h
                                ; for a boot partition. NOT the volume index
dsk_cylhi:    resb 1            ; cylinder bits 9:8, for CL bits 7:6
dsk_cylmax:   resw 1            ; 80 on a floppy, 1024 on a hard disk
dsk_base:     resd 1            ; the partition base added to every LBA; 0
                                ; everywhere except a boot partition

; --- write-path state (SPEC.md 18.4), all reset by disk_mount ---------------
dsk_mntok:    resb 1            ; 1 only between a fully successful mount and
                                ; the next mount attempt - the write gate
dsk_wpad:     resb 1            ; keeps the words below even-aligned
dsk_rover:    resw 1            ; next cluster the allocator examines
dsk_fatd1:    resw 1            ; the dirty range's high end; the low end is
                                ; dsk_fatd0 in .text (it must read CLEAN before
                                ; the first mount, and .bss reads as 0)

section .text
