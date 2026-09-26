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

DSK_FIND_SZ  equ 24              ; dsk_find's answer (SPEC.md 19.7.1): a
                                 ; 13-byte NUL name, the attribute, the type
                                 ; word, the first cluster, a 32-bit size and
                                 ; one reserved word

; DSK_DE_SIZE, DSK_DE_STRIDE, DSK_ICO_SIZE, DSK_NENT and DSK_ICOIX_N are in
; kernel/dskwin.inc, with the `.lowbss` pair they size. They went there because
; the pair's ADDRESS became load-bearing (SPEC.md 2.1.2) - it has to sit at the
; bottom of the rung, against the FAT window - and a reservation is placed by
; the order its file is included, so it had to move to the front. The constants
; went with it rather than being typed twice; everything else about a listing
; is still here.

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
DSK_BPB_SZ   equ 18              ; bytes staged and banked per volume: the BPB
                                 ; from DSK_B_BPS through DSK_B_HEADS+1 (even,
                                 ; for rep movsw). DSK_B_JMP is asked of the
                                 ; sector itself, beside rule 1 (SPEC.md 18.9.2)
%if DSK_BPB_SZ < DSK_B_HEADS + 2 - DSK_B_BPS
  %error "DSK_BPB_SZ no longer covers DSK_B_BPS..DSK_B_HEADS"
%endif

; --- raw on-disk root directory entry field offsets (SPEC.md 19) -------------
DSK_R_NAME   equ 0               ; name[8] (space-padded)
DSK_R_EXT    equ 8               ; ext[3] (space-padded)
DSK_R_ATTR   equ 11              ; attribute byte
DSK_R_CZM    equ 12              ; THE COMPRESSION HINT (SPEC.md 20.14,
DSK_R_CZH    equ 13              ; docs/plans/O88-COMPRESSION-PLAN.md 15): a mark
DSK_R_CZL    equ 20              ; byte, then the UNPACKED size as a high byte
DSK_CZ_MARK  equ 0x5A            ; and a low word. **THE MARK CARRIES THE
                                 ; FORMAT**: 0x5A + the LZ_* id, so 0x5A is
                                 ; LZ4 and 0x5B is LZB (SPEC.md 20.14.1). It
                                 ; had to when the two formats got different
                                 ; answers at the capacity decision, which
                                 ; happens BEFORE any data I/O; they get the
                                 ; same answer now (20.13.7), and what the
                                 ; format still buys is that Compress can
                                 ; refuse a file already stored LZB without
                                 ; reading it. THESE FOUR BYTES ARE FREE -
                                 ; dskw_commit clears all 32 and writes only
                                 ; name, attribute, the three stamps, cluster
                                 ; and size, and 19.1 already documents @20 as
                                 ; "FAT32-only per spec - ignored". The
                                 ; directory sector is read anyway, so knowing
                                 ; a file is compressed costs NO extra I/O at
                                 ; mount, per listing or per entry.
                                 ; **IT IS A CACHE**: a foreign tool may drop
                                 ; it, a missing hint reads as "not
                                 ; compressed", and trusting that would hand an
                                 ; application compressed bytes - so the read
                                 ; path checks the file's own 'CZ' header too
DSK_CZ_MAG   equ 0x5A43          ; 'CZ', as the word a `cmp` reads it back as
DSK_CZ_HDR   equ 8               ; magic, format, a reserved zero, and the
                                 ; unpacked size as a dword
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
%ifdef KERN_SMALL
DVOL_MAX     equ 4               ; FOUR ON kern_small, and it is a bound the
                                 ; machine cannot exceed rather than a trim
                                 ; that might pinch: SPEC.md 51.0 takes the
                                 ; loadable-driver mechanism out of that
                                 ; build, so a DVK_DRV row can never be
                                 ; created and every volume it will ever have
                                 ; is a BIOS floppy - of which SPEC.md 18.98
                                 ; says there are at most four, the ROM's own
                                 ; `cmp dl,4 / jae <invalid>`. So this is the
                                 ; same number as `int 11h` can report and not
                                 ; one fewer.
                                 ;
                                 ; It follows A3 and could not have preceded
                                 ; it: with drivers loadable, four partitions
                                 ; plus two floppies is six and this would
                                 ; have cost a machine sight of its own disk.
%else
DVOL_MAX     equ 8               ; the FLOPPIES the machine has, and one per
                                 ; PARTITION a disk driver can mount: four
                                 ; primaries is the whole of what a partition
                                 ; table holds (SPEC.md 52.2), and each is its
                                 ; own volume, its own drive letter and its
                                 ; own desktop icon.
                                 ;
                                 ; It was 6 - A:, B: and the four partitions -
                                 ; and SPEC.md 18.98's external pair would have
                                 ; eaten two of them, so a machine with four
                                 ; floppies could not reach all of a
                                 ; four-partition disk at once. 8 is 54 bytes
                                 ; of .text (this table, dsk_vbase and 18.8.1's
                                 ; three per-volume words) and 2 of .bss, and
                                 ; every one of them scales off this constant
                                 ; rather than being written out
%endif
%if DVOL_MAX > 8
  %error "assoc_go (assoc.inc) keeps one bit a volume in the byte [assoc_dead]"
%endif
; ...and the purge protocol's array has to be exactly as long as the volume
; table (SPEC.md 18.8.4), or mem_shed_one gives a claim away that a volume
; above the end still holds. It was written `!= 8` because DVOL_MAX was not
; yet defined where the check sat; moved BELOW the definition it can name the
; thing it is checking, which is what let it catch kern_small's 4 against
; memory.inc's 8 on the first assembly.
%if MEM_P_FATW_N < DVOL_MAX
  %error "MEM_P_FATW_N is SHORTER than DVOL_MAX (SPEC.md 18.8.4) - the purge \
protocol would name a shorter array than there are volumes, and a volume \
above the end would keep a claim mem_shed_one has already given away"
%endif
; ...and the other way round is FINE, which is what kern_small needs it to be.
; MEM_P_FATW_N is the purgeable RANK RANGE and it is mirrored in the SDK
; (apps/os88api.inc), where taskmgr reads it to label an owner word - so it is
; one number for both kernels, because a package is one binary that runs on
; both (SPEC.md 24). DVOL_MAX is how many volumes there ARE, and on kern_small
; that is four. dsk_fatwc is therefore sized from the RANGE and not from the
; count: eight words where four would do, and the eight bytes buy the
; guarantee that mem_shed_one cannot walk off the end of it.
DV_KIND      equ 0               ; db: 0 = BIOS int 13h, 1 = driver, 0xFF free
DV_UNIT      equ 1               ; db: the int 13h DL, or the driver's handle
DV_FLAGS     equ 2               ; db: bit 0 = show a desktop zone (SPEC 26)
                                 ;     bit 1 = DVF_525, a BIOS floppy whose
                                 ;     DRIVE takes 5.25" media (SPEC 26.4.1)
                                 ;     bit 2 = DVF_GUESS, the ROM would not
                                 ;     say, so the first mount decides bit 1
DVF_525      equ 2
DVF_GUESS    equ 4
DV_CLASS     equ 3               ; db: the DRVC_* whose published DSV_BLK owns
                                 ; this row's transport; 0 on a BIOS volume.
                                 ;
                                 ; It was DV_PAD - "every kind-1 row belongs
                                 ; to the ONE published DRVC_DISK driver, so
                                 ; nothing has to record WHICH driver a volume
                                 ; came from" - and that was true right up
                                 ; until a SECOND class could serve blocks
                                 ; (docs/plans/completed/NET-PLAN.md 2.1: a cable and a hard
                                 ; disk have to coexist on the machine this
                                 ; project is calibrated against, which has an
                                 ; ST-225 in it). It costs NOTHING: the byte
                                 ; was already here keeping the stride a power
                                 ; of two, so DV_SIZE is still 16 and a row is
                                 ; still reached with a shift
DV_SECS      equ 4               ; dw: sectors in the volume, 0 = unknown.
                                 ;     BPB rule 13's replacement reads it
                                 ; DV_SEG WAS HERE - a word naming the listing
                                 ; claim the row's driver donated, 0 meaning
                                 ; the `.lowbss` floor. SPEC.md 22.6 retired
                                 ; the donation, so every volume lists into
                                 ; the floor and there is nothing per-row to
                                 ; record. DV_SIZE does NOT shrink with it:
                                 ; 16 is what makes a row a shift rather than
                                 ; a multiply, so what the word bought is two
                                 ; SPARE bytes at 14..15 for whoever needs one
DV_LBL       equ 6               ; db[8]: the desktop label, NUL-terminated
                                 ;     and INLINE - a desktop label is six
                                 ;     glyphs (the 48px zone), and inline
                                 ;     means `lea si, [bx+DV_LBL]` is already
                                 ;     the DS pointer font_str wants, with no
                                 ;     staging buffer behind it
DV_SIZE      equ 16

DVK_BIOS     equ 0
DVK_DRV      equ 1
DVK_FILE     equ 2               ; a REDIRECTED volume (SPEC.md 62.9): its
                                 ; contents come from a DRVC_FILE driver
                                 ; answering questions about FILES, and it has
                                 ; no sectors at all - no BPB, no FAT, no
                                 ; directory to walk. dsk_xfer REFUSES one
                                 ; rather than falling back, because a caller
                                 ; that reaches it is a bug
DVK_FREE     equ 0xFF

; How much of a package a HEADER PEEK has to bring back (SPEC.md 62.9.2.2).
; It is not a sector and must not become one: it is every byte the harvest
; below reads out of the buffer - the SPEC.md 20.2 header at 0..31, the
; embedded icon at 32..95 and the document glyph at LD_H_GLYPH..+15 - rounded
; to the 128 that covers the last of them. The FAT arm still reads a whole
; sector, because that is what int 13h moves; the redirected arm asks for
; exactly this and refuses a short answer.
DSK_PEEK     equ 128             ; ...and loader.inc ASSERTS it against
                                 ; LD_H_GLYPH, because disk.inc is included
                                 ; first and cannot name it here

; --- and what a PACKAGE is told about a volume (SPEC.md 18.7.2) -------------
; Deliberately NOT the DVK_* values: those are the transport and are kernel
; internals, and a package asking OSAPI_VOL_KIND is nearly always asking about
; the MEDIUM. Two separate answers in one call, so neither has to be inferred
; from the other - which is the mistake this section was written to end.
VK_REMOVABLE equ 0               ; AL: a floppy
VK_FIXED     equ 1               ; AL: a hard disk, however it is reached
VT_BIOS      equ 0               ; AH: int 13h
VT_DRIVER    equ 1               ; AH: a DRVC_DISK driver (SPEC.md 52)
VT_FILE      equ 2               ; AH: a DRVC_FILE driver ANSWERING QUESTIONS
                                 ; ABOUT FILES (SPEC.md 62.9) - so there are no
                                 ; sectors behind it and OSAPI_FILE_* is the
                                 ; only way in. A package that does not know
                                 ; this value reads "not 0 and not 1", which is
                                 ; already the right conclusion for every
                                 ; question it could be asking

; .text and not .bss for the reason every other boot-reachable table is: -f bin
; zeroes nothing, and the first disk_mount runs before anything could.
dsk_vtab:
    db DVK_BIOS, 0, 0, 0         ; volume 0 = A:
    dw 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0   ; named by the kernel (SPEC.md 26.4)
    db DVK_BIOS, 1, 0, 0         ; volume 1 = B:
    dw 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0   ; ...as every other volume already was
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
dsk_cylrun:  db 0                ; may a transfer run cross a HEAD on this
                                 ; machine? (SPEC.md 18.93.1) Set at mount from
                                 ; the boot sector's canary; 0 until then, so
                                 ; the cautious answer is the one in force
                                 ; before anybody has asked
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

fdd_unit:    db 1                ; WHICH unit dsk_fdd_probe is asking about
                                 ; (SPEC.md 18.98). It was unit 1 by
                                 ; construction, in four places.
                                 ;
                                 ; HERE and not beside the probe's own
                                 ; constants, which are .ovl: the overlay is
                                 ; overwritten by the first mount, so a byte
                                 ; there is FAT data by the time anything can
                                 ; read it back - measured, it read 1 after a
                                 ; boot that had probed unit 3. The probe does
                                 ; not care (it runs before that mount) and a
                                 ; test does, which is the same argument that
                                 ; puts the fdd_dbg_* block below in .text.

; --- the floppy-presence probe's published state (SPEC.md 18.97/57.5) --------
; The int 11h equipment word is a CLAIM - on a 5150 it is the SW1 DIP switches
; read back through the 8255 - and this is what the FDC answered when the
; kernel asked it instead.
;
; UNCONDITIONAL, and not behind DISKCNT like the block below it: the whole
; value of this one is that no emulator here can be trusted about what a 765
; reports for a drive that is not there (SPEC.md 18.92 is the same lesson
; about int 1Eh, docs/FIELD-NOTES.md 5 is what it cost), so the raw status
; bytes have to be in the build the field machine is actually sent. It is also
; the one block that costs bytes rather than pointing at state the kernel
; already keeps - SPEC.md 57.3 rule 6's exception, and it exists because
; before this the kernel never asked the question at all.
;
; In .text with real initialisers, like dsk_vtab and dsk_dpt above it and for
; their reason: -f bin zeroes nothing, and a reader may well find this block on
; a machine where the probe never ran.
FDD_S_OFF    equ 0               ; never ran: one drive claimed, or FDDPROBE=0
FDD_S_TRK0   equ 1               ; ST3 said TRACK 0 with the motor off - the
                                 ; drive is there and nothing further ran
FDD_S_SEEKOK equ 2               ; ...said it after the recalibrate: the drive
                                 ; is there and was parked off track 0
FDD_S_NOTRK0 equ 3               ; the recalibrate ran and TRK0 never came up.
                                 ; THE absent drive, and the one verdict in
                                 ; this list that removes anything
FDD_S_SEEKST0 equ 5              ; ST3 never showed TRK0 and ST0 DID NOT PROVE
                                 ; ABSENCE - so the drive is kept and its TRK0
                                 ; line is not reaching ST3 (SPEC.md
                                 ; 18.97.3/18.97.4). The Packard Bell Victory
                                 ; 286's 1.2MB drive B, measured: ST3 = 21
                                 ; twice, ST0 = 21, against the 5150's absent
                                 ; drive answering ST3 = 21 twice and ST0 = 71.
                                 ; A KEEP-ONLY signal, and it is now everything
                                 ; that is not 71's exact shape rather than 21's:
                                 ; EC clear proves presence, EC set proves
                                 ; nothing (the 4865 is present and sets it -
                                 ; docs/FIELD-NOTES.md 19), and a sense that
                                 ; answered nothing proves less than either
FDD_S_NORDY  equ 4               ; the controller never took a byte, or never
                                 ; offered one - a refusal, and it KEEPS the
                                 ; drive like every other thing that goes wrong
; ...and it is ONE ROW PER UNIT, because SPEC.md 18.98 made the probe run up
; to three times. It was one set of bytes, written by every run, so on a
; machine claiming three or four drives the block described the LAST unit
; asked and said nothing at all about the others - and that is precisely the
; configuration this diagnostic exists for, an internal drive plus a 4865 on
; the 37-pin connector. Ten bytes of .text.
FDU_ST3      equ 0               ; SENSE DRIVE STATUS, motor off. bit 4 = TRACK
                                 ; 0, and bit 3 = two-side, which the field 5150
                                 ; also reads CLEAR on the drive it does not
                                 ; have (ST3 = 21)
FDU_ST3B     equ 1               ; ...and again after the recalibrate. THIS is
                                 ; what decides: the same physical line, asked
                                 ; once the head has been told to go and find it
FDU_ST0      equ 2               ; a drained SENSE INTERRUPT STATUS ST0, taken
                                 ; to leave the controller clean. DIAGNOSTIC
                                 ; ONLY - nothing branches on it (18.97.1)
FDU_STEP     equ 3               ; where it stopped - THE byte that carries, for
                                 ; the reason clk_dbg_step is (SPEC.md 37.92):
                                 ; the verdict alone cannot tell "proved
                                 ; present" from "failed to prove absent"
FDU_VRD      equ 4               ; 0 = absent (retired, or never given a row);
                                 ; 1 = kept
FDU_SIZE     equ 5

fdd_dbg_blk:
    dw DBG_TAG_FDD               ; 'FD' - the magic, and the block's own first
    dw fdd_dbg_span              ; word, so a reader can check where it landed

fdd_dbg_span:
fdd_dbg_eqp:  db 0               ; drives int 11h claimed, after the clamp
fdd_dbg_ran:  db 0               ; a BITMAP: bit n = the probe ran on unit n.
                                 ; It was a flag, and a flag cannot answer for
                                 ; three units - nor can `step != FDD_S_OFF`,
                                 ; which is the same fact only while every exit
                                 ; writes step, i.e. only until somebody adds
                                 ; one that does not
fdd_dbg_u:                       ; three FDU_SIZE rows: unit 1, unit 2, unit 3.
                                 ; Unit 0 is where the kernel came from and is
                                 ; never a question, so it has no row
    db 0xFF, 0xFF, 0xFF, FDD_S_OFF, 1
    db 0xFF, 0xFF, 0xFF, FDD_S_OFF, 1
    db 0xFF, 0xFF, 0xFF, FDD_S_OFF, 1
FDD_DBG_SPAN equ ($ - fdd_dbg_span)
%if FDD_DBG_SPAN != 2 + 3*FDU_SIZE
  %error "disk: SPEC.md 57.5 block - the published span is not 17 bytes"
%endif

%ifdef DISK_COUNTERS
; The measurement instruments of docs/plans/completed/DISK-PERF-PLAN.md 2, compiled in only by
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
dsk_dbg_tp:  dw dsk_dbg_trc      ; +16 ...and where they are - an offset in
                                 ;     LOW_SEG, not in this segment
dsk_dbg_tsg: dw LOW_SEG          ; +18 ...and WHICH segment, because the ring
                                 ;     is the one part of this block that is
                                 ;     not here (SPEC.md 18.94.4)

DSK_DBG_TRC equ 400              ; (lba, run|drive|op) pairs. 40 covered the
                                 ; 16KB read this was written for - 34 calls -
                                 ; and a whole INSTALL is 154, so it is 400
                                 ; now: the trace is what an offline cache
                                 ; simulation is run against (§18.94.3), and a
                                 ; trace that stops halfway answers the wrong
                                 ; question. It stops rather than wrapping,
                                 ; because a wrapped trace is
                                 ; indistinguishable from a short one and this
                                 ; exists to be READ. 1,600 bytes, and only in
                                 ; the DISKCNT build
; ...and the ring itself lives in LOW_SEG, not here. It is 1,600 bytes of
; ZEROS, and in .text they were 1,600 bytes of the kernel's own 64KB segment
; AND of the image the boot sector loads - for a buffer that no shipped kernel
; has at all. .text+.bss was 65,632 of KERN_CODE_MAX with it, which is a
; DISKCNT build that would not assemble while the shipped one had 1,572 bytes
; spare: the guard cannot say that the overflowing bytes are an instrument's,
; it sees one number (SPEC.md 18.94.4). Reached through SS, which is LOW_SEG.
section .lowbss
dsk_dbg_trc: resw DSK_DBG_TRC*2
section .text

; WHERE THE SECTORS WENT, which dsk_dbg_sec's total cannot say. The progress
; widget (SPEC.md 12.8) reports the DATA PHASE of one file operation and
; nothing else - deliberately, because that is the only part whose size is
; known when it starts - so a user watching it sees a bar that stalls for
; whole seconds with no account of what the machine is doing in between.
; These six are that account.
;
; They need NO CALLER TAGGING, which is what makes them cheap enough to be
; worth having: the mounted volume's own layout says which REGION a sector is
; in, and [fpg_total] says whether the bar was counting it. So dsk_dbg_pdat is
; exactly what the widget shows and the other five are exactly what it does
; not, with no second opinion about the boundary.
;
; NOT in the published block above, deliberately. That one's offsets are an
; ABI - tests/sysbench reads it by offset - so this goes after it and is
; reached by NAME through tools/os88sym.py instead.
dsk_dbg_ph:
dsk_dbg_pbpb: dw 0               ; +0  below the FAT: the boot sector / BPB
dsk_dbg_pfr:  dw 0               ; +2  FAT region, read (the window)
dsk_dbg_pfw:  dw 0               ; +4  ...and written - a flush is BOTH copies
dsk_dbg_prt:  dw 0               ; +6  the root directory
dsk_dbg_pdat: dw 0               ; +8  data area, widget ARMED: the payload
dsk_dbg_pmet: dw 0               ; +10 data area, widget NOT armed - a
                                 ;     subdirectory walk, or an icon harvest

; ...and the same six as int 13h CALLS rather than sectors, which is the
; currency closer to TIME. A sector inside a run costs about what the media
; can stream; a call costs a seek and a rotation whatever it carries, and the
; 5150 measured a whole 9-sector track in ONE call at 384 ms against 10.02
; revolutions for the same nine sectors in nine (PERFORMANCE.md Set 14). So
; sectors-per-call PER PHASE is the thing to look at: the payload coalesces
; into runs and the overhead phases are scattered single sectors, which is
; exactly why a bar that is 76% of the SECTORS is nothing like 76% of the
; wait.
dsk_dbg_phc:  times 6 dw 0
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


; -----------------------------------------------------------------------------
; dsk_dbg_class - attribute [dsk_run] sectors at LBA DI to a phase (internal)
; in:  DI = the volume-relative LBA, [dsk_run] = sectors, [dsk_op], [fpg_total]
; out: nothing - every register AND the flags preserved
;
; Called from inside dsk_xfer's run loop, where the flags are live and AX is
; about to be popped back into: it is a measurement and must be invisible.
; -----------------------------------------------------------------------------
dsk_dbg_class:
    pushf
    push ax
    push bx
    push dx
    mov dx, [dsk_run]
    mov ax, di
    xor bx, bx                  ; +0: below the FAT
    cmp ax, [dsk_fatlba]
    jb .add
    cmp ax, [dsk_rootlba]
    jb .fat
    cmp ax, [dsk_datalba]
    jb .root
    mov bx, 8                   ; the data area, and the widget decides which
    cmp word [fpg_total], 0     ; half of the answer this sector is - the
                                ; SCALE and not [fpg_on], which since SPEC.md
                                ; 12.8.3 says the icon is up rather than that
                                ; the bar is counting this sector
    jne .add
    mov bx, 10
    jmp short .add
.fat:
    mov bx, 2
    cmp byte [dsk_op], 0x03
    jne .add
    mov bx, 4
    jmp short .add
.root:
    mov bx, 6
.add:
    add [bx+dsk_dbg_ph], dx
    inc word [bx+dsk_dbg_phc]   ; ...and this run is ONE int 13h call
    pop dx
    pop bx
    pop ax
    popf
    ret
section .text                   ; ...but NOT this one. dsk_dbg_raw is a FAR
                                ; ENTRY a package calls, and SPEC.md 18.94
                                ; publishes its NEAR OFFSET at dsk_dbg_ent
                                ; +12 with KERNEL_SEG implied - so it has to
                                ; stay in the segment that contract names
                                ; (SPEC.md 2.6 rule 3, from the other side:
                                ; the pointer is read by a HOST tool and a
                                ; test package, neither of which can be
                                ; taught a second segment by a thunk)


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
; runs its disk handler and its IRQ6 nesting on whichever 384-byte task stack
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
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


; -----------------------------------------------------------------------------
; THE BATCH BRACKET (SPEC.md 18.9.3)
;
; §18.9.2 lets a FIXED disk skip re-reading its boot sector, because an ST-225
; cannot be swapped between two writes. A floppy can, and no predicate the
; kernel can evaluate answers it: §18.9.1's motor timeout is physical proof
; for one quiet switch and nothing more - copying A to B the spindle stops
; between files, so a motor-gated batch would revalidate on nearly every
; switch and buy almost nothing.
;
; So the assertion is the CALLER'S: "I am in the middle of a batched operation
; with the user interface frozen, so the disk is the same disk."
;
; **AND ANY UNLOCKING OF THE USER INTERFACE ENDS IT.** That is the whole
; safety argument and it is one instruction: `gfx_unlock` zeroes the counter.
; The bracket is therefore not something a caller can leave open - not by
; forgetting `dsk_batch_end`, not by returning early, not by failing - because
; every path out of a locked run of kernel code goes through the unlock that
; put the cursor back. What the user CAN do between two of our reads is bounded
; by what they can do while their machine is frozen, which is nothing.
;
; The installer is the shape this was specified against: it stops to ask for a
; disk change, and that ask unlocks the interface and so ends the batch - the
; click that answers begins another. When the apps disk is found instead of
; asked for, both stages run under the same lock and so under the same batch.
;
; **`dsk_bpbok` carries 2 for "banked inside a batch".** A floppy's evidence
; expires with the batch that took it, a fixed disk's does not, and the
; distinction has to be in the bank rather than in the reader - so the
; outermost begin clears every 2 and leaves every 1. No new array, and no
; second opinion about which volumes are removable.
; -----------------------------------------------------------------------------
dsk_batch: db 0                  ; nesting depth, 0 = no batch. In .text with a
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle

                                 ; real initialiser: -f bin zeroes nothing and
                                 ; gfx_unlock writes it before any mount reads

dsk_batch_begin_x:
    push ax
    cmp byte [dsk_batch], 0
    jne .nest                   ; nested: the outer batch's banks stand
    call dsk_bpb_flopzap        ; a NEW batch is a new assertion, and the last
                                ; one's evidence expired when the UI unlocked
.nest:
    mov al, [dsk_batch]
    cmp al, 0xFF
    je .out                     ; saturate rather than wrap into 0, which would
    inc byte [dsk_batch]        ; read as "no batch" and be the safe answer for
.out:                           ; the wrong reason
    pop ax
    ret

dsk_batch_end_x:
    cmp byte [dsk_batch], 0
    je .out                     ; gfx_unlock may have ended it already, which
    dec byte [dsk_batch]        ; is not an error and is the common case
.out:
    retf

; dsk_bpb_flopzap - forget every bank that was taken inside a batch
; preserves every register and the flags
dsk_bpb_flopzap:
    pushf
    push bx
    push cx
    mov bx, dsk_bpbok
    mov cx, DVOL_MAX
.row:
    cmp byte [bx], 2
    jne .next
    mov byte [bx], 0
.next:
    inc bx
    loop .row
    pop cx
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; dsk_bpb_bank_zap - forget every banked BPB (SPEC.md 18.9.2)
; out: nothing; every register preserved
;
; Called wherever the volume TABLE changes and wherever the disk answers an
; error. A banked head is a claim that a specific volume still has a specific
; boot sector, and both of those events break it: a partition can be formatted
; under us between an unmount and a mount, and an I/O error is exactly the
; shape "this is not the disk you think it is" arrives in.
; -----------------------------------------------------------------------------
dsk_bpb_bank_zap:
    pushf                       ; AX and DF included: this is called at the TOP
    push ax                     ; of dsk_vol_add/del, where AL is an argument
    push cx
    push di
    push es
    push ds
    pop es
    mov di, dsk_bpbok
    mov cx, DVOL_MAX
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; dsk_bpb_bank_get - restore [disk_drive]'s banked BPB, if it has one
; out: CF = 0 and dsk_bpb + [dsk_sigcur] filled with NO disk read;
;      CF = 1 = read the boot sector the ordinary way
; clobbers: flags
; -----------------------------------------------------------------------------
dsk_bpb_bank_get:
    cmp byte [dsk_vflop], 1
    jne .ok                     ; a floppy is the one that can be swapped, so
    cmp byte [dsk_batch], 0     ; it needs the CALLER's assertion that it has
    je .no                      ; not been (SPEC.md 18.9.3)
.ok:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bl, [disk_drive]
    xor bh, bh
    cmp bx, DVOL_MAX
    jae .nopop
    cmp byte [bx+dsk_bpbok], 0  ; 1 permanent or 2 batch-scoped; a 2 that
    je .nopop                   ; outlived its batch was cleared at the next
                                ; begin, so reaching one here means this batch
                                ; took it
    mov ax, bx
    add bx, bx
    mov si, [bx+dsk_bpbsg]      ; the signature, banked with the head
    mov [dsk_sigcur], si
    mov cl, DSK_BPB_SZ
    mul cl                      ; index * DSK_BPB_SZ (AX = AL*CL; DX untouched)
    mov si, ax
    add si, dsk_bpbv
    mov di, dsk_bpbh
    push ds
    pop es
    mov cx, DSK_BPB_SZ / 2
    cld
    rep movsw                   ; the staged head
    mov byte [dsk_bpbpre], 1    ; ...so dsk_bpb_check skips rules 1-2 and the
    pop es                      ; staging, and only those
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.nopop:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_bpb_bank_put - remember [disk_drive]'s validated BPB, if it is fixed
; out: nothing; every register AND the flags preserved
; -----------------------------------------------------------------------------
dsk_bpb_bank_put:
    pushf                       ; FIRST - the flags are part of the contract,
                                ; and the common exit (a floppy outside a
                                ; batch) is a compare away from clobbering
                                ; the CF a caller may be carrying
    cmp byte [dsk_vflop], 1
    jne .keep                   ; a floppy is banked only inside a batch, and
    cmp byte [dsk_batch], 0     ; only for as long as it runs (SPEC.md 18.9.3)
    jne .keep
    popf
    ret
.keep:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bl, [disk_drive]
    xor bh, bh
    cmp bx, DVOL_MAX
    jae .done
    mov ax, bx
    add bx, bx
    mov si, [dsk_sigcur]
    mov [bx+dsk_bpbsg], si
    shr bx, 1
    mov byte [bx+dsk_bpbok], 1  ; 1 = permanent...
    cmp byte [dsk_vflop], 1
    jne .fixed
    mov byte [bx+dsk_bpbok], 2  ; ...2 = only for as long as this batch runs
.fixed:
    mov cl, DSK_BPB_SZ
    mul cl                      ; index * DSK_BPB_SZ
    mov di, ax
    add di, dsk_bpbv
    mov si, dsk_bpbh
    push ds
    pop es
    mov cx, DSK_BPB_SZ / 2
    cld
    rep movsw
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    popf
.out:
    ret
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


; The listing's home, and there is ONE (SPEC.md 22.6). Every volume on the
; machine lists into the .lowbss buffers below with a cap of DSK_NENT. A
; DVK_DRV volume used to list into a claim its driver donated instead, with a
; bigger cap - one code path, two configurations - and these three words are
; what made that one code path possible.
;
; **THEY ARE THE DESTINATION, AND THE DESTINATION IS AN ARGUMENT**
; (docs/plans/LISTING-HOME-PLAN.md 13). `dsk_list_pick` and `dsk_list_floor`
; were deleted with the donation and for a while nothing wrote these at all;
; what writes them now is `dsk_dest_x`, and the caller of a LOUD mount is who
; calls it. A Disk window hands over its own `FS_VSEG` claim, the Standard
; File dialog its own, and a caller with nowhere to put a listing hands over
; nothing - at which point the mount is quiet, which is a state the whole
; kernel already handles.
;
; They are kept as words rather than folded into immediates because every
; reader is smaller this way: `mov es, [dsk_dseg]` is four bytes where
; `mov ax, LOW_SEG` + `mov es, ax` is five and wants a scratch register. They
; are in .text and not .bss for the reason the paragraph below gives.
dsk_dseg:    dw 0                ; the segment the listing lives in, and ZERO
                                ; IS THE DEFAULT: a listing has no home of
                                ; its own, so a mount that nobody gave a
                                ; store to has nowhere to put one and is
                                ; quiet. It was LOW_SEG:disk_dir, the floor
                                ; listing, and `.lowbss` is 1,600 bytes
                                ; lighter for its going
dsk_doff:    dw 0                ; entry 0's offset in it
dsk_ioff:    dw 0                ; **AND `dsk_ioff` IS BACK**, for a different
                                ; job than the one SPEC.md 25.9 retired. That
                                ; one was the base of a per-listing ARRAY OF
                                ; BODIES; this is the base of the one
                                ; REFERENCE BYTE an entry keeps, and it exists
                                ; because the index is no longer at a fixed
                                ; `.lowbss` address - it follows the entries
                                ; into whatever store the caller supplied.
                                ; DERIVED by dsk_dest_x rather than passed, so
                                ; a store has one shape and not two
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
dsk_fatwc:   times MEM_P_FATW_N dw 0    ; ...the claim it lives in, 0 = none,
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
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


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
%ifndef KD_BUILD                    ; **THE BOOT OVERLAY IS NOT IN kern_dos**
                                    ; (SPEC.md 96.43.2): there is no kmain to
                                    ; run it, the DPT arrives in the launch
                                    ; block (96.40.2) and the volume the
                                    ; program came off is named rather than
                                    ; enumerated - so nothing in that root
                                    ; calls one of these and they are 762
                                    ; bytes of a build whose every byte is a
                                    ; byte off the DOS program
section .ovlw                  ; THE BOOT OVERLAY (SPEC.md 2.5.2,
                                ; docs/plans/LAST-DROP-BYTES.md row 12): kmain runs
                                ; this once, before the first mount, and
                                ; nothing re-installs int 1Eh afterwards -
                                ; dsk_xfer READS the table, it does not
                                ; rebuild it. Zero outbound calls
dsk_dpt_init_x:
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
%ifndef DPT_ROM                 ; DPTROM=1 LEAVES int 1Eh THE ROM'S (18.92.1):
                                ; an AT-class BIOS swaps parameter tables per
                                ; MEDIA, and owning the vector for ever stops
                                ; it - docs/FIELD-NOTES.md 32 candidate 1. The
                                ; copy above still happens, so `dsk_dpt` holds
                                ; the machine's own bytes and dsk_xfer's EOT
                                ; write lands somewhere harmless
    xor ax, ax
    mov ds, ax
    pushf
    cli                         ; the vector is two words and an ISR between
    mov word [0x0078], dsk_dpt  ; them would hand the BIOS half an address
    mov word [0x007A], KERNEL_SEG
    popf
%endif
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    retf

%endif                              ; KD_BUILD
section .cold                   ; ...and back to resident cold code

; -----------------------------------------------------------------------------
; dsk_bytes_secs_x - a byte count as whole 512-byte sectors, rounded UP
; in:  AX = bytes (the +511 must not carry out - every caller's bound below)
; out: AX = DX = sectors, which is the shape dsk_read_chain_x's callers want
; clobbers: CL, exactly as the four open-coded copies did
;
; `.cold` and near-called, which is the whole reason it can exist at all: the
; sites are in three files that cannot reach a `.text` helper with a near call
; (SPEC.md 2.6). The DX copy is free here and dead at the one site that does
; not want it.
; -----------------------------------------------------------------------------
dsk_bytes_secs_x:
    add ax, 511
    mov cl, 9
    shr ax, cl
    mov dx, ax
    ret

; -----------------------------------------------------------------------------
; dsk_rd1_x / dsk_wr1_x - ONE sector, to or from dsk_secbuf (SPEC.md 18.94.1)
; in:  AX = LBA; ES = LOW_SEG (dsk_secbuf is .lowbss, SPEC.md 2.1)
; out: exactly disk_read_x's / disk_write_x's - CF, and [dsk_ioerr] on failure
; clobbers: BX and CX, which every site was loading anyway
;
; "read one sector into the scratch" was spelled out at fourteen `.cold` sites:
; three instructions of marshalling in front of a call that costs ~400 ms
; (PERFORMANCE.md), so the call+ret this adds is 11 us against one disk
; revolution. dsk_rd1_x FALLS THROUGH into disk_read_x and pays no jump at all;
; dsk_wr1_x cannot (disk_read_x is between) and pays a short one.
; -----------------------------------------------------------------------------
dsk_wr1_x:
    mov cx, 1
    mov bx, dsk_secbuf
    jmp short disk_write_x
dsk_rd1_x:
    mov cx, 1
    mov bx, dsk_secbuf
                                ; ...and falls through
disk_read_x:
    mov byte [dsk_op], 0x02     ; AH=02 read (mov leaves the flags alone)
    jmp short dsk_xfer
disk_write_x:
    mov byte [dsk_op], 0x03     ; AH=03 write - the ONLY difference
    call dsk_rah_wr             ; ...and it drops §19.2.3's directory window if
                                ; this write lands in it. Preserves every
                                ; register AND the flags, which dsk_xfer's
                                ; "clobbers nothing but CF" requires
dsk_xfer:
    call kentc_bp               ; KENT: push ax..bp







%ifndef KD_BUILD                ; (kern_dos's fpg_busy is a stub: nothing to arm)
    push cx                     ; **A STOPPED FLOPPY MOTOR IS WARM ALREADY**
    push dx                     ; (SPEC.md 12.8.3.1). The first transfer of a
    mov dl, [disk_drive]        ; mount is the one-sector boot read, and it is
    call dsk_media_ok           ; the one that carries the SPIN-UP - an AT
    jnc .warm                   ; BIOS waits DPT byte 10 for it, one second on
    mov cx, FPG_WARM            ; a 286 - so FPG_WARM's sector count put the
.warm:                          ; widget and the busy pointer up only AFTER the
    pop dx                      ; second the machine sat dark. dsk_media_ok's
                                ; CF=1 is exactly "this floppy's motor is not
                                ; turning" (a fixed disk and a driver volume
                                ; answer CF=0), so it is asked here as it is
                                ; asked for the media question; CX goes back
                                ; below, fpg_busy preserving it
%endif
    call KERNEL_SEG:fpg_busy    ; **EVERY SECTOR IN THE MACHINE PASSES HERE,
                                ; SO THIS IS WHERE THE PROGRESS WIDGET IS PUT
                                ; UP** (SPEC.md 12.8.3). CX is what this call
                                ; is about to move, and fpg_busy accumulates
                                ; it against FPG_WARM: a mount, a directory
                                ; walk, an icon harvest and a FAT flush all
                                ; freeze the machine for as long as their
                                ; sectors take and none of them has a byte
                                ; count to arm SPEC.md 12.8's bar with, so
                                ; until this they froze it with nothing on
                                ; screen at all. No operation has to remember
                                ; to ask and none can forget - dskw_sync's
                                ; argument (22.8) at the other end of the same
                                ; problem.
                                ;
                                ; ABOVE the pin and the lock, deliberately: it
                                ; draws the first time it decides to, and
                                ; that draw must not land inside the window
                                ; where ES:BX is pinned and switching is off.
                                ; Every register and the flags are preserved
%ifndef KD_BUILD
    pop cx                      ; the transfer's own count again
%endif

    inc byte [sch_lock]         ; pause task switching across all int 13h
%ifdef OS88_COMPACT             ; ...and kern_small pins nothing, because
                                ; nothing there MOVES (SPEC.md 66.0): the word
                                ; and its one reader, mem_in_xfer, are the
                                ; compactor's and go with it. The three
                                ; instructions below are one stack-balanced
                                ; group and are gated as one
    push word [mem_pinseg]      ; NESTED, not owned: clip_put pins its source
                                ; through the same word across a claim that
                                ; can yield, and a transfer on another task
                                ; that zeroed it on the way out would unpin
                                ; that source for the compaction that follows
    push es
    pop word [mem_pinseg]       ; ...AND PIN WHATEVER CLAIM THE DESTINATION IS
                                ; IN (SPEC.md 66.3 rule 5). A transfer holds
                                ; ES:BX across a call that itself CLAIMS -
                                ; SPEC.md 18.95's sector cache, MEM_P_DIRW,
                                ; taken lazily inside disk_read - so a movable
                                ; block being read INTO could be compacted out
                                ; from under the kernel's own walking pointer,
                                ; which no relocation proc can repair because
                                ; the stale copy is on this stack.
                                ;
                                ; It fails SAFE by construction: the only
                                ; thing a wrong value here can do is refuse to
                                ; move a block, so the worst outcome is a
                                ; compaction that achieves less. That is why
                                ; the guard lives here rather than in an
                                ; author rule - the rule would be correct,
                                ; unenforceable, and paid for in silent
                                ; corruption the one time somebody missed it
%endif

    mov si, cx                  ; SI = sectors remaining
    mov di, ax                  ; DI = current LBA

    push bx                     ; the volume's transport: a BIOS floppy falls
    mov bl, [disk_drive]        ; into the code below unchanged, and a
    call dsk_vol_row_x            ; driver-backed volume goes out to its driver
    jc .jbadvol                 ; with the SAME volume-relative LBA (18.7)
    mov al, [bx+DV_UNIT]        ; THE ROW'S UNIT, not the volume index
    mov [dsk_unit], al          ; (SPEC.md 52.10.3). This was [disk_drive] and
                                ; was right only because A: and B: are volumes
                                ; 0 and 1 AND drives 0 and 1; a boot partition
                                ; is volume 2 and drive 80h and ends that
    mov al, [bx+DV_KIND]        ; AL is free: the unit went to [dsk_unit] above
    pop bx
    cmp al, DVK_DRV
    je .drv
    cmp al, DVK_FILE            ; A REDIRECTED VOLUME HAS NO SECTORS AT ALL
    je .jfilevol                ; (SPEC.md 62.9). This REFUSES rather than
                                ; falling back, because a caller that reaches
                                ; dsk_xfer on one is a bug - and the fallback
                                ; is the worst available answer: DV_UNIT on
                                ; such a row is the DRIVER'S OWN HANDLE, so
                                ; the LBA would go to whatever int 13h drive
                                ; that number happens to name
    call dsk_geom_pick          ; [dsk_base] and [dsk_cylmax] for this volume
    jmp short .sector
.jbadvol:
    pop bx
.jfilevol:
    jmp .fail

.drv:                           ; DI = LBA, SI = count, ES:BX = the buffer.
    mov dx, es                  ; DSV_BLK takes DX:BX and not ES:BX, because
    mov cx, si                  ; the buffer is in LOW_SEG, the FAT window or
    mov si, di                  ; a heap claim and NEVER in the driver's
    push bx                     ; segment or the kernel's
    mov bl, [disk_drive]
    call dsk_vol_row_x            ; BX = the row - and BX is also the buffer
    mov ah, [bx+DV_UNIT]        ; offset, so bank it across the lookup
    mov al, [bx+DV_CLASS]       ; ...and so is the CLASS, which has to be read
    mov [drv_blkcls], al        ; HERE, while BX is still the row
    pop bx
    mov al, [dsk_op]
    sub al, 2                   ; the int 13h function -> 0 read / 1 write
    push di                     ; THE ROW SAYS WHICH CLASS (SPEC.md 18.7). This
    push ax                     ; was `drv_svc + DSV_SIZE`, the DISK class's
    mov al, [drv_blkcls]        ; entry hard-coded - right while one class
    call drv_cls_svc_x            ; served blocks and silently wrong the moment
    mov bp, [di+DSV_BLK]        ; two did: a network volume's sectors would
    pop ax                      ; have been handed to the hard-disk driver
    pop di
    CURXFER_ON                  ; **AND THE DRIVER'S TRANSFER TOO** (SPEC.md
                                ; 7.4.1.1). Not the ROM, so the "CPU parked on
                                ; IRQ6" half of 15.3.8 does not apply and the
                                ; draw costs real time here - but the SCREEN
                                ; argument is the whole of what the flag
                                ; claims, and it is unchanged: a DRVC_DISK
                                ; block verb moves sectors and does not draw,
                                ; and [sch_lock] is up across this call, so no
                                ; other task can be inside a primitive either.
                                ; Without it a hard-disk install - the longest
                                ; freeze on the machine - keeps a dead pointer
                                ; for its whole run, because a DVK_DRV volume
                                ; leaves this routine before the int 13h loop
                                ; below ever runs
    call drv_blk_call_x
    CURXFER_OFF                 ; ...and CF is the driver's answer, which `mov`
    jnc .success                ; leaves alone
    mov [dsk_ioerr], al         ; the driver reports int 13h's status byte,
.jfail:                         ; ...AND the two range refusals in .sector, at
    jmp .fail                   ; two bytes each rather than the five NASM
                                ; spells an out-of-reach `jcc` as.  The jump
                                ; itself was already here (so 03h still means
                                ; write-protected) and is NEAR because the
                                ; batching below pushed .fail out of short
                                ; range

.sector:
    test si, si
    jz .success                 ; all sectors read (count 0 reads nothing)

    ; --- §18.95: can the cache serve this, and may it try? -------------------
    ; Reads only (a write must reach the platter), BIOS volumes only (a driver
    ; splits its own runs and has no revolution to save), and never inside a
    ; fill, which re-enters this routine to do the reading.
    cmp word [dsk_rah_seg], 0
    je .nocache
    cmp byte [dsk_rah_busy], 0
    jne .nocache
    cmp byte [dsk_op], 0x02
    jne .nocache
    cmp byte [dsk_vkind], DVK_DRV
    je .nocache
    call dsk_rah_serve          ; CF=0 with AX = sectors copied out of it, the
    jc .nocache                 ; slot filled first if a fill would have paid
                                ; (SPEC.md 18.95.1). THREE CALLS AND A TEST
                                ; STOOD HERE - take, len, the surplus compare,
                                ; fill, take AGAIN - and the second take
                                ; re-scanned the whole table for the slot the
                                ; fill had just written. One routine holds the
                                ; slot across the fill instead, so a miss that
                                ; fills costs ONE scan rather than two
    jmp .count                  ; the same advance and THE SAME NOTCHES .count
                                ; makes, which is the whole of SPEC.md
                                ; 12.8.2: a sector the cache served is a
                                ; sector the operation is DONE with, and a bar
                                ; that counts only what reached the drive
                                ; stops short and vanishes there. This was a
                                ; second copy of that block, notching before
                                ; the DI/SI advance instead of after - and the
                                ; order is immaterial, both steppers
                                ; preserving every register and the flags and
                                ; neither reading DI or SI. CX is dead at
                                ; .sector, so the copy's own push/pop pair
                                ; goes with it
.nocache:

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
    jae .jfail                  ; hd_chs's proof: the highest LBA CHS can name
                                ; is 1024*255*63 and the quotient - the
                                ; cylinder - is under 1024, so DX is far below
                                ; the divisor. A dividend that fails this is a
                                ; corrupt LBA and must fail, not wrap
    div cx                      ; AX = cylinder, DX = rem
    cmp ax, [dsk_cylmax]        ; a corrupt LBA (bad directory entry) must
    jae .jfail                  ; fail, not truncate into CH and read the
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
%ifndef TRACK_RUN
    cmp byte [dsk_cylrun], 0    ; ...but ONLY on a machine whose FDC was seen
    je .runtrk_only             ; to do it (SPEC.md 18.93.1). The boot sector
                                ; settled that with a canary before we ran, so
                                ; there is no probe to pay for here
    ; ...to the end of the CYLINDER (SPEC.md 18.91.1): the FDC's multi-track
    ; bit carries a run onto the other head at EOT, so the bound a CHS call
    ; must not cross is the cylinder and not the track. That is
    ; (heads - head) * spt - (sector - 1).
    ;
    ; FLOPPIES ONLY, and the test is the one dsk_geom_pick already uses. The
    ; mechanism is the 765's (SPEC.md 18.92); a BIOS hard-disk handler is
    ; different code with its own rules about crossing a head, and a wrong
    ; guess there is SILENT - the wrong sectors, CF = 0, full count.
    ;
    ; `make TRACKRUN=1` puts it back at the track, in both loops together.
    ;
    ; **READS ONLY, AND THAT IS MEASURED RATHER THAN CAUTIOUS** (SPEC.md
    ; 18.91.3). A WRITE whose run crosses the head fails with status 0x80 -
    ; a timeout - and dsk_xfer then shortens the run and resets the
    ; controller, so it does not corrupt anything; it just grinds. The
    ; correlation is exact, from the trace of a 360KB clone's write pass:
    ;
    ;   LBA  7 run 11  c0/h0/s8  crosses  FAILED, retried at run 1
    ;   LBA  8 run 10  c0/h0/s9  crosses  FAILED x3, retried at run 1
    ;   LBA  9 run  9  c0/h1/s1  stays    OK
    ;
    ; ...against a read pass over the same platter streaming 18 a call the
    ; whole way. Whether the ROM simply does not set the multi-track bit on
    ; WRITE DATA or the drive will not accept it, the answer here is the
    ; same, and it costs the change NOTHING: every figure in 18.91.1 is a
    ; BOOT, and a boot is all reads.
    ;
    ; It was invisible because a write path is only reached in anger by the
    ; disk cloner - SPEC.md 18.99, the one thing in the system that writes
    ; hundreds of sectors in long runs - and the verification that landed the
    ; cylinder bound was KERNEL.SYS and TRACKER.O88, both READS.
    cmp byte [dsk_op], 0x02
    jne .runtrk_only
    test byte [dsk_unit], 0x80
    jnz .runtrk_only
    push dx                     ; the sector index: mul is about to take DX
    mov bp, [disk_heads]
    sub bp, ax                  ; BP = heads left on this cylinder (>= 1: the
                                ; head came out of a div by spt on a remainder
                                ; below spt*heads, so it is under heads)
    mov ax, [disk_spt]
    mul bp                      ; AX = this track's start to the cylinder's end
    pop dx                      ; (DX = 0 - 255*63 is all CHS can name)
    sub ax, dx                  ; ...less what is already behind the head
    jmp short .runcmp
.runtrk_only:
%endif
    mov ax, [disk_spt]
    sub ax, dx                  ; sectors left in THIS track (>= 1)
%ifndef TRACK_RUN
.runcmp:
%endif
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
    call dsk_dbg_class          ; ...and WHERE they went (SPEC.md 18.94.2)
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
    mov [ss:dsk_dbg_trc+bx], di ; DI is the LBA this pass is issuing - and
                                ; SS, because the ring is in LOW_SEG
    mov ah, [disk_drive]        ; ...and the run, which is at most 128 - so AL
    cmp byte [dsk_op], 0x03     ; holds it and AH is free for the VOLUME and,
    jne .dbgrd                  ; in its top bit, for WRITE. A simulation of a
    or ah, 0x80                 ; cache has to tell a read from a write and one
.dbgrd:                         ; volume from another
    mov [ss:dsk_dbg_trc+bx+2], ax
    xor ah, ah                  ; ...and AX goes back to the plain run
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
    CURXFER_ON                   ; **THE ARROW MAY MOVE FROM HERE TO THE `iret`**
                                ; (SPEC.md 7.4). The bracket is the INSTRUCTION
                                ; and not the operation, which is the whole
                                ; safety argument: nothing between .attempt and
                                ; here draws, so no primitive of ours can be in
                                ; flight while the flag is set - and the CPU in
                                ; there is parked on IRQ6 waiting for DMA
                                ; (SPEC.md 15.3.8), so what the ISR spends is a
                                ; gap the machine was going to spend idle
    int 0x13
    CURXFER_OFF                  ; `mov` leaves the flags alone, so int 13h's CF
    jnc .ok                     ; still reaches this branch
    mov [dsk_ioerr], ah         ; keep the status for diskw.inc's mapping
    cmp ah, 0x03
    je .fail                    ; write-protected: retrying cannot help
    mov ah, 0x00                ; failed: reset the controller and retry
    mov dl, [dsk_unit]          ; the ROW's drive (SPEC.md 52.10.3): resetting
    CURXFER_ON                   ; ...and the arrow keeps moving through it too
    int 0x13                    ; drive 2 because the volume index is 2 resets
    CURXFER_OFF                  ; a floppy that is not in this transfer. A
                                ; reset on ageing media is where the machine
                                ; grinds longest, so it is the LAST place to
                                ; let the pointer die (SPEC.md 7.4.1)
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
    jmp .attempt                ; NOT `jmp short`: SPEC.md 7.4.1's two stores
                                ; put .attempt out of a byte's reach on the
                                ; DISKCNT=1 build, whose counters sit in this
                                ; same loop - and a knob nothing ships is a
                                ; build only test-full compiles, so it fails
                                ; for somebody else. Written plainly, this
                                ; grows a byte rather than failing to
                                ; assemble (fprog.inc's idiom, for its reason)

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
    SPLCALL splf_step               ; it is stepped per sector rather than per
    mov bp, fpg_step
    call ct_cw_mem_disp         ; call, or the bar would under-count exactly
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
    call dsk_bpb_bank_zap       ; an I/O error is exactly the shape "this is
    stc                         ; not the disk you think it is" arrives in
.out:
%ifdef OS88_COMPACT
    pop word [mem_pinseg]       ; the destination is nobody's again - or the
%endif
    dec byte [sch_lock]         ; outer pin's (dec and pop leave CF alone, and
    jmp kretc_bp

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
dsk_vol_row_x:
    call dsk_vol_slot_x
    jc .out                     ; the slot already left BX = 0 and CF = 1
    cmp byte [bx+DV_KIND], DVK_FREE
    clc                         ; CLC writes CF alone on the 8086, so the
    jne .out                    ; compare's ZF is still the one being read
    xor bx, bx
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; dsk_boot_from - adopt the volume the boot sector loaded us from
; in:  DL = the BIOS drive it read with; BX:CX = the partition base (both 0
;      from boot/boot.asm, which does not set them and does not need to)
; out: nothing; every register preserved
;
; Called from kmain before anything touches a disk (SPEC.md 52.10.3).
;
; A FLOPPY BOOT IS ALMOST NOTHING: for units 0 and 1 the unit IS the volume
; index - a live row whose unit matches - so this stores the byte drv_mounted
; was hardcoding and returns. It is not true of a unit above 1 and the arm
; used to assume it was (SPEC.md 18.7.1.1), so the store is now behind a look
; at the row it is about to name. Still the common path, still free.
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
%ifndef KD_BUILD                    ; **THE BOOT OVERLAY IS NOT IN kern_dos**
                                    ; (SPEC.md 96.43.2): there is no kmain to
                                    ; run it, the DPT arrives in the launch
                                    ; block (96.40.2) and the volume the
                                    ; program came off is named rather than
                                    ; enumerated - so nothing in that root
                                    ; calls one of these and they are 762
                                    ; bytes of a build whose every byte is a
                                    ; byte off the DOS program
section .ovlw                  ; THE BOOT OVERLAY (SPEC.md 2.5.2,
                                ; docs/plans/LAST-DROP-BYTES.md row 4): which volume
                                ; the machine came off, asked once by kmain
                                ; from the registers the boot sector left.
                                ; dsk_bootltr goes with it - it is this
                                ; body's only caller's only callee, and its
                                ; work (stamping the letter into three fixed
                                ; messages) is done once and stays done
dsk_boot_from_x:
    push ax
    push bx
    push si
    test dl, 0x80
    jnz .hard

    ; --- A FLOPPY BOOT, AND DL IS A BIOS UNIT (SPEC.md 18.7.1.1) ------------
    ; It is a volume index as well for units 0 and 1 and for nothing else:
    ; row 2 is C:, reserved for the hard disk, and dsk_flop_add skips it, so
    ; a third drive is unit 2 at volume 3 and `mov [dsk_bootvol], dl` named a
    ; DVK_FREE row. `boot/boot.asm`'s range check passes 0..3 - a unit above
    ; 3 is not a drive that sector can have come from and 2 and 3 are - so
    ; the contract that arm was written against ("DL is 0 or 1") was never
    ; enforced anywhere.
    ;
    ; So NAME A ROW THAT EXISTS. Rows 0 and 1 are live from the initialiser
    ; here and every other one is free, so this asks exactly "is DL a volume
    ; index too" - written as that question rather than as `cmp dl, 1`, which
    ; would be the same answer for a reason that could go stale.
    mov bl, dl
    xor bh, bh
    add bx, bx                  ; DV_SIZE = 16: the index to a row, four
    add bx, bx                  ; doublings rather than `shl bx, cl`, because
    add bx, bx                  ; CX is the caller's partition base and this
    add bx, bx                  ; routine preserves every register it is given
    cmp bx, DVOL_MAX*DV_SIZE
    jae .nota                   ; past the table entirely
    cmp byte [bx+dsk_vtab+DV_KIND], DVK_FREE
    je .nota                    ; ...or a row nothing has claimed
    mov [dsk_bootvol], dl
    jmp short .out
.nota:
    ; **BOOTING FROM AN EXTERNAL FLOPPY IS NOT SUPPORTED** (18.7.1.1): getting
    ; it right means claiming the drive's row here, which is desk_init's job
    ; three phases later. A: is what the out-of-range arm already chose and it
    ; names a volume that is there.
    mov byte [dsk_bootvol], 0
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

    mov bx, dsk_vtab + 2*DV_SIZE    ; the row walks, as dsk_vol_add's does -
                                ; and from row 2, which at THIS point in kmain
                                ; is always free, so a boot partition is
                                ; always C:. That used to be a happy accident
                                ; of the call order; SPEC.md 18.7.1 makes it
                                ; the rule, and dsk_flop_add now skips row 2
                                ; so a floppy cannot take it first. Do not
                                ; "tidy" this to scan from 0: rows 0 and 1
                                ; are the floppies' by that same rule
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
    retf

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
; a format call at three sites, for one byte. Each store below names the
; letter BYTE, and each of those is its own LABEL inside its string - an
; offset would silently drift the first time somebody reworded a message.
;
; Called from dsk_boot_from's .out, so a floppy machine runs it too and
; writes back the 'A' that was already there.
; -----------------------------------------------------------------------------
dsk_bootltr:
    push ax
    mov al, [dsk_bootvol]
    add al, 'A'
    ; FIVE STORES, NOT A TABLE. This used to walk `dsk_ltrtab`, five words of
    ; `.text` naming the letter bytes - and since this body is in `.ovlw` and
    ; runs once, that table was the only part of the stamp still RESIDENT: ten
    ; bytes kept for the life of the machine to serve one boot-time loop. A
    ; `mov [moffs], al` is three bytes, so the unrolled stamp is SMALLER than
    ; the loop it replaces (no BX/CX/SI to bank) and the table is gone
    ; (kernel size pass 5). Each target is still a LABEL on the letter byte
    ; inside its string, never an offset, for the reason above.
    mov [ui_s_nodisk_l], al     ; the Task Manager's notice (SPEC.md 28.3)
    mov [cp_s_dsdrv], al        ; the Control Panel's save refusal (51.5.1)
    mov [drv_e1_l], al          ; DRVE_DISK, the Drivers page's status (51)
    mov [cp_s_nodrv], al        ; ...and its OPEN refusal (SPEC.md 2.8.4), now
                                ; that the panel's own code is a file on that
                                ; same disk
    mov [fm_s_swapnd], al       ; ...and the formatter's REFUSAL toast (2.8.5).
                                ; Its swap PROMPT names the volume too, and is
                                ; the one standalone 'A:' of the six - it is
                                ; `fm_s_swapd equ ui_s_nodisk_l` now, so the
                                ; first store stamps both and there is no sixth
    pop ax
    ret
%endif                              ; KD_BUILD
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


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
; dsk_flop_add - register a BIOS floppy beyond A: and B: (SPEC.md 18.98)
; in:  AL = the int 13h drive number (2 or 3)
; out: CF=0 and AL = the volume index it took; CF=1 = no free row
; clobbers: AX (the output), flags
;
; The pair on the 5.25" adapter's external 37-pin connector. Nothing about
; the transport is new - a floppy is already "a DVK_BIOS row whose unit is
; below 80h" everywhere that asks (disk_mount's medium test, dsk_geom_pick's
; cylinder bound, dsk_here_ok's motor bit, dskw_fmt_probe's refusal), and
; dsk_mbit has had four entries since SPEC.md 18.9.1. All that was missing is
; a ROW.
;
; It takes the first FREE row rather than row AL, and that is what keeps
; SPEC.md 52.10.3's boot partition at C: on a machine that booted from a hard
; disk: dsk_boot_from runs first and has already taken row 2 there, so the
; externals land at D: and E: and the volume the machine booted from keeps the
; letter dsk_bootltr has already stamped into three messages. ON A FLOPPY
; MACHINE THEY ARE D: AND E: TOO, not the C: and D: this paragraph used to
; claim: row 2 is the hard disk's whether or not one turns up (SPEC.md
; 18.7.1) and the scan below skips it, which is the departure from DOS 4 and
; earlier that section pays on purpose.
; -----------------------------------------------------------------------------
%ifndef KD_BUILD                    ; **THE BOOT OVERLAY IS NOT IN kern_dos**
                                    ; (SPEC.md 96.43.2): there is no kmain to
                                    ; run it, the DPT arrives in the launch
                                    ; block (96.40.2) and the volume the
                                    ; program came off is named rather than
                                    ; enumerated - so nothing in that root
                                    ; calls one of these and they are 762
                                    ; bytes of a build whose every byte is a
                                    ; byte off the DOS program
section .ovlw                  ; ...and row 9, for the same reason one
                                ; screen down: desk_init is ALREADY in the
                                ; overlay and reaches this through a `.text`
                                ; thunk, which is the only edge there is
dsk_flop_add_x:
    push bx
    push cx
    mov cl, al                  ; the unit, before AX becomes the answer
    mov bx, dsk_vtab            ; the ROW walks, as dsk_vol_add's does - and
                                ; it starts at A: (SPEC.md 18.7.1). This was
                                ; row 2, which made an external floppy C:
                                ; even on a machine whose B: had been proven
                                ; absent and freed: A and B are the first two
                                ; FLOPPIES the machine has, wherever they are
                                ; plugged in. Row 0 is never free (only
                                ; dsk_fdd_retire frees one, and only row 1),
                                ; so this cannot hand out A: by accident
.scan:
    cmp bx, dsk_vtab + DVOL_MAX*DV_SIZE
    jae .full
    cmp bx, dsk_vtab + 2*DV_SIZE
    je .next                    ; ...and C: BELONGS TO THE HARD DISK, whether
                                ; or not one ever turns up. dsk_vol_add still
                                ; scans from row 2, so the first partition
                                ; lands there the moment floppies stop taking
                                ; it - and on a floppy-only machine with three
                                ; drives the letters read A, B, D, which is
                                ; the price and is paid on purpose
    cmp byte [bx+DV_KIND], DVK_FREE
    je .got
.next:
    add bx, DV_SIZE
    jmp short .scan
.got:
    mov [bx+DV_UNIT], cl
    mov word [bx+DV_SECS], 0    ; unknown, like every other floppy's
    mov byte [bx+DV_FLAGS], 1   ; shown on the desktop - the caller only calls
                                ; for a drive the equipment word claims
    mov byte [bx+DV_LBL], 0     ; ...and NAMED by the kernel, not here: an
                                ; empty label is what makes desk_zone_label
                                ; derive 'Disk C' from the index, the same
                                ; courtesy dsk_vol_add extends a driver
    mov byte [bx+DV_KIND], DVK_BIOS ; published LAST, dsk_vol_add's discipline
    mov ax, bx
    sub ax, dsk_vtab
    mov cl, 4
    shr ax, cl                  ; DV_SIZE = 16: the row back to an index
    pop cx
    pop bx
    clc
    retf
.full:
    pop cx
    pop bx
    stc
    retf

; -----------------------------------------------------------------------------
; dsk_vol_fixed - is this volume a HARD DISK? (SPEC.md 18.7.2)
; in:  BL = volume index
; out: CF=1 = no such volume (out of range, or a free row); CF=0 with AL = 0
;      removable / 1 fixed, and AH = VT_BIOS / VT_DRIVER
; clobbers: AX (the output), flags
;
; **DV_KIND IS THE TRANSPORT AND NOT THE MEDIUM**, which SPEC.md 18.7 has said
; since the volume table existed and which nothing shared enforced. A hard
; disk reached through a DRIVER is DVK_DRV; a hard disk reached through the
; BIOS - a boot partition, SPEC.md 52.10.3 - is DVK_BIOS with unit 80h or
; above, and every `cmp DV_KIND, DVK_DRV` in the tree read that as a floppy.
; desk_draw_zone did, so a machine booted off its hard disk drew C: with the
; FLOPPY icon.
;
; One predicate, so the desktop and OSAPI_VOL_KIND cannot drift: the same
; question is now asked once and answered once.
; -----------------------------------------------------------------------------
%endif                              ; KD_BUILD
section .cold                   ; ...and back to resident cold code
dsk_vol_fixed_x:
    push bx
    call dsk_vol_slot_x
    jc .no
    mov ah, [bx+DV_KIND]
    cmp ah, DVK_FREE
    je .no                      ; AH IS the transport: VT_BIOS/VT_DRIVER are
                                ; DVK_BIOS/DVK_DRV, asserted below rather than
                                ; assumed, so a change to either breaks the
                                ; BUILD instead of the published ABI
    mov al, 1                   ; a driver volume is fixed by construction
    cmp ah, DVK_BIOS            ; (SPEC.md 52 mounts partitions and nothing
    jne .out                    ; else through DSV_BLK), and so is a REDIRECTED
                                ; one, which has no medium at all to eject
                                ; (SPEC.md 62.9).
                                ;
                                ; THE TEST IS "NOT BIOS" AND NOT "IS DVK_DRV",
                                ; because DV_UNIT below is an int 13h DRIVE
                                ; NUMBER on a BIOS row and THE DRIVER'S OWN
                                ; HANDLE on every other one. Fallen through to,
                                ; a handle with bit 7 set would have answered
                                ; `fixed` by luck and one without it
                                ; `removable` - which is a floppy icon on the
                                ; desktop for a network drive, decided by a
                                ; number the kernel does not get to interpret
    xor al, al                  ; ...and a BIOS one is fixed exactly when its
    test byte [bx+DV_UNIT], 0x80    ; int 13h drive says so - disk_mount's own
    jz .out                     ; test, in the one place that owns it
    mov al, 1
.out:
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret
%if VT_BIOS != DVK_BIOS || VT_DRIVER != DVK_DRV || VT_FILE != DVK_FILE
  %error "disk: SPEC.md 18.7.2 - VT_* must equal DVK_*, or dsk_vol_fixed lies"
%endif

; -----------------------------------------------------------------------------
; dsk_vol_slot - a volume index's row, live or not (module internal)
; in:  BL = volume index
; out: CF = 0 and BX = the row; CF = 1 = out of range
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
dsk_vol_slot_x:
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

%ifndef KD_BUILD                ; **NO DRIVER-BACKED VOLUME UNDER kern_dos**
                                ; (SPEC.md 96.44.9). A `DVK_DRV` volume's
                                ; transport is a loadable driver and a
                                ; `DVK_FILE` one has no sectors at all - and
                                ; neither survives the handoff, which is not a
                                ; hope: `hbm_*`'s gather writes every NON-BIOS
                                ; row `DVK_FREE` into the launch block
                                ; (kernel/hiber.inc), so kern_dos's own table
                                ; can hold only DVK_BIOS and DVK_FREE by
                                ; construction. The RAM disk keeps working in
                                ; the WINDOW, which is this same file with
                                ; KD_BUILD undefined
; -----------------------------------------------------------------------------
; dsk_vol_kind - what kind of volume is mounted right now?
; in:  nothing
; out: AL = DVK_*, or DVK_FREE if [disk_drive] names no live row
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; osapi_vol_at - is this PARTITION already a volume? (slot 0x02F6, SPEC 52.10.3)
; in:  DL = an int 13h drive number, BX:CX = the partition's 32-bit base LBA
;      (BX = the high word, CX = the low - dsk_boot_from's own argument shape)
; out: CF = 0 and AL = the volume index that covers it; CF = 1 = none does
; clobbers: AL (the output), flags
;
; **This exists because the kernel gets there first.** On a machine installed
; to its hard disk, dsk_boot_from adopts the partition the BIOS booted it from
; before any driver loads (52.10.3) - it has to, because HDD.DRV is a file on
; that very volume. The driver then probes the same disk, finds the same FAT
; partition, and has no way of knowing it is already C:, so its Mount put a
; SECOND volume and a second desktop icon on the same sectors: one from the
; mount, one from the BIOS.
;
; It answers about DVK_BIOS rows and only those, which is not tidiness. A
; driver-backed row's DV_UNIT is the DRIVER's own volume handle - a small
; integer that would collide with an int 13h drive number for free - and its
; partition base never reaches the kernel at all, dsk_xfer handing the driver
; a volume-relative LBA and the driver adding its own base (18.7). So a
; driver's rows are the DRIVER's to recognise, out of its own table, and this
; is the half it cannot see.
;
; dsk_vbase is written by dsk_boot_from ALONE and that row can never be freed
; (dsk_vol_del takes only DVK_DRV and DVK_FILE), so a stale base cannot be
; inherited by a row somebody else later takes - which is what makes the
; comparison below safe with no clearing anywhere.
; -----------------------------------------------------------------------------
osapi_vol_at_x:
    push bx
    push si
    push di
    xor al, al                  ; AL = the volume index under test, and the
    mov si, dsk_vtab            ; answer if one matches
    mov di, dsk_vbase           ; ...whose stride is 4 and not DV_SIZE: the
.scan:                          ; base is a side table indexed by VOLUME
    cmp byte [si+DV_KIND], DVK_BIOS ; a free row is 0xFF, so this drops it too
    jne .next
    cmp [si+DV_UNIT], dl
    jne .next
    cmp [di], cx                ; the base, low word...
    jne .next
    cmp [di+2], bx              ; ...and high
    je .found
.next:
    add si, DV_SIZE
    add di, 4
    inc al
    cmp al, DVOL_MAX
    jb .scan
    pop di
    pop si
    pop bx
    stc
    retf
.found:
    pop di
    pop si
    pop bx
    clc
    retf

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
    call dsk_bpb_bank_zap       ; the volume TABLE moved: a banked head names a
                                ; row, and a row can be re-used by another
                                ; partition, or the same one re-formatted
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the handle for the duration
    mov bx, dsk_vtab + 2*DV_SIZE    ; the ROW walks, not an index: the index
    cmp byte [dsk_vcls], DRVC_DISK  ; ...AND C: IS RESERVED FOR A HARD DISK.
    je .scan                        ; Index 2 is the first a driver may have
    add bx, DV_SIZE                 ; and a RAM disk or a cable took it simply
                                    ; by mounting first - so a machine that
                                    ; gained a hard disk later found C: in use
                                    ; and its partitions lettered from D:,
                                    ; which is not what anybody who has used
                                    ; DOS expects a hard disk to be called.
                                    ; Everything that is not a DRVC_DISK now
                                    ; starts its search one row up.
                                    ;
                                    ; THE GAP COSTS NOTHING ON THE DESKTOP:
                                    ; desk_zone_xy places a zone by its ORDINAL
                                    ; among the shown volumes (desk_ord), not
                                    ; by its table index, so a reserved C: on a
                                    ; machine with no hard disk leaves no hole -
                                    ; the RAM disk is simply the third icon and
                                    ; is called D:
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
    mov al, [dsk_vcls]          ; which CLASS serves this row - the fence
    mov [bx+DV_CLASS], al       ; matched the caller's segment against a
                                ; publication slot and left the class here
    mov ah, DVK_DRV             ; ...AND THE KIND FOLLOWS THE CLASS. A
    cmp al, DRVC_FILE           ; DRVC_FILE driver answers questions about
    jne .kindpub                ; FILES (SPEC.md 62.9), so its volume has no
    mov ah, DVK_FILE            ; sectors at all and dsk_xfer refuses it.
.kindpub:                       ; DERIVED and not passed, because a driver
    mov [bx+DV_KIND], ah        ; that published DSV_FS and then asked for a
                                ; BLOCK volume is not a thing that can be
                                ; meant - and an argument would be one more
                                ; way for the row and the dispatch to
                                ; disagree. Published LAST either way: the
                                ; row is live from this store on
    call desk_zmark_x             ; the desktop owes a zone - and a driver adds
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
    call dsk_bpb_bank_zap       ; the volume TABLE moved: a banked head names a
                                ; row, and a row can be re-used by another
                                ; partition, or the same one re-formatted
    push ax
    push bx
    mov bl, al
    call dsk_vol_row_x
    jc .out
    mov ah, [bx+DV_KIND]        ; EITHER kind of driver row: DVK_DRV serves
    cmp ah, DVK_DRV             ; sectors, DVK_FILE serves FILES (SPEC.md
    je .mine                    ; 62.9), and both were added by a driver and
    cmp ah, DVK_FILE            ; are that driver's to take away. A BIOS
    jne .out                    ; floppy is not, and neither is a free row
.mine:
    call desk_zmark_x             ; ...and owes the zone's removal likewise -
                                ; BEFORE the flag goes, so the mark counts the
                                ; zone that is about to vanish. Its pixels are
                                ; at the ordinal nothing will occupy after
                                ; this, and they are exactly what the repaint
                                ; has to reach (SPEC.md 26.3)
    mov byte [bx+DV_KIND], DVK_FREE
    mov byte [bx+DV_FLAGS], 0
    mov byte [bx+DV_LBL], 0
    call dsk_fatw_drop          ; AL is still the index; it also puts the
                                ; live window back if this volume owned it
    cmp al, [disk_drive]
    jne .out
    xor ax, ax                  ; the mounted volume just went away: fall back
    mov [disk_drive], al        ; to A: with the write gate shut, exactly as a
    mov [dsk_mntok], al         ; failed mount leaves things (SPEC.md 18.3).
    mov [disk_nfiles], ax       ; AL was the index and the compare above is
    dec ax                      ; its last reader, so these four are the
    mov [dsk_fatw0], ax         ; accumulator's: 15 bytes against 22
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
dsk_vol_drop_drv_x:
    push ax
    push bx
    push cx
    mov cl, al                  ; the class being torn down
    mov al, 2
.next:
    mov bl, al
    call dsk_vol_slot_x           ; BX = the row; AX is untouched by it
    jc .skip
    mov ah, [bx+DV_KIND]        ; either kind of driver row (dsk_vol_del's
    cmp ah, DVK_DRV             ; reasoning, and the same two compares): a
    je .drvrow                  ; DRVC_FILE driver detaching has to take its
    cmp ah, DVK_FILE            ; REDIRECTED volume with it, or the desktop
    jne .skip                   ; keeps a zone whose transport has been freed
.drvrow:
    cmp [bx+DV_CLASS], cl       ; ...and is it THIS driver's?
    jne .skip
    call dsk_vol_del            ; takes AL, which is still the index
.skip:
    inc al
    cmp al, DVOL_MAX
    jb .next
    jmp kretc_cx

%endif                          ; KD_BUILD
; dsk_list_floor and dsk_list_pick lived here, and SPEC.md 22.6 deleted both.
; There were two homes for a listing - the `.lowbss` floor, and the claim a
; DVK_DRV volume's driver donated - so the mount had to CHOOSE one and the
; volume-removal path had to put it back. With the donation retired there is
; one home, [dsk_dseg]/[dsk_doff]/[dsk_nmax] are written by nobody, and their
; `.text` initialisers below are the whole of the configuration.
;
; **THE THREE WORDS STAY, and that is a size decision rather than inertia**:
; every reader is `mov es, [dsk_dseg]` at four bytes, where the immediate form
; is `mov ax, LOW_SEG` + `mov es, ax` at five AND costs a scratch register at
; sites that have none to spare.

; =============================================================================
; The API slots a DRVC_DISK driver mounts a volume through (SPEC.md 51.8)
;
; The fence on all four is the same one drv_task uses: the caller's segment
; must be the segment of the driver whose DISK services are published. That
; is an identity test rather than an approximation, and it is what stops a
; package from registering a volume the kernel would then read through a
; transport nobody published.
; =============================================================================

%ifndef KD_BUILD                ; ...and the SLOTS a driver registers one
                                ; through, which are reachable only via the
                                ; API table - and since 96.44.6 kern_dos has
                                ; no table at all
; -----------------------------------------------------------------------------
; osapi_vol_fence - is the caller the published disk driver? (internal)
; in:  ES = the caller's segment (the slot's X stub, or the caller's own DS)
; out: CF = 0 allowed, CF = 1 refused
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
osapi_vol_fence:
    push ax
    push bx
    push di
    mov bx, es
    or bx, bx
    jz .no
    mov al, 1                   ; WALK the classes rather than naming one. It
.cls:                           ; tested [drv_fseg2] alone - the DISK class -
    call drv_cls_fp_x             ; which is a fence that only one kind of
    jc .no                      ; driver can ever pass, and a second
    cmp bx, [di+2]              ; block-serving class (docs/plans/completed/NET-PLAN.md 2.1)
    je .yes                     ; would have been refused its own volumes
    inc al
    cmp al, DRVC_MAX
    jbe .cls
.no:
    pop di
    pop bx
    pop ax
    stc
    ret
.yes:
    mov [dsk_vcls], al          ; dsk_vol_add stamps the row with it
    pop di
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; osapi_vol_add - slot 0x01F8 (X: ES = the caller's segment)
; in:  AL = the driver's volume handle, CX = sectors, DX = a listing claim's
;      segment (0 = the floor), ES:SI = a NUL label or SI = 0
; out: CF = 0 and AL = the volume index; CF = 1 refused
; -----------------------------------------------------------------------------
osapi_vol_add_x:
    call osapi_vol_fence
    jc .no
    call dsk_vol_add
    retf
.no:
    stc
    retf

; -----------------------------------------------------------------------------
; osapi_fs_ent - slot 0x02D9 (X: ES = the caller's segment), SPEC.md 62.9.1
; in:  ES:SI -> a DSK_DE_SIZE-byte staged SPEC.md 19.1 entry, in the DRIVER's
;      own segment
; out: CF = 0 and AX = the index it landed at; CF = 1 = the listing is full,
;      or this was called from outside FSV_LIST
; clobbers: AX (the output), flags
;
; THE DRIVER APPENDS, IT DOES NOT STAGE. dsk_put_dir is module-internal and
; knows whether this volume's listing is the .lowbss floor or a claim the
; driver itself donated (SPEC.md 22.6); the count is disk_nfiles'; and the
; sort has not run yet. Three pieces of kernel bookkeeping, none of which a
; driver may hold - so it hands over one entry and is told where it went.
;
; [dsk_fslist] IS THE WHOLE FENCE, and it is a gate on the MOMENT rather than
; on the caller. There is no identity test here and there does not need to be:
; the only window in which the flag is up is between disk_mount's two stores
; around FSV_LIST, the kernel is inside a far call to the driver for all of
; it, and nothing else on the machine is running - dsk_xfer's [sch_lock] is
; not even needed, because a mount holds the gfx lock. Outside that window
; every caller is refused, driver or not.
; -----------------------------------------------------------------------------
osapi_fs_ent_x:
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [dsk_fslist], 0
    je .no
    mov ax, [dsk_fsn]
    cmp ax, [dsk_nmax]
    jae .no                     ; SPEC.md 19's cap for this volume: the extras
                                ; are invisible, exactly as they are on a FAT
                                ; volume whose folder overruns it
    mov dx, es                  ; the driver's segment...
    mov di, dsk_ent             ; ...into the mount's own staging entry, which
    mov cx, DSK_DE_SIZE         ; is where every other producer puts one
    push ax
    mov ax, ds                  ; the KERNEL segment, and NOT `mov ax, cs`:
                                ; this body is in .cold now, where CS is
                                ; COLD_SEG and only DS still names KERNEL_SEG
                                ; (SPEC.md 2.6 rule 1)
    call dsk_copy_seg_x
    pop ax
    mov byte [dsk_ent+15], 0    ; THE NAME TERMINATES, whatever arrived. Bytes
                                ; 0..15 are it and font_str reads to a NUL, so
                                ; a driver that filled all sixteen would send
                                ; the renderer into the type word and on
                                ; through the entry - one store against a
                                ; whole class of bug, in the one place every
                                ; redirected entry passes
    call dsk_put_dir            ; AX = the index, and the listing's home is
    inc word [dsk_fsn]          ; the one every volume has (SPEC.md 22.6)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    retf
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    xor ax, ax
    stc
    retf

; -----------------------------------------------------------------------------
; osapi_vol_del - slot 0x01FE (X).  in AL = volume index
; -----------------------------------------------------------------------------
osapi_vol_del_x:
    call osapi_vol_fence
    jc .out
    call dsk_vol_del
.out:
    retf

; -----------------------------------------------------------------------------
; osapi_vol_mount - slot 0x0204 (X).  in AL = volume index; out CF = 1 refused
;
; The Mount button (SPEC.md 51.8). UI-task context only, exactly like every
; other file slot: it takes [sch_lock] around int 13h and rewrites the one
; global mount snapshot.
; -----------------------------------------------------------------------------
osapi_vol_mount_x:
    call osapi_vol_fence
    jc .nofence
    push bx
    push dx
    mov bl, al
    call dsk_vol_row_x            ; the row's own existence is the check...
    jc .no
    mov dl, al                  ; ...and the INDEX is still in AL, which the
    call disk_mount_x             ; lookup does not touch - BL is the row's low
                                ; byte by the time it returns
    jc .no
    pop dx
    pop bx
    clc
    retf
.no:
    pop dx
    pop bx
.nofence:
    stc
    retf

; -----------------------------------------------------------------------------
; osapi_vol_paint - slot 0x0288: repaint the desktop's drive zones
;
; It takes the gfx lock ITSELF, so a driver must not call it from a page
; click that already holds one - the Control Panel's own deferred-repaint
; idiom ([cp_dirty], SPEC.md 31.2) is the shape to copy.
; -----------------------------------------------------------------------------

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

%endif                          ; KD_BUILD
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
dsk_fat_ofs_x:
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
    call dskw_flush_x             ; anything dirty goes back before it is lost.
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
    mov es, [dsk_fatseg]
    mov cx, [dsk_fatwn]
    xor bx, bx
    add ax, [dsk_fatlba]
    call disk_read_x
    pop ax
    jnc .ok
    cmp byte [dsk_nfats], 2     ; FAT1 unreadable: FAT2 is the redundancy the
    jne .out                    ; format gives for free (SPEC.md 18.3)
    push ax
    add ax, [dsk_fatlba]
    add ax, [dsk_fatsz]
    mov cx, [dsk_fatwn]
    xor bx, bx
    call disk_read_x
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
disk_mount_x:
    call kentc_bp               ; KENT: push ax..bp






    push es
%ifdef DISK_COUNTERS
    inc word [dsk_dbg_mnt]
%endif

    call dsk_fatw_park          ; the OUTGOING volume's window, banked while
                                ; [disk_drive] still says which one it is
    mov [disk_drive], dl
%if DVK_BIOS != 0               ; the block below SPELLS DVK_BIOS as a zeroed
  %error "DVK_BIOS is no longer 0: dsk_vkind below is stored from AL"
%endif                          ; AL, so the named constant is asserted here
                                ; rather than quietly assumed. Five stores
                                ; through the accumulator are 21 bytes where
                                ; five immediates are 28, and AX is dead here
                                ; by construction: dsk_fatw_park above answers
                                ; in nothing, and dsk_vol_row_x below spends
                                ; AX for its own reasons whichever way it goes
    xor ax, ax
    mov [dsk_vkind], al         ; what the BPB rules are checked against
    mov [dsk_vsecs], ax         ; (SPEC.md 18.2/18.7)
    inc ax
    mov [dsk_vflop], al
    inc ax
    mov [disk_heads], ax        ; the geometry for the LBA 0 read, BEFORE the
    mov al, 9                   ; BPB can supply one - see .nofloppy below
    mov [disk_spt], ax
    push bx
    mov bl, dl
    call dsk_vol_row_x
    jc .kindok
    ; DV_SECS is loaded FIRST and DV_KIND LAST, because `mov ax, [bx+DV_SECS]`
    ; writes AL and the floppy test below reads it. The other order assembles
    ; to the same twelve bytes and asks `is DV_SECS's low byte zero?`.
    mov ax, [bx+DV_SECS]
    mov [dsk_vsecs], ax
    mov al, [bx+DV_KIND]
    mov [dsk_vkind], al
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
    jz .kindok                  ; a floppy: vflop stays 1, geometry stays 9/2
    mov ax, [dsk_bootspt]       ; a BIOS HARD DISK - SPEC.md 52.10.3's boot
    or ax, ax                   ; partition - needs its OWN geometry, and this
    jz .nofloppy                ; is the one read that cannot get it from the
    mov [disk_spt], ax          ; BPB: it is the read that FETCHES the BPB.
    mov ax, [dsk_boothds]       ; Never probed: leave the floppy fallback and
    mov [disk_heads], ax        ; let the mount fail honestly
.nofloppy:
    mov byte [dsk_vflop], 0
.kindok:
    pop bx
    ; THE GEOMETRY IS SETTLED ABOVE, inside the block that already had the
    ; row. It used to be a second dsk_vol_row_x lookup asking the same two
    ; questions of the same row thirty lines later. Its LBA is not 0 either:
    ; a partition's sector 0 is `base` sectors in, so the wrong geometry does
    ; not merely mis-seek by a track, it asks for a CHS that translates
    ; somewhere else entirely. With 9/2 and a base of 26 the drive was asked
    ; for (1,0,9) and answered LBA 112 - inside the FAT - so the BPB never
    ; validated and the volume "was not an os8088 disk". The machine still
    ; booted, because the BOOT SECTOR probes AH=08h and reads the kernel
    ; correctly; the kernel simply never mounted the volume it had booted
    ; from, which is why this showed up as a Task Manager that could not find
    ; its file and a Disk window with nothing in it rather than as a failure
    ; to start.
    ; **EIGHT IMMEDIATE STORES, THROUGH THE ACCUMULATOR.** `mov word [m], imm`
    ; is SIX bytes and 20 clocks where `mov [m], ax` is THREE and 14, so this
    ; is 31 bytes against 46 and faster on the target machine as well - the
    ; same argument dsk_bpb_check makes for `mov bh, [dsk_vflop]` a page down.
    ; AX is dead here by CONSTRUCTION and not by inspection: the block above
    ; ends at `pop bx`, and every path through it - the row lookup, the floppy
    ; test, the boot partition's geometry - has already spent AX for its own
    ; reasons. The stores are ordered by what AX has to hold; each keeps the
    ; comment that says why it is set.
    xor ax, ax
    mov [disk_nfiles], ax       ; stays 0 unless the mount fully succeeds
    mov [dsk_mntok], al         ; the write gate (SPEC.md 18.4) closes for
                                ; the whole mount: a torn mount leaves it
                                ; closed, so nothing can write a volume
                                ; whose layout was never fully validated
    mov [dsk_fatd1], ax         ; no window state survives a mount, and the
    mov [ld_pending], al        ; a queued click indexes the OLD directory;
                                ; never let it run against the new one
    inc ax
    mov [dsk_fatwn], ax         ; window itself is invalid until rule 10 has
    inc ax                      ; sized it (SPEC.md 18.8)
    mov [dsk_rover], ax         ; ...and 2 is the allocator hint's first
                                ; cluster
    mov ax, 0xFFFF              ; the FAT dirty range and the window's base,
    mov [dsk_fatd0], ax         ; both of which mean "nothing"
    mov [dsk_fatw0], ax
    cmp byte [dsk_keepcwd], 0   ; a remount always lands at the root, and
    jne .cwd_kept               ; dskw_* resolve there until dsk_chdir
    mov word [dsk_cwd], 0       ; moves - EXCEPT when dsk_chdir is the
.cwd_kept:                      ; caller, which is how navigation reuses
                                ; this whole validated pipeline (19.2)

%ifdef OS88_REDIR                   ; ...and kern_small CANNOT HAVE ONE EITHER
                                    ; (SPEC.md 62.9.2.3), for a reason one
                                    ; link further back than 96.44.9's: no
                                    ; class is ever published there, so
                                    ; osapi_vol_fence refuses every caller and
                                    ; no row is ever stamped DVK_FILE
    cmp byte [dsk_vkind], DVK_FILE  ; a REDIRECTED volume (SPEC.md 62.9) has
    jne .bpbread                    ; no boot sector, no BPB, no FAT window
    jmp .fsmount                    ; and no directory to walk: everything
%endif                              ; between here and the listing is SECTORS.
                                    ; **AND kern_dos CANNOT HAVE ONE** (SPEC.md
                                    ; 96.44.9): `[dsk_vkind]` is written from a
                                    ; table whose non-BIOS rows the gather
                                    ; already turned FREE, so the test is one
                                    ; that can only answer `jne` and the arm
                                    ; below is reached by falling through
.bpbread:

    ; --- 1. boot sector: read LBA 0, validate the BPB (SPEC.md 18.2) ---
    ; ...unless this volume is FIXED and we have validated it before, in which
    ; case the banked head is the same head and the read is a seek to cylinder
    ; 0 and back for an answer we already have (SPEC.md 18.9.2). A floppy
    ; always reads, because a floppy is the thing that can be swapped.
    call dsk_bpb_bank_get
    jnc .bpb_staged
    push ss
    pop es
    xor ax, ax                  ; LBA 0
    inc byte [dsk_rah_busy]     ; NEVER from §18.95's cache: this read is the
    call dsk_rd1_x                ; swap detector itself. Served from a cached
    dec byte [dsk_rah_busy]     ; chunk it would re-sign the OLD disk's boot
                                ; sector, [dsk_sigcur] would never move, and
                                ; every later hit - FAT window included -
                                ; would be the old disk's bytes laid onto the
                                ; new one by the next write. (dec leaves CF)
    jc .jfail
    call dsk_bpb_sig            ; [dsk_sigcur] = which disk this is (18.8.2),
                                ; off the sector we have just read anyway.
                                ; ONLY on this path: it sums dsk_secbuf, which
                                ; the banked path never filled - the bank
                                ; restored the signature along with the head
.bpb_staged:
    call dsk_bpb_check          ; fills the 18.1 vars + disk_spt/disk_heads
    jc .jfail
    call desk_learn_x           ; ...which is what settles a floppy drive's
                                ; KIND when its ROM would not (SPEC.md 26.4.1)
    call dsk_bpb_bank_put       ; ...and remember it, if this volume is fixed

    ; --- 2. the FAT WINDOW's first load (SPEC.md 18.8; ES only, never DS) ---
    ; Not the whole FAT any more - a 32MB FAT16 volume's is 64 sectors and the
    ; window is nine. Sector 0 is read EAGERLY, with its FAT2 fallback, for
    ; one reason: "a torn mount is a failed mount" is a property worth keeping,
    ; and a lazy first fault would move the moment an unreadable disk is
    ; diagnosed to some later cluster walk. Every window after this one faults
    ; in, with the same fallback.
    call dsk_rah_want           ; §18.95's cache, if a shed took it - here
                                ; rather than in the walker, because it now
                                ; serves every read and not only a directory's
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
    call dsk_next_clus_x          ; CF = outside [2, dsk_maxclus]
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
    jne .quiet
    cmp word [dsk_dseg], 0      ; ...AND A MOUNT WITH NOWHERE TO PUT A LISTING
    jne .listing                ; IS QUIET TOO (13.1). A listing has no home
                                ; of its own: it is written into the store its
                                ; CALLER supplied through dsk_dest_x, and a
                                ; caller that supplied none - the boot mount,
                                ; a driver registering a volume, inst_vol_enter
                                ; on every package file call - did not want one
.quiet:                         ; ...and .fsmount's copy of this arrives here
    xor ax, ax                  ; three stores through the accumulator, which
    mov [disk_nfiles], ax       ; kretc_es restores: 12 bytes against 16
    inc ax
    mov [dsk_lstale], al
    mov [dsk_mntok], al         ; the volume IS validated - writes are legal
    clc
    jmp .out
.listing:

    ; --- 3. root-dir scan -> synthesized directory (SPEC.md 19) ---
    ; §18.95's cache is dropped FIRST, and only on this path. A full mount
    ; is a navigation or a Refresh, and Refresh is exactly a request to re-read
    ; the disk - answered out of a cache it would be a no-op, which is the one
    ; thing it must never be (SPEC.md 18.9's rule for the quiet path, from the
    ; other end). A QUIET mount does not come through here, so a volume switch
    ; and back keeps the runs, which is the whole win.
    call dsk_rah_flush
    ; A sector at a time out of dsk_secbuf, wherever dsk_dirw_get got it from.
    ; DS never leaves the kernel
    ; segment while parsing: raw fields are read via es:, the staged entry is
    ; built in dsk_ent (kernel scratch) and copied out through dsk_put_dir -
    ; which knows whether this volume's listing is the .lowbss floor or a
    ; driver's claim, so nothing below has to (SPEC.md 22.6).
    push ss
    pop es
    xor bp, bp                  ; BP = accepted entry count
    cmp word [dsk_cwd], 0
    je .noup
    call dsk_synth_up           ; in a subdirectory the listing OPENS with
    xor ax, ax                  ; '..' (SPEC.md 19.5) - built before the scan
    call dsk_put_dir            ; so the scan's entries land above it and the
    mov bp, 1                   ; sort below never has to keep it in place
.noup:
    mov ax, [dsk_cwd]           ; the current directory (0 = root at mount;
    call dsk_dirw_start_x         ; dsk_chdir re-enters here, SPEC.md 19.2)
.rootsec:
    call dsk_dirw_get_x           ; dsk_secbuf = the next directory sector
    jnc .rootok
    or ax, ax
    jz .scan_done               ; ...the directory ended
    jmp .jfail                  ; ...as against a torn mount, which fails
.rootok:
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
    call dsk_synth_x              ; ES:SI raw entry -> dsk_ent (SPEC.md 19)
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
    add bx, DSK_DE_STRIDE       ; permuted alongside the entries (19.4). The
    dec cx                      ; parent link is not IN the sort - it is
.sortall:                       ; always the first entry (19.5)
    call dsk_sortdir

    ; Blank EVERY icon slot up front so none ever carries a previous mount's
    ; bytes (SPEC.md 18.3 step 4: fully rewritten every mount) - the harvest
    ; below then only fills the slots that earn an icon.
%ifdef DSK_WANT_ICONS           ; **STEP 4 IS THE DISK WINDOW'S** (SPEC.md
                                ; 96.44.7): a build with nothing to draw an
                                ; icon on has no `disk_icons` to blank and
                                ; nothing to harvest into it. The LISTING
                                ; above stays - 96.47's live resume finds
                                ; HIBERNAT.IMG through it
                                ; SPEC.md 25.8: blanking the INDEX is what
                                ; retires a listing, and the bodies below it
                                ; are simply re-reserved. 64 bytes of stosb
                                ; where this was 2,048 of stosw.
                                ;
                                ; **ES IS LEFT AS [dsk_dseg]**, which the sort
                                ; above has just loaded, and that is the
                                ; change rather than a saving: the index used
                                ; to be at a fixed `.lowbss` address while the
                                ; LISTING could be in a driver's claim, so it
                                ; had to be forced to SS. The two are one
                                ; block in one store now (13.1), so forcing it
                                ; would blank the wrong segment
    mov di, [dsk_ioff]
    mov cx, [dsk_nmax]
    mov al, 0xFF
    cld
    rep stosb

%ifdef OS88_REDIR
    cmp byte [dsk_vkind], DVK_FILE  ; ...and a REDIRECTED volume takes the
    jne .harvest0                   ; CACHE-ONLY pass below: it has no sectors
                                    ; to peek at, and reading an ASSOC.DAT over
                                    ; a cable is the one thing this may not do
    test byte [drv_svc + (DRVC_FILE-2)*DSV_SIZE + DSV_CAPS], FSCAP_LOCAL
    jnz .harvloc                ; ...UNLESS ITS READS ARE MEMORY (SPEC.md
                                ; 62.9.2.2), in which case it takes the real
    jmp .fsicons                ; harvest below - the peek is a `rep movsw`
%endif                          ; and the pass was refusing, on the CABLE's
.harvest0:                      ; grounds, a read the RAM disk serves faster
                                ; than the floppy it is being denied. **THE
                                ; INNER `%ifdef OS88_DRIVERS` IS GONE** and
                                ; the OS88_REDIR above is the whole gate now
                                ; (SPEC.md 62.9.2.3): the drv_svc read it was
                                ; protecting is exactly as impossible as the
                                ; volume it asks about, so one symbol says it

    mov ax, LOW_SEG             ; **AND [dsk_dseg] MAY NOT BE LIVE ACROSS THE
    mov es, ax                  ; CALL BELOW** (SPEC.md 66.5.10.2). This is not
                                ; the redundant load Fdisk-15 removed: asc_use_x
                                ; does bank ES, so the pair after the call is
                                ; enough to get the harvest its sector buffer -
                                ; but asc_use_x reaches mem_claim_x, and a claim
                                ; COMPACTS on its refusal path (50.6.2), which
                                ; MOVES the donated listing block this segment
                                ; names. Nothing reads the stale value today,
                                ; because that pair overwrites ES first - which
                                ; is precisely why the next use added between
                                ; the two would be silent. tools/dsegaudit.py
                                ; refuses the WINDOW rather than the read, and
                                ; it is the reason those five bytes are back

    ; --- 4. icon harvest (SPEC.md 18.3 step 4; failures are NON-fatal) ---
    ; Type-1 entries carry a validated first cluster: peek the file's first
    ; sector, and if it is a v3 .o88 with the embedded-icon flag, its bytes
    ; 32..95 are the 16x16 icon body. Everything else - type 0, iconless,
    ; read failure - gets the all-zero slot (the generic-icon sentinel).
    ; disk_icons is fully rewritten every mount (SPEC.md 29.1).
%ifdef OS88_ASSOC
    call asc_use_x                ; this volume's ASSOC.DAT (SPEC.md 54.7), so
                                ; the loop below can answer most of its icon
                                ; reads without doing them
%endif                          ; ...and kern_small has none (SPEC.md 54.0):
                                ; every icon below is read the slow way
%ifdef OS88_REDIR
.harvloc:                       ; **AND asc_use_x IS ABOVE THIS LABEL** (SPEC.md
                                ; 62.9.2.2): the peek is free and re-keying the
                                ; association cache to this volume is not - it
                                ; reads an ASSOC.DAT the RAM disk almost never
                                ; has and EVICTS the rows 62.9.2.1's own
                                ; lookups live on. The harvest below fills the
                                ; cache from the headers it reads anyway
%endif
    push ss                     ; asc_use reads the disk: put ES back. OUTSIDE
    pop es                      ; the gate - it is the loop's guarantee about
                                ; ES and not asc_use's own cleanup, so it must
                                ; not depend on whether the call happened
    xor dx, dx                  ; DX = entry index
.harvest:
    cmp dx, bp
    jae .harvested
    mov ax, dx
    call dsk_get_dir_x            ; SI -> dsk_ent, the synthesized entry
    call ico_ref_try            ; a folder, or a body the store already has?
    jnc .h_next                 ; then this entry costs no sector read at all
.h_read:
    mov ax, [si+18]             ; first cluster - or, on a redirected volume,
                                ; THE DRIVER'S OPAQUE HANDLE, which is what
                                ; rd_stage puts at @18 and what FSV_READAT
                                ; takes (SPEC.md 62.9.1)
    mov cx, DSK_PEEK            ; ...and DSK_PEEK bytes is every byte below
    call dsk_peek_x             ; reads: the header is 32, the embedded icon
    jc .h_zero                  ; 32..95 and the document glyph
    cmp ax, cx                  ; LD_H_GLYPH..+15. dsk_peek_x hands the asked
    jne .h_zero                 ; count back in CX, so this compare is 2 bytes
                                ; and not 3. **THE WHOLE ASK OR
                                ; NOTHING** - a short answer would leave the
                                ; PREVIOUS entry's header sitting in the
                                ; buffer, and that one is VALID, so a 40-byte
                                ; file would take the icon of the package
                                ; above it in the sort (SPEC.md 62.9.2.2).
                                ; The FAT arm answers the whole ask off one
                                ; sector, so this compare only ever bites a
                                ; redirected volume
    mov si, bx                  ; stage the 32 header bytes, from where
                                ; dsk_peek_x left BX pointing; dsk_ent's
    mov di, dsk_ent             ; directory copy is done with above.
    mov cx, DSK_DE_SIZE         ; A PACKAGE HEADER, not a 19.1 entry - so
                                ; DSK_DE_SIZE (dsk_ent's own width) and never
                                ; DSK_DE_STRIDE, which would be a .bss overrun
                                ; the moment the two diverge. It was a bare
                                ; `32` and that is exactly why it hid
    call dsk_copy_in_x
    cmp word [dsk_ent+0], 0x384F    ; .o88 magic 'O','8'
    jne .h_zero
    cmp byte [dsk_ent+2], PKG_FMT   ; format version (SPEC.md 20.2.0)
    jne .h_zero
    mov al, [dsk_ent+3]             ; BANK the header flags here. dsk_get_dir
%ifdef OS88_ASSOC
    mov [assoc_hflags], al          ; below re-stages the DIRECTORY entry over
                                    ; dsk_ent, so [dsk_ent+3] stops being the
                                    ; header's flags and becomes the fourth
                                    ; character of the file's NAME. The LOAD
                                    ; stays outside the gate: `test al, 1`
                                    ; below is the icon flag and is both
                                    ; builds'
%endif
    test al, 1                      ; embedded-icon flag
    jz .h_noicon                ; ...and an ICONLESS package still gets the
    mov bx, dsk_secbuf+32       ; rest of this: its header may DECLARE
    mov ax, dx                  ; extensions (SPEC.md 54.6), and hanging that
    push dx                     ; off the icon flag skipped precisely the
    mov dx, LOW_SEG
    call ico_put                ; AL = the row it took
%ifdef OS88_ASSOC
    cmp al, ICO_R_NONE          ; ...AND THE SHIPPED GLYPH GOES WITH IT
    je .h_noglyph               ; (SPEC.md 54.7.4). A row stored with an EMPTY
                                ; glyph column is not merely incomplete: the
                                ; next HIT on it takes asc_take's `.body` arm
                                ; and REDUCES, which overwrites whatever the
                                ; baked table, the seed or this very harvest
                                ; had resolved. Measured - browse APPS/, force
                                ; one miss, browse it again, and DOS's slot
                                ; goes 7effbfdfb1ff7e00 -> 7e818181b17e3c00,
                                ; after which every .EXE on the machine draws
                                ; its document icon with the wrong glyph in
                                ; it. asc_row_take fills this column out of a v2
                                ; cache row, and the harvest has to fill it
                                ; out of the header, or a package the cache
                                ; does not name can never keep one
    test byte [assoc_hflags], LD_HF_GLYPH
    jz .h_noglyph               ; ships none: the zeros ico_add wrote are the
    mov si, dsk_secbuf+LD_H_GLYPH   ; truth, and a hit reducing the body is
    call ico_glyph_put          ; exactly assoc_img_glyph's own fallback
.h_noglyph:                     ; (DX is still LOW_SEG, where the sector is)
%endif
    pop dx
.h_noicon:                      ; case the declaration ships on first
%ifdef OS88_ASSOC
    mov ax, dx                  ; the NAME again - the header staged above
    call dsk_get_dir_x            ; overwrote dsk_ent's copy of it
    call assoc_note_app_x         ; this program is HERE, declares THIS, and
                                ; looks like THAT (SPEC.md 54.3) - no extra
                                ; I/O: the sector is the one just read
%endif                          ; THE RE-STAGE GOES WITH THE CALL: its only
                                ; reader is assoc_note_app, and the next pass
                                ; of this loop calls dsk_get_dir_x itself
    jmp .h_next

.h_zero:                        ; no icon: the slot is already blank
.h_next:
    inc dx
    jmp .harvest
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: ...so neither arm
.fsicons:
    ; --- 4a'. A REDIRECTED VOLUME'S ICONS, FROM RAM ONLY (SPEC.md 62.9.2.1) --
    ; The harvest above is a sector read per package and there are no sectors
    ; here - but the ANSWER to most of those reads is already in memory, and
    ; asking costs nothing. asc_lookup matches a package on its STEM AND ITS
    ; SIZE, so a hit is the same name and the same byte count: the same
    ; package, not a guess that happens to be plausible. A miss leaves the
    ; blank slot the loop above wrote and 25's generic icon does its job.
    ;
    ; **asc_use_x IS DELIBERATELY NOT CALLED**, and that is the whole design
    ; rather than an omission. It re-keys the cache to [disk_drive] and reads
    ; that volume's ASSOC.DAT - which over a cable is exactly the traffic this
    ; exists to avoid, and would evict the rows that make the pass work. So
    ; the cache is used AS IT STANDS: whatever volume it describes, a row is
    ; only ever spent on a name-and-size match.
    ;
    ; The consequence to know is that this is SESSION STATE. A Link listing
    ; shows a package's icon if the user has browsed the disk it also lives on,
    ; and the generic one after a fresh boot. That is inherent to "only if it
    ; is already in RAM" and not a defect; nothing here may make it otherwise
    ; by going to look.
    push ss                     ; **AND [dsk_dseg] MAY NOT BE LIVE ACROSS THIS
    pop es                      ; LOOP EITHER** (SPEC.md 66.5.10.2), which is
                                ; `.harvest0`'s rule above applied to the pass
                                ; that did not have it. This read
                                ; `mov es, [dsk_dseg]`, and every one of
                                ; `ico_have`, `asc_take_x` and `ico_put` below
                                ; reaches `ico_need` -> `mem_claim_x` since
                                ; SPEC.md 25.9 made the icon bodies a
                                ; machine-wide store - and a claim COMPACTS on
                                ; its refusal path (50.6.2), which MOVES the
                                ; donated listing block that segment names
                                ; (`dsk_dseg_reloc` is its own relocation
                                ; proc). `asc_take_x` banks and restores ES, so
                                ; what the loop got back after a compaction was
                                ; a STALE segment.
                                ;
                                ; Nothing here reads ES at all - `dsk_get_dir_x`
                                ; takes the segment from `[dsk_dseg]` itself
                                ; and answers a KERNEL-segment pointer,
                                ; `ico_put` takes the body's segment in DX, and
                                ; `ico_ref_set` writes through SS - so the load
                                ; was left over from when this loop wrote icon
                                ; BODIES into the listing. It is not merely
                                ; deleted, though: the icon-index blank above
                                ; ends `pop es`, which restores
                                ; `[dsk_dseg]` from `.scan_done`, so deleting
                                ; it would leave the stale segment in ES and
                                ; only make tools/dsegaudit.py go quiet.
                                ; Overwriting it is what closes the window.
    call dsk_icopass            ; THE SAME CLASSIFICATION the harvest above
                                ; makes, which is why it is one routine: a
                                ; folder takes the built-in body out of
                                ; `.text`, a package already in the store
                                ; takes its row, and a document is pass 4b's
                                ; business. The one thing this pass does
                                ; differently is what it does with a MISS -
                                ; nothing, there being no sector here to go
                                ; and read - so `ico_ref_try`'s carry is
                                ; simply not tested. It is a routine rather
                                ; than a loop here because SPEC.md 25.9.5's
                                ; repair walks it too, over a WINDOW's listing
                                ; instead of a mount's
                                ; **AND THEN IT FALLS THROUGH TO PASS 4b**,
                                ; which is what `jmp short .done` used to skip
                                ; (SPEC.md 62.9.2.1). The reason given for
                                ; skipping it was that a redirected volume has
                                ; no program to have learned an icon from, and
                                ; that is not what pass 4b reads: it composes
                                ; from the ASSOCIATION's glyph, which is
                                ; machine-wide and warm out of the boot
                                ; volume's ASSOC.DAT, so a `.TXT` on the RAM
                                ; disk has exactly the same claim on a Note
                                ; Pad page as a `.TXT` on a floppy. It also
                                ; costs no I/O at all - which is the property
                                ; that lets this arm run it - so the two bytes
                                ; that went were buying a volume with LESS
                                ; than the machine already knew

%endif                          ; OS88_REDIR
.harvested:
%endif                          ; DSK_WANT_ICONS

%ifdef OS88_ASSOC
    call dsk_docpass            ; --- 4b. document icons (SPEC.md 54.3) ------
%endif                          ; kern_small composes none (SPEC.md 54.0)
.done:
    mov [disk_nfiles], bp       ; publish the count last
    mov byte [dsk_lstale], 0    ; ...the listing is this volume's again...
    mov byte [dsk_mntok], 1     ; ...and only now open the write gate
    clc
    jmp .out
    ; --- A REDIRECTED VOLUME: ASK THE DRIVER, READ NO DISK (SPEC.md 62.9) ---
    ; Everything the FAT path does between .cwd_kept and the listing is about
    ; SECTORS - the boot sector, the BPB's seventeen rules, the FAT window,
    ; the root directory's extent - and a DRVC_FILE volume has none of them.
    ; What replaces the lot is one question: stand in this folder, and say
    ; what is above it.
%ifndef KD_BUILD                ; ...and the arm itself (SPEC.md 96.44.9)
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: no volume here
.fsmount:
    mov byte [dsk_spc], 1       ; FSV_DFREE answers BYTES and dsk_free_clus
                                ; answers CLUSTERS, so a redirected volume's
                                ; "cluster" is one sector and both consumers
                                ; of the free figure are untouched (22.7)
    mov ax, [dsk_cwd]           ; the opaque handle (62.9.1); 0 = the root
    mov bp, FSV_CHDIR
    call drv_fs_call
    jc .fail                    ; no DRVC_FILE driver published, or it refuses
                                ; this handle. THE MOUNT FAILS, exactly as an
                                ; unreadable boot sector does and for the same
                                ; reason: nothing below may run against a
                                ; folder we are not standing in
    mov [dsk_fsup], dx          ; the parent's handle, which dsk_synth_up
                                ; spends - there is no '..' on a disk here to
                                ; read one out of (62.9.1)

    cmp byte [dsk_quiet], 0     ; 2b's gate, and 2b's ANSWER: a caller
    jne .fsquiet                ; switching volumes to READ a file never looks
                                ; at the listing, and an EMPTY one is a state
                                ; the whole kernel handles where a stale one
                                ; is not (SPEC.md 18.9)
    cmp word [dsk_dseg], 0      ; **...AND THE SECOND HALF, WHICH THIS COPY DID
    jne .fslist                 ; NOT HAVE** (SPEC.md 22.6.3.1). The FAT path's
.fsquiet:                       ; gate is two tests - no DESTINATION is as quiet
    jmp .quiet                  ; as a quiet mount - and osapi_vol_mount is
                                ; named in 22.6.3's own table as a caller that
                                ; supplies none, which is every driver
                                ; registering a volume. Without it `.scan_done`
                                ; below does `mov es, [dsk_dseg]` on a ZERO and
                                ; the icon blank writes [dsk_nmax] = 64 bytes of
                                ; 0xFF at 0000:0000 - interrupt vectors 0..15,
                                ; int 08h among them. The mount COMPLETES and
                                ; the caller repaints; the machine dies at the
                                ; next tick, which is why the RAM disk's Mount
                                ; button read as a freeze rather than a failure

.fslist:
    mov ax, LOW_SEG             ; dsk_synth_up and dsk_put_dir both stage
    mov es, ax                  ; through the kernel segment, so ES only has
    xor bp, bp                  ; to hold something valid until .scan_done
    cmp word [dsk_cwd], 0       ; reloads it from [dsk_dseg]
    je .fsnoup
    call dsk_synth_up           ; the parent link, type 3, opening the listing
    xor ax, ax                  ; exactly as it does on a FAT volume - built
    call dsk_put_dir            ; before the driver's entries so the sort below
    mov bp, 1                   ; never has to keep it in place (19.5)
.fsnoup:
    mov [dsk_fsn], bp           ; THE DRIVER APPENDS AND THE KERNEL COUNTS
    mov byte [dsk_fslist], 1    ; (62.9.1). osapi_fs_ent is legal only between
    mov bp, FSV_LIST            ; these two stores, because an append outside a
    call drv_fs_call            ; mount would leave disk_nfiles describing a
    mov byte [dsk_fslist], 0    ; listing the sort never saw - and the gate is
    jc .fail                    ; lowered whatever the driver answered, or one
                                ; refusal leaves the cell open all session
    mov bp, [dsk_fsn]           ; what it actually appended, parent link and
    jmp .scan_done              ; all. The sort, the icon blank and the publish
                                ; below are the FAT path's, untouched: two
                                ; places deciding a listing's order is how a
                                ; display index stops meaning what the
                                ; hit-test thinks it means (19.4)
%endif                          ; OS88_REDIR
%endif                          ; KD_BUILD

.fail:
    xor ax, ax                  ; ...and the same here: 15 bytes against 18
    mov [dsk_cwd], ax           ; a failed chdir must not leave the volume
    mov al, 2                   ; pointed inside a directory nothing listed
    mov [disk_heads], ax        ; ...and sane fallback geometry is left behind
    mov al, 9
    mov [disk_spt], ax
    stc
.out:
    jmp kretc_es

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
; the boot sector's BPB (offsets 11..28) is then staged into dsk_bpbh (kernel
; .bss) and every later rule runs on plain DS reads - odd-offset word loads
; are plain 8086 loads, no alignment requirement exists. The BPB is
; HOSTILE: each rule protects a specific downstream consumer (18.2), and
; the two failure modes the table exists to prevent are a divide fault or
; hang with sch_lock held and a read landing outside its buffer.
; -----------------------------------------------------------------------------
dsk_bpb_check:
    call kentc_di               ; KENT: push ax..di





    push es

    ; A BANKED head skips rules 1 and 2 and the staging, and only those (SPEC.md
    ; 18.9.2): it passed rules 1 and 2 when it was banked (both are asked of
    ; the sector, and the jump byte is not banked), the bytes in dsk_bpb ARE
    ; that sector's, and dsk_secbuf now holds something else entirely. Every
    ; rule from 3 down still runs, because re-validating costs no I/O and a
    ; bank that has been corrupted in RAM must not be trusted further than a
    ; sector off the disk. READ-AND-CLEAR: the flag is for exactly one call,
    ; and a leaked 1 would skip rule 1 on a sector that never passed it.
    mov al, [dsk_bpbpre]
    mov byte [dsk_bpbpre], 0
    or al, al
    jnz .staged

    ; rule 1: boot signature 0xAA55 at bytes 510..511
    push ss
    pop es
    cmp word [es:dsk_secbuf+510], 0xAA55
    jne .jfail1

    ; rule 2: BS_jmpBoot[0] in {0xEB, 0xE9} (MS-spec first validity test).
    ; Asked of the SECTOR, beside rule 1, because the jump byte is not staged:
    ; a banked head passed both when it was banked and neither can change in RAM
    mov al, [es:dsk_secbuf+DSK_B_JMP]
    cmp al, 0xEB
    je .r2ok
    cmp al, 0xE9
    jne .jfail1
.r2ok:
    ; stage the BPB fields rules 3..16 read, so they read plain DS (SPEC.md
    ; 18.2) - offsets DSK_B_BPS..DSK_B_BPS+DSK_BPB_SZ-1 and nothing else
    mov si, dsk_secbuf+DSK_B_BPS
    mov di, dsk_bpbh
    mov cx, DSK_BPB_SZ
    call dsk_copy_in_x
.staged:
    mov bh, [dsk_vflop]         ; FOUR rules below ask this and `cmp byte
                                ; [mem],1` is five bytes against `cmp bh,1`'s
                                ; three. BH survives: BX is first written at
                                ; rule 14, dsk_copy_in_x banks everything it
                                ; touches and never names BX, and `mul` writes
                                ; only AX and DX

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
    cmp bh, 1
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
    cmp bh, 1
    jne .r11d
    or ah, ah                   ; {8,9,15,18,21,36} walked as DELTAS in AL,
    jnz .jfail1                 ; where `sub al, imm8` is the two-byte
    sub al, 8                   ; accumulator form and `cmp ax, imm8` was
    je .r12                     ; three. A floppy spt is a byte, which is what
    dec al                      ; makes AL enough - and AX is dead past here,
    je .r12                     ; rule 12 reloading from dsk_bpb
    sub al, 6
    je .r12
    sub al, 3
    je .r12
    sub al, 3
    je .r12
    sub al, 15
    jne .jfail1
    jmp short .r12
.r11d:
    dec ax                      ; 1..63 as one range: the CHS sector field is
    cmp ax, 62                  ; six bits, and 0 wraps to 0xFFFF and fails
    ja .jfail1
.r12:
    mov ax, [dsk_bpb+DSK_B_HEADS]
    dec ax                      ; both arms are "value-1 <= N": a floppy has
    mov cx, 254                 ; one head or two, a driver-backed volume
    cmp bh, 1     ; 1..255, the CHS head field being eight bits.
    jne .r12d                   ; 0 wraps to 0xFFFF and fails, which is the
    mov cx, 1                   ; case the rule exists for. AX is dead past
.r12d:                          ; here and CX is rule 13's own to write
    cmp ax, cx
    ja .jfail1
.r13:
    ; rule 13: on a floppy, TotSec16 <= spt*heads*80, so every in-volume LBA
    ; stays CHS-reachable under disk_read's cyl<80 guard. On a driver-backed
    ; volume the DRIVER declared the partition's length when it registered
    ; the volume, and the question stops being "is this geometry plausible"
    ; and becomes the sharper "does this volume claim to be bigger than the
    ; partition it lives in" (SPEC.md 18.7).
    cmp bh, 1
    jne .r13d
    mov ax, [dsk_bpb+DSK_B_SPT]
    mul word [dsk_bpb+DSK_B_HEADS]  ; AX = spt*heads (<= 72), DX = 0
    mov cx, 80
    mul cx                          ; AX = spt*heads*80 (<= 5760)
    cmp [dsk_bpb+DSK_B_TOT16], ax
    ja .jfail2
    jmp short .r14
.jfail2:                        ; the second relay, .jfail1 having gone out of
    jmp .fail                   ; range half way down. Reached only by a jump,
                                ; like .jfail1: nothing falls into it
.r13d:
    mov ax, [dsk_vsecs]
    test ax, ax
    jz .r14                     ; the driver did not say: nothing to check
    cmp [dsk_bpb+DSK_B_TOT16], ax
    ja .jfail2
.r14:

    ; the raw fields are now trusted: stash them (SPEC.md 18.1)
    mov ax, [dsk_bpb+DSK_B_TOT16]
    mov [dsk_totsec], ax        ; ...and the volume's extent, which §18.95's
                                ; fill clamps against: reading to the end of a
                                ; track near the end of a small volume asks for
                                ; sectors that are not there, and dsk_xfer
                                ; answers that with a cylinder-guard failure
                                ; the caller would read as an unreadable file
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
    jc .jfail2
    mov [dsk_rootlba], ax       ; first root-dir LBA
    add ax, [dsk_rootsecs]
    jc .jfail2
    mov [dsk_datalba], ax       ; FirstDataSec
    ; rule 14: FirstDataSec + SecPerClus <= TotSec16 (>= 1 cluster exists;
    ; DataSec underflow guard)
    mov bl, [dsk_spc]
    xor bh, bh
    add ax, bx
    jc .jfail2
    cmp ax, [dsk_bpb+DSK_B_TOT16]
    ja .jfail2
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
    call dsk_bytes_secs_x       ; ...as whole sectors. DX comes back a copy of
    jmp short .capcmp           ; AX and .capcmp reads AX alone
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

    ; ...and whether a transfer run may cross a HEAD on this machine (SPEC.md
    ; 18.93.1). The boot sector already paid for the answer - a canary over its
    ; own load - so this is a test of one word, not a probe.
    ;
    ; It is a fact about THE MACHINE'S BIOS and not about this volume, which is
    ; why nothing here looks at [disk_spt]. It did, once: the loader published
    ; its run bound and this compared that against the mounted volume's spt.
    ; That reads correctly on the volume the loader loaded from and wrongly on
    ; any other - a 286 booting 1.44MB publishes 18 (a TRACK there), and
    ; mounting a 360KB disk or a 17-sector hard disk made 18 > spt true and
    ; turned the crossing back ON, on the one BIOS class that cannot do it.
    mov byte [dsk_cylrun], 0
    cmp word [boot_cylrun], 0   ; the loader writes that word on ONE path: the
    je .nocyl                   ; one where a run crossed a head and the canary
    mov byte [dsk_cylrun], 1    ; came back right. Zero is the kernel image's
.nocyl:                         ; own value, so "it fell back", "it never had
                                ; cause to look" and "this image predates the
                                ; canary" are one answer and it is the cautious
                                ; one.
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
; dsk_ent_zero - blank the staged directory entry (module-internal)
; out: dsk_ent is 32 zero bytes; CX and DI clobbered, everything else kept
;
; NOT a `rep stosw`: ES is LOW_SEG at both call sites and DS never points
; there (SPEC.md 2.1), which is why the loop is written by hand.
; -----------------------------------------------------------------------------
dsk_ent_zero:
    mov di, dsk_ent
    mov cx, DSK_DE_SIZE/2
.zero:
    mov word [di], 0
    inc di
    inc di
    loop .zero
    ret

; -----------------------------------------------------------------------------
; dsk_synth_name - a raw entry's DISPLAY NAME: stem[.ext], trimmed, sanitized
; in:  ES:SI -> a raw 32-byte directory entry (ES = LOW_SEG)
; out: dsk_ent @0.. holds the name, NOT terminated; DI -> the byte after it,
;      so DI - dsk_ent is its length (0..12)
; clobbers: AX, BX, CX, DI, flags
;
; dsk_synth's name half, on its own because it has TWO callers: the staged
; entry every listing and OSAPI_FILE_FIND record is built from, and
; dsk_byclus's, which names one level of an OSAPI_FILE_PATH answer (SPEC.md
; 19.2.4). One formatter is what makes a path's spelling of a folder the
; same as the listing's, KANJI escape and sanitizer included - the slot
; carried its own 8.3 formatter once, and it was 69 bytes that could disagree.
; -----------------------------------------------------------------------------
dsk_synth_name:
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
    ret                         ; blank ext: the dot is omitted
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
dsk_synth_x:
    push ax
    push bx
    push cx
    push dx
    push di

    call dsk_ent_zero           ; zero the staged entry first: the name's
                                ; NUL padding and bytes 22..31 fall out
    call dsk_synth_name         ; display name @0: stem[.ext], trimmed,
                                ; sanitized (SPEC.md 19)

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

    ; --- @16: type word = 1 iff ext == "O88" AND the file is under 1MB AND
    ;     size >= 1 AND first cluster in [2, dsk_maxclus] (SPEC.md 19) -
    ;     a garbage entry can never reach the loader as type 1
    cmp byte [es:si+DSK_R_EXT], 'O'
    jne .typed
    cmp byte [es:si+DSK_R_EXT+1], '8'
    jne .typed
    cmp byte [es:si+DSK_R_EXT+2], '8'
    jne .typed
    cmp dx, PKG_FILE_HI         ; the file must be under 1MB. It was `test dx,
    jae .typed                  ; dx` - the file had to fit 16 bits - and what
                                ; changed it is that APP_MAX_SIZE no longer
                                ; bounds a FILE (SPEC.md 19.1). It bounds the
                                ; primary segment's image+bss, which is all it
                                ; ever meant. LIFTING THIS ALONE IS A DEFECT:
                                ; a 70KB file's size LOW word is 4,608, a
                                ; plausible small package, so ld_run_body step
                                ; 1 tests the high word before it trusts the
                                ; low one (SPEC.md 21) - the two are one
                                ; change
    mov ax, [es:si+DSK_R_SIZE]  ; ...and not EMPTY, which is BOTH words zero.
    or ax, dx                   ; It was `cmp word [..SIZE], 1 / jb`, right
    jz .typed                   ; while the high word had to be 0 and wrong
                                ; the moment it did not: a parts package of
                                ; exactly n x 65,536 bytes has a low word of
                                ; 0 and was typed 0 - not a package - here,
                                ; while ld_take's own test (`or cx,cx / jnz
                                ; / or dx,dx / jz .bad`) launched it by name
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
    call dsk_ent_zero           ; the name's NUL padding and the unused tail
    mov word [dsk_ent+0], '..'  ; both fall out of it; then two bytes
    mov word [dsk_ent+16], 3    ; type 3 = the parent link (SPEC.md 19)
    cmp byte [dsk_vkind], DVK_FILE
    je .fsup                    ; a REDIRECTED volume carries no '..' entry on
    call dsk_dotdot_x           ; a disk to read one out of, so FSV_CHDIR
    jnc .have                   ; answered it and disk_mount banked it in
    xor ax, ax                  ; [dsk_fsup] (SPEC.md 62.9.1). The handle is
    jmp short .have             ; opaque, so this is the ONLY source there is
.fsup:
    mov ax, [dsk_fsup]
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
    call kentc_di               ; KENT: push ax..di





    cmp cx, 2
    jb .out                     ; 0 or 1 entries are already in order
    mov ax, cx
    mov cx, DSK_DE_STRIDE       ; count * DSK_DE_STRIDE. A MULTIPLY, because
    mul cx                      ; the stride is no longer a power of two -
                                ; count <= DSK_NENT so DX is 0, and DX is
                                ; overwritten on the next line anyway. Every
                                ; register here is banked at entry
    add ax, bx
    mov dx, ax                  ; DX = one past the last entry
    mov cx, dx
    sub cx, DSK_DE_STRIDE       ; CX = the last entry: no choice to make for
                                ; it once everything below is placed
.outer:                         ; BX = the slot being filled
    cmp bx, cx
    jae .out
    mov di, bx                  ; DI = the smallest name seen so far
    mov si, bx
.inner:
    add si, DSK_DE_STRIDE       ; SI = the candidate
    cmp si, dx
    jae .placed
    call dsk_ncmp
    jae .inner                  ; not smaller: keep looking
    mov di, si
    jmp short .inner
.placed:
    cmp di, bx
    je .next                    ; already in place: an entry not moved
    mov si, bx
    call dsk_swapent
.next:
    add bx, DSK_DE_STRIDE
    jmp short .outer
.out:
    jmp kretc_di

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
    mov cx, DSK_DE_STRIDE
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
; dsk_peek_x - the head of a file, whichever transport (SPEC.md 20.14.6.2)
; in:  AX = its first cluster on a FAT volume, or THE DRIVER'S OPAQUE HANDLE
;      on a redirected one (SPEC.md 62.9.1); CX = how many bytes, <= 512
; out: CF=0 - AX bytes of the file, from its byte 0, at LOW_SEG:dsk_secbuf,
;      with ES = LOW_SEG and BX = dsk_secbuf. AX is what ARRIVED and may be
;      less than CX. CF=1 - the transport refused.
; clobbers: AX (the output), BX, CX, ES, CF - DX, SI, DI and BP are all
;           preserved, which the icon harvest's entry index depends on
;
; THREE PLACES WANTED THE SAME NINE INSTRUCTIONS and each had written them
; out: the loader's header peek (SPEC.md 21 step 2), the icon harvest's
; (62.9.2.2) and the compression sniff's (20.14.6). They differ in ONE thing
; and it is not the transport - it is how much of a short answer each will
; take, so THE COUNT IS AN OUTPUT and the strictness stays at the call site.
;
; **THE FAT ARM READS A WHOLE SECTOR WHATEVER IT WAS ASKED FOR**, so it
; answers CX and the slack past EOF comes free - unrelated bytes that every
; caller's own magic test throws out. A redirected volume has no sectors and
; delivers exactly what is there, which is why the harvest and the sniff
; compare: a short answer would leave the PREVIOUS entry's bytes in the tail
; of the buffer, and those are VALID, so a 40-byte file would take the icon
; of the package above it in the sort. The LOADER deliberately does not
; compare - build/filler.o88 is 370 bytes, so a package shorter than the 512
; it asks for is an ordinary thing and `ld_check_hdr` is what judges it.
;
; It costs no extra `int 13h` on the FAT arm: the peek is a one-sector
; `disk_read`, SPEC.md 18.95's cache fills the slot to the end of the track,
; and the data read that follows comes straight back out of it.
; -----------------------------------------------------------------------------
dsk_peek_x:
%ifdef OS88_REDIR               ; DX, SI, DI and BP are preserved on BOTH
    push dx                     ; builds and only ONE has to work at it: the
    push si                     ; FAT arm below spends none of them
    push di                     ; (dsk_clus2lba_x and dsk_xfer both push what
    push bp                     ; they touch), so the four saves are the
%endif                          ; REDIRECTED arm's - DX is the segment it
                                ; hands the verb, DI:SI the 32-bit offset and
                                ; BP the verb itself. The contract is the same
                                ; sentence either way; it is 8 bytes of
                                ; kern_small that is not
    push cx                     ; ...and the asked count, which is the
                                ; driver's to spend and which both arms answer
    push ss                     ; ES = LOW_SEG, for BOTH arms and for the way
    pop es                      ; out (SPEC.md 2.1)
%ifdef OS88_REDIR
    cmp byte [dsk_vkind], DVK_FILE
    jne .fat
    mov dx, ss                  ; DX:BX and never ES:BX (SPEC.md 51.8) - this
    mov bx, dsk_secbuf          ; site wants LOW_SEG in a general register
    xor si, si                  ; DI:SI = offset 0, in 32 bits: the head of
    xor di, di                  ; the file
    mov bp, FSV_READAT
    call drv_fs_call            ; ...and it restores ES, so the way out below
    jmp short .out              ; needs only BX. AX is the count it delivered
.fat:
%endif
    call dsk_clus2lba_x         ; the first sector of the first cluster IS the
    jc .out                     ; file's first 512 bytes, so peek semantics
    call dsk_rd1_x              ; are exact (SPEC.md 21 step 2)
    pop ax                      ; ...and a whole sector arrived, so the answer
    push ax                     ; is the whole ask. CF is dsk_rd1_x's still
.out:
    mov bx, dsk_secbuf          ; the contract's other half, and neither this
    pop cx                      ; nor the pops touch a flag
%ifdef OS88_REDIR
    pop bp
    pop di
    pop si
    pop dx
%endif
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
dsk_clus2lba_x:
    push dx
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    dec ax
    dec ax
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
    call dskw_flush_x             ; CF is not propagated: a flush that fails has
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
    jc .homeless
    mov ax, [bx+DVOL_MAX*2]     ; the claim, or FAT_SEG for the pin; the saved
    or ax, ax                   ; sector is at [bx]
    jz .homeless                ; no home at all - dsk_fatw_want makes this
    mov [dsk_fatseg], ax        ; unreachable, and .homeless is why that is
                                ; safe to rely on rather than to assert
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
.homeless:                      ; ABOUT TO WRITE THE KERNEL'S OWN WINDOW
    call dsk_fatw_evict         ; WITHOUT HOLDING THE PIN (SPEC.md 18.8.3), so
                                ; nobody may go on believing they hold it: the
                                ; holder's dsk_fatww would still say "sector N
                                ; resident" and its next quiet mount would
                                ; reuse another disk's FAT. dsk_fatw_want does
                                ; not let this happen; the invariant is
                                ; enforced here as well because THIS is the
                                ; routine that would do the damage
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
; **THE KERNEL'S OWN WINDOW IS TRIED FIRST AND HAS NO GATE** (SPEC.md 18.8.3).
; FAT_SEG is 4,608 bytes reserved on every machine whether anything uses them
; or not, so a volume that takes them spends nothing - where the same 4,608
; bytes bought out of the arena are a real claim. Only when it is already
; pinned does the gate below get asked, and only when the heap refuses as well
; does this volume EVICT the pin and take it - which is exactly what used to
; happen to those bytes when a claim was refused, except that the holder is
; now told.
;
; THERE IS NO THIRD STATE. This routine returns with the mounting volume
; holding a claim or holding the pin, never neither, and dsk_fatw_pick relies
; on it: a homeless volume would be pointed at FAT_SEG and mounted into it
; WITHOUT demoting whoever thinks they hold it, and 18.8.2's signature cannot
; catch that - two os8088-built floppies of one geometry have byte-identical
; boot sectors by construction.
%ifdef FATW_GATE
DSK_FATW_MINK equ FATW_GATE      ; ...moved by the knob, for the tests that
%else                            ; have to reach the steal (SPEC.md 18.8.3)
DSK_FATW_MINK equ ((DSK_FAT_SECS*512 + 1023) / 1024) * 2   ; ...and it is
                                ; TWICE THE CLAIM now, not 128KB (SPEC.md
                                ; 18.8.4). What the big number was defending is
                                ; defended by the tag instead: the window is a
                                ; cache, so a machine that needs the room back
                                ; TAKES it. What is left of the gate is the
                                ; part that was always right - mem_avail_lvl's
                                ; bottom rank, so a window is still only taken
                                ; out of memory that is free without shedding
                                ; anybody for it
%endif

dsk_fatw_want:
    push ax
    push bx
    push cx
    push dx
    mov bl, [disk_drive]        ; EVERY volume, not just the two floppies:
    call dsk_fatw_slot          ; the driver volumes' claim at osapi_vol_add
    jc .out                     ; is their only one, so a refusal there used
    cmp word [bx+DVOL_MAX*2], 0 ; to be PERMANENT where a floppy retried
    jne .out                    ; every mount (SPEC.md 18.8.3)
    call dsk_fatw_pinned        ; the kernel's own window is free? Take it -
    jc .pin                     ; it is already paid for, so there is no gate
    push bx                     ; mem_avail_lvl ANSWERS in BX, so the slot
    mov al, MEM_PG_MED          ; goes on the stack rather than being read
    call mem_avail_lvl_x          ; twice. ...AT OUR OWN RANK (SPEC.md 50.6.4),
    cmp bx, DSK_FATW_MINK       ; which is dsk_rah_want's rule and now ours: a
    pop bx                      ; CHEAPER cache counts as free and a dearer one
    jb .evict                   ; does not. So a FAT window may have the window
                                ; raise caches - one window redrawing when it
                                ; is raised, which is what every window did
                                ; before 11.96 - and MEM_P_DIRW at HIGH may
                                ; quietly have this. It asked at MEM_PG_MIN for
                                ; one commit, which counted NOTHING as free and
                                ; made the window more timid than its own rank
                                ; entitles it to be. AX = largest run KB, BX =
                                ; TOTAL free KB, and pop leaves the cmp's flags
    mov al, [disk_drive]
    call dsk_fatw_claim         ; a refusal is a normal path (SPEC.md 50) and
    cmp word [bx+DVOL_MAX*2], 0 ; falls through to the pin below. BX survives:
    jne .out                    ; claim preserves it, and so does evict
.evict:                         ; the heap said no: TAKE THE PIN (18.8.3). The
    call dsk_fatw_evict         ; victim is never the volume being mounted -
.pin:                           ; that one has a home and never reaches here -
                                ; and never dirty, because dsk_fatw_park
                                ; flushes the outgoing volume at the top of
                                ; every mount
    mov word [bx+DVOL_MAX*2], FAT_SEG
    mov word [bx], 0xFFFF       ; nothing of OURS is in it yet, whatever the
.out:                           ; bytes hold
    jmp kretc_dx

; -----------------------------------------------------------------------------
; dsk_fatw_pinned / dsk_fatw_evict - the kernel's own window (SPEC.md 18.8.3)
;
; pinned: out CF=0 = some volume holds FAT_SEG, CF=1 = it is free
; evict:  out nothing (every register preserved, flags clobbered) - the holder
;         is demoted to homeless, which is where every volume stood before
;         18.8.1, and both its words go: a cleared dsk_fatwc with a live
;         dsk_fatww behind it is the zombie window dsk_fatw_park describes
;
; BY VALUE and not by a second index word, for dsk_fatw_reloc's reason: two
; words naming one fact is how a window ends up reading the right bytes from
; the wrong place. dsk_fatwc[v] IS the record, and FAT_SEG in it means the pin.
; Neither routine can collide with a heap base - the arena starts above
; LOW_SEG, which is above FAT_SEG - which is the same fact that lets
; dsk_fatw_reloc scan the array without a guard.
;
; evict re-scans rather than taking a pointer from pinned, so no register has
; to survive the mem_claim in between; that call can COMPACT, and reasoning
; about which pointers a compaction leaves alone is the kind of argument this
; file should not have to make twice.
; -----------------------------------------------------------------------------
dsk_fatw_pinned:
    push cx
    push si
    mov si, dsk_fatwc
    mov cx, DVOL_MAX
.scan:
    cmp word [si], FAT_SEG
    je .out                     ; equal leaves CF=0, which IS the answer; the
    inc si                      ; pops below do not touch flags
    inc si
    loop .scan
    stc
.out:
    pop si
    pop cx
    ret

dsk_fatw_evict:
    push cx
    push si
    mov si, dsk_fatwc
    mov cx, DVOL_MAX
.scan:
    cmp word [si], FAT_SEG
    jne .next
    mov word [si], 0                    ; homeless: no claim, and...
    mov word [si-DVOL_MAX*2], 0xFFFF    ; ...nothing of theirs resident
.next:
    inc si
    inc si
    loop .scan
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dsk_fatw_demote - this volume's FAT window is being SHED (SPEC.md 18.8.4)
; in:  BX = the owner word, MEM_P_FATW + the volume
; out: nothing (all registers preserved)
;
; mem_pg_forget hands the owner here instead of zeroing dsk_fatwc[v], because
; the purge protocol carries ONE naming word and the LIVE window has three.
; Clearing the record is still this routine's job - it just does the other two
; as well.
;
; A BANKED window stops at the clear: nothing else names it, and 18.8.3's
; dsk_fatw_want gives that volume a home again at its next mount - the pin if
; it is free, a fresh claim if the heap will fund one, the pin taken from its
; holder otherwise.
;
; THE LIVE ONE HANDS THE VOLUME THE PIN ON ITS WAY OUT. The bytes are lost
; either way, which is what a cache is; the HOME must not be, or 18.8.3's
; "there is no third state" ends right here - dsk_fatw_pick would point this
; volume at FAT_SEG and mount into it WITHOUT demoting whoever holds the pin,
; and 18.8.2's signature cannot catch that between two os8088-built floppies.
; [dsk_fatw0] going to 0xFFFF is the other half: dsk_fat_window would otherwise
; find the sector it wants already resident and return without loading it.
;
; It runs inside mem_shed_one's cli, so it is word stores and nothing else - no
; I/O, no claim, no flush. mem_fatw_dirty is what makes that safe: a window
; with FAT edits still in it never reaches here.
; -----------------------------------------------------------------------------
dsk_fatw_demote:
    push ax
    push bx
    mov ax, bx
    sub ax, MEM_P_FATW          ; AX = the volume this owner names
    mov bl, al
    call dsk_fatw_slot
    jc .out
    mov ax, [bx+DVOL_MAX*2]     ; the claim that is going...
    mov word [bx+DVOL_MAX*2], 0 ; ...and mem_pg_forget's own store, done here
    mov word [bx], 0xFFFF       ; because it does not make the other two
    cmp ax, [dsk_fatseg]
    jne .out                    ; BANKED: no other word ever named it
    call dsk_fatw_evict         ; LIVE: take the pin off whoever holds it -
    mov word [bx+DVOL_MAX*2], FAT_SEG   ; which is never this volume, its
    mov word [dsk_fatseg], FAT_SEG      ; record having just been cleared -
    mov word [dsk_fatw0], 0xFFFF        ; and move in
.out:
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
%ifdef FATW_NONE
    stc                         ; the knob: every heap window refused, so the
    jmp short .none             ; whole machine runs on the pin and the
%endif                          ; ping-pong is what a test sees (18.8.3)
    mov bx, MEM_P_FATW          ; ONE OWNER PER VOLUME (SPEC.md 18.8.4): the
    add bl, al                  ; range's base plus the index, which is what
                                ; makes dsk_fatwc the array mem_pg_own names.
                                ; BEFORE THE SIZE GOES INTO AX, which is where
                                ; the volume still is - written the other way
                                ; round it tagged every window with the CLAIM'S
                                ; KB instead, and 5 KB is a legal volume index:
                                ; volume 1 carried owner FC25, so a shed would
                                ; have zeroed volume 5's record and left B:
                                ; naming freed memory (tests/fatwpin.py)
    mov ax, (DSK_FAT_SECS*512 + 1023) / 1024
    call mem_claim_x              ; out: DX = the segment; CF=1 = refused
    jc .none
                                ; NOT mem_movable. mem_can_move refuses a
                                ; purgeable cache outright (SPEC.md 50.6.1), so
                                ; a relocation proc here was dead code the
                                ; moment the tag changed - and 66.5.6's reason
                                ; for wanting one is spent twice over: the
                                ; compactor's mem_cp_drop dissolves a cache
                                ; instead of moving it, and since 18.8.3 the
                                ; boot volume takes the PIN rather than a
                                ; claim, so there is no first bottom-up claim
                                ; of the boot to wall the arena's floor in
    pop bx
    mov [bx], dx
    jmp short .out
.none:
    pop bx
    mov word [bx], 0
.out:
    jmp kretc_dx

; dsk_fatw_reloc lived here: MEM_K_FATW's relocation proc, deleted with the tag
; (SPEC.md 18.8.4). mem_can_move refuses a purgeable cache outright, so a
; relocation proc for a FAT window can never be reached - and 66.5.6's reason
; for having one is spent twice over: the compactor's mem_cp_drop dissolves a
; cache instead of moving it, and since 18.8.3 the boot volume takes the pin
; rather than a claim, so there is no first bottom-up claim of the boot walling
; in the arena's floor. What it knew that this file must not forget is that the
; live window has a SECOND naming word, [dsk_fatseg]; dsk_fatw_demote below is
; where that now lives.
; dsk_dseg_reloc lived here: the KERNEL's half of a DONATED claim's move
; (SPEC.md 66.5.10.2), later two rows of mem_rr_tab, and now NOTHING - SPEC.md
; 22.6 retired the donation, so no listing is ever in a claim and [dsk_dseg] is
; LOW_SEG for the life of the machine. Both rows are out of mem_rr_tab with it.
section .cold                   ; ...and back, or every routine below joins it

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
    cmp dx, FAT_SEG             ; THE PIN IS NOT A CLAIM (SPEC.md 18.8.3) and
    je .out                     ; there is no record to free. mem_free happens
                                ; to refuse it anyway - it scans mem_tab for
                                ; MC_SEG = DX and no record can hold a segment
                                ; below HEAP_SEG - but that is a coupling to
                                ; the ladder's layout, not a contract
    mov bx, MEM_P_FATW          ; the owner is the range's base plus the
    add bl, al                  ; volume (SPEC.md 18.8.4), and it has to be the
                                ; SAME arithmetic the claim did - AL is
                                ; untouched here, where in dsk_fatw_claim the
                                ; size very nearly took it
    call mem_free_x               ; DX = the segment, BX = the owner word
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_fc_bad - dsk_free_clus_x's first three refusals share this one near jump.
; An 8086 has no near conditional jump, so NASM spells one that cannot reach as
; `Jcc $+5 / jmp near` - five bytes instead of two - and `.bad` is past 240
; bytes of cluster walk.  From here each is two bytes and this is the ONE near
; jump: -6.  It sits after the routine above's `ret`, on a path nothing falls
; into, and it is a global rather than a `.local` because a NASM local here
; would belong to THAT routine; tools/stkbalance.py reads a global that is only
; ever jumped to as a continuation and does not walk it from depth zero.  The
; arms are cold: no mounted volume, a file-system driver refusing, and a FAT
; window that would not load.
; -----------------------------------------------------------------------------
dsk_fc_bad:
    jmp dsk_free_clus_x.bad

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
dsk_free_clus_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [dsk_mntok], 1
    jne dsk_fc_bad
    cmp byte [dsk_vkind], DVK_FILE
    jne .fat

    ; --- a REDIRECTED volume: ASK, do not count (SPEC.md 62.9) --------------
    ; There is no FAT to walk, and FSV_DFREE answers in BYTES where this
    ; routine answers in CLUSTERS. Both of its consumers - fm_measure's status
    ; figure and dskw_dfree_x - multiply what comes back by [dsk_spc], so the
    ; redirected mount sets [dsk_spc] = 1 and this answers SECTORS. Neither
    ; consumer changes by a byte, and the two cannot drift, which is the whole
    ; reason there is one counting body (SPEC.md 22.7).
    push bp                     ; drv_fs_call clobbers BP, and this routine's
    mov bp, FSV_DFREE           ; contract is that it clobbers AX and CF
    call drv_fs_call            ; -> DX:AX = free bytes, BX = the granule
    pop bp
    jc dsk_fc_bad
    mov cx, 9                   ; bytes -> sectors, in 32 bits
.fsshr:
    shr dx, 1
    rcr ax, 1
    loop .fsshr
    or dx, dx
    jz .fsok
    mov ax, 0xFFFF              ; more free space than a word of sectors can
.fsok:                          ; carry. SATURATE rather than wrap: 32MB is
    mov di, ax                  ; already past what the status line's KB
.jcounted:
    jmp .counted                ; figure means, and a wrapped one would read
.slowj:                         ; ...and the FAT12 fast path's two vetoes:
    jmp .slow                   ; the window IS whole on every floppy, so
                                ; this is the arm that does not run (-3)
                                ; as a nearly full volume on the emptiest
                                ; possible remote disk
.fat:
    xor di, di                  ; DI = free clusters
    mov si, 2                   ; SI = cluster under test
    cmp si, [dsk_maxclus]
    ja .jcounted                ; a volume with no data clusters
    mov ax, si                  ; make the window cover cluster 2, so the
    call dsk_fat_ofs_x            ; residency test below reads a loaded window
    jc dsk_fc_bad               ; rather than the 0xFFFF of a fresh mount
    cmp byte [dsk_fattype], 0
    jne .f16

    ; --- FAT12: two entries per three bytes, one word load for both. Needs
    ;     the WHOLE FAT resident, because a pair straddles wherever it likes;
    ;     that is the degenerate window every floppy has (SPEC.md 18.8) ------
    mov ax, [dsk_fatwn]
    cmp ax, [dsk_fatsz]
    jb .slowj
    cmp word [dsk_fatw0], 0     ; ...and the window actually sitting at 0
    jne .slowj
    mov cx, [dsk_fatseg]
    inc byte [sch_lock]         ; **THE WINDOW IS A CACHE NOW** (SPEC.md
    mov es, cx                  ; 18.8.4), and ES is banked for the whole walk
    mov bx, 3                   ; below. While it was MEM_K_FATW nothing could
                                ; move or shed it and the bank was safe; a
                                ; purgeable one is shed by any ordinary claim
                                ; from any other task, and mem_fatw_dirty does
                                ; not cover a COUNT - [dsk_fatd0] is 0xFFFF
                                ; here. So the count would be read out of
                                ; whatever took the paragraphs, and it is the
                                ; free-space figure the file manager prints and
                                ; FTPd answers with. dsk_xfer's own hold is the
                                ; same lock and nests; there is no I/O in this
                                ; loop, so the hold is arithmetic only
.pair:
    mov ax, [es:bx]             ; b0 | b1<<8 - the even entry's 12 bits are
    mov dx, ax                  ; the bottom of it, the odd entry's low
    and dx, 0x0FFF              ; nibble the top
    jnz .odd
    inc di
.odd:
    inc si
    cmp si, [dsk_maxclus]
    ja .c12                     ; an odd cluster count ends on the even half
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
.c12:
    dec byte [sch_lock]         ; ...and ES is nobody's again
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
    call dsk_fat_ofs_x            ; BX = offset in the window, and the sector
    jc .counted                 ; holding cluster SI is now resident
    mov cx, [dsk_fatseg]
    inc byte [sch_lock]         ; the same bank and the same reason, one FAT
    mov es, cx                  ; SECTOR at a time - released below before the
    mov cx, si                  ; next dsk_fat_ofs_x, which does disk I/O and
                                ; must not run inside this hold
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
    dec byte [sch_lock]
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
    call dsk_next_clus_x
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
dsk_next_clus_x:
    push bx
    push cx
    push dx
    push es
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    call dsk_fat_ofs_x            ; BX = the offset INSIDE THE WINDOW, and the
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

dsk_dirw_start_x:
    mov [dsk_wclus], ax
    mov word [dsk_wsec], 0
    mov word [dsk_wguard], DSK_DIRW_MAX
    ret

dsk_dirw_next_x:
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
    call dsk_next_clus_x          ; CF=1 only if the CURRENT link is invalid
    jc .end
    mov [dsk_wclus], ax         ; the new link is validated by clus2lba
    mov word [dsk_wsec], 0
.have:
    mov ax, [dsk_wclus]
    call dsk_clus2lba_x           ; CF=1 = outside [2, dsk_maxclus]: the end
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

; =============================================================================
; THE SECTOR CACHE (SPEC.md 18.95)
;
; One cache, under `disk_read`, serving every read in the machine: directory
; walks, the FAT window's loads, file data, the loader, the icon harvest. On a
; miss it reads **to the end of the track** rather than the sectors asked for,
; and hands the surplus to whoever wants them next.
;
; **The unit the media charges in is the revolution, and the whole of this
; follows from that.** A CHS `int 13h` costs one to two revolutions near enough
; whatever it moves (PERFORMANCE.md), and a track is 9 sectors on the machine
; this project is calibrated against, so reading 4 sectors and reading 9 cost
; about the same and the other 5 are free until they are evicted. §18.4.2's
; "reading ahead can cost work rather than save it" is true in SECTORS and
; false in revolutions.
;
; **It replaces §19.2.3's directory-only window, which is where it started.**
; That version sat ABOVE disk_read inside dsk_dirw_get and could only ever
; help a directory walk; simulated against the same access pattern the two
; answer identically, so the one that also covers file data is the one to
; keep. Consolidating it also removes the question of which of two caches a
; sector is in.
;
; **Simulated before it was built** (§18.94.3): the whole install's 316 calls
; were replayed on the host against several policies, and the answer is
; monotone in the chunk length - a 2-sector chunk is 446 calls, WORSE than no
; cache; 4 is 273; 6 is 190; a full track is **102**. Eight slots is the knee
; (16 buys 4 more calls for twice the memory). Measured afterwards on the
; guest, so the simulation is checkable rather than a story.
;
; Five rules hold it up.
;
; **The cache mirrors the DISK, and every disk write invalidates it.** Not the
; FAT window, not a listing, not anything held in RAM above it: `dsk_rah_wr`
; hangs off `disk_write` - the one routine every sector that changes on the
; platter passes through - so there is no invalidation for a future write to
; forget. A RAM-side buffer that is dirty and unflushed is not this layer's
; business; when it is flushed, the flush is a write and the write invalidates.
;
; **The key is §18.8.1's**: the volume index plus `[dsk_sigcur]`, the
; position-sensitive sum of the boot sector this mount read (§18.8.2). A
; switch to another volume and back therefore keeps the cache, a disk swapped
; for a different one loses it, and the residual - two os8088 disks of the same
; geometry, whose boot sectors os88disk.py makes byte-identical - is the one
; that section already accepts and states.
;
; **A full mount drops everything and a quiet one does not.** A full mount is
; a navigation or a Refresh, and Refresh answered out of a cache is a no-op,
; which is the one thing it must never be.
;
; **The fill re-enters `dsk_xfer`**, with `[dsk_rah_busy]` stopping it caching
; its own fill. That is why the fill needs no CHS conversion, no retry ladder
; and no EOT handling of its own: it asks for a normal read into the cache's
; own claim, and the one transfer routine does what it always did. That
; includes `dsk_runcap`: the claim takes NO 64KB page head (§18.95.7), so a
; slot can straddle a physical page, and a fill into one is then TWO calls
; cut at the boundary - which slots do is wherever the heap put the claim.
;
; **Reads only, BIOS volumes only.** A write must reach the platter. And a
; `DVK_DRV` volume leaves `dsk_xfer` before the run loop for its driver, which
; splits its own runs (§52.1) and has no revolution to save.
; =============================================================================
DSK_RAH_SECS equ 9               ; sectors per chunk: ONE REVOLUTION on the
                                 ; 9-sector floppies this project is
                                 ; calibrated against. The simulation is
                                 ; monotone in this and the track is the
                                 ; ceiling - a CHS call cannot cross one
DSK_RAH_RUNS equ 7               ; ...and how many chunks are kept. IT WAS 14,
                                 ; the segment-offset ceiling the assert below
                                 ; derives, chosen on the INSTALL trace alone:
                                 ; 1 slot is 156 calls, 2 is 123, 4 is 108, 8
                                 ; is 102, 14 is 93. §18.95.6 measured a
                                 ; SECOND workload of a different shape - a DOS
                                 ; program loading off a floppy, which opens
                                 ; few files and reads them in big pieces - and
                                 ; the curve there is FLATTER: 4 slots is 71%
                                 ; of the saving where the install trace's own
                                 ; low end reads 93%, and 7 is 87%.
                                 ;
                                 ; So the five calls between 7 and 14 buy 31KB,
                                 ; and three things want them more. The DOS
                                 ; program, whose arena this comes straight out
                                 ; of (§96.24's heap map is 419.0K of arena
                                 ; with the full cache sitting under it). Every
                                 ; machine between the old 126KB free-run bar
                                 ; and the new 64KB one, which got NO cache at
                                 ; all - §18.95.4 named that band as what the
                                 ; ceiling costs. And the PLACEMENT: the whole
                                 ; claim is its own 64KB DMA head, so a 63KB
                                 ; block may only begin in the first 1KB of a
                                 ; physical page - ten legal bases in a 640KB
                                 ; machine - where a 32KB one may begin
                                 ; anywhere in the first half of one.
                                 ;
                                 ; §18.95.4, §18.95.6
DSK_RAH_KB   equ (DSK_RAH_SECS * 512 * DSK_RAH_RUNS) / 1024   ; the CEILING's
                                 ; size. The live width is [dsk_rah_runs],
                                 ; solved for from the machine (SPEC.md
                                 ; 18.95.5) - this is what n = 14 costs
DSK_RAH_MIN  equ 2               ; ...and the FLOOR, in slots. Below this the
                                 ; walk's own metadata - a directory sector,
                                 ; the FAT window's loads - takes so much of
                                 ; the cache that little is left for the file:
                                 ; sysbench measures the EFFECTIVE width at 12
                                 ; against a compiled 14, and that overhead
                                 ; does not shrink with the cache.
                                 ;
                                 ; **IT WAS FOUR AND IS TWO** (SPEC.md
                                 ; 18.95.5.3): four is 18KB of claim behind a
                                 ; 36KB bar and 93% of what the ceiling saves,
                                 ; and TWO is 9KB behind an 18KB bar and 87%
                                 ; - 18.95.4's own simulation, which the floor
                                 ; was chosen off and which says the rung
                                 ; below it is worth having. `kern_dos` has
                                 ; said so for a cycle: KD_RAH_L2 is 2 and its
                                 ; comment is *"a cache that is not worth
                                 ; having beats one that is not there"*
                                 ; (96.44.11.4), a judgement the kernel's own
                                 ; floor was making the other way on the
                                 ; machines that need it most.
                                 ; tests/dirwsize.py is the row
%if DSK_RAH_MIN < 2 || DSK_RAH_RUNS * DSK_RAH_SECS * 512 >= 65536
  %error "dsk_rah_fill skips the one slot a 64KB page boundary crosses and takes the next (SPEC.md 18.95.7.1): that needs a claim under 64KB, so at most ONE slot straddles, and a width of at least two, so the next slot is a different one - or the pick loops for ever"
%endif
DSK_RAH_SHARE equ 4              ; ...and the OTHER bound: the cache may be at
                                 ; most 1/DSK_RAH_SHARE of the WHOLE ARENA,
                                 ; not merely half of what happens to be free
                                 ; (SPEC.md 18.95.5.2). The free-run rule
                                 ; alone reads the machine at ONE INSTANT -
                                 ; the mount that finds no claim, which on a
                                 ; big machine is the one at boot - so it was
                                 ; effectively asking how much RAM the machine
                                 ; HAS. This asks that question on purpose and
                                 ; bounds the answer
%if DSK_RAH_SHARE != 4
  %error "dsk_rah_want halves the arena and then divides by 9, which is \
n <= heap / (SHARE * 4.5) for SHARE = 4 alone. Another share wants another \
shift, and a constant nothing reads is worse than no constant"
%endif
DSK_RAH_AUTO equ 0xFF            ; ...and the THIRD bound, which is not the
                                 ; kernel's at all: OSAPI_DSK_CACHE (SPEC.md
                                 ; 18.95.8) lets a program that is about to
                                 ; take the machine say how much of the cache
                                 ; it is willing to leave standing. This value
                                 ; in [dsk_rah_cap] means NOBODY HAS, and it
                                 ; is 0xFF rather than 0 because 0 is a real
                                 ; command - "hold nothing" - and the
                                 ; do-nothing default may not collide with an
                                 ; instruction. It is deliberately ABOVE
                                 ; DSK_RAH_RUNS so the solve needs no special
                                 ; case: an uncommanded cap loses every
                                 ; compare it is in
%if DSK_RAH_AUTO <= DSK_RAH_RUNS
%error "DSK_RAH_AUTO is the no-command sentinel in [dsk_rah_cap] and is read \
as a plain ceiling, so it has to sit strictly above the widest legal width or \
an uncommanded machine silently caps itself at the sentinel"
%endif

RAH_LBA equ 0                    ; one slot: the chunk's first LBA, how many
RAH_N   equ 2                    ; sectors of it are here (0 = the slot says
RAH_SIG equ 4                    ; nothing), and §18.8.1's key - the mount
RAH_VOL equ 6                    ; signature and the volume index
RAH_REC equ 9
%if RAH_REC != DSK_RAH_SECS
%error "dsk_rah_serve derives a slot's byte offset with ONE shift, which needs \
the record stride and the chunk length to be the same number"
%endif
%if DSK_RAH_SECS * 512 != 4608
%error "dsk_rah_want sizes the claim as (n*9+1)>>1 KB and solves the gate as \
n = free/9, and BOTH assume a chunk is 4,608 bytes - ceil(n * 4.5KB) and \
2 x n x 4.5KB. Change DSK_RAH_SECS and those two lines are silently wrong in \
the direction that matters: a claim SHORTER than the width it was told to \
hold, so dsk_rah_fill writes past the end of it (SPEC.md 18.95.5)"
%endif
%if DSK_RAH_MIN < 1 || DSK_RAH_MIN > DSK_RAH_RUNS
%error "DSK_RAH_MIN is the floor in SLOTS and has to sit inside [1, \
DSK_RAH_RUNS]: at 0 dsk_rah_want would claim nothing and then scan it, and \
above the ceiling it can never be satisfied so the cache is dead code"
%endif
%if DSK_RAH_RUNS * DSK_RAH_SECS > 128
%error "the WHOLE cache is addressed by one 16-bit offset from dsk_rah_seg \
(dsk_rah_serve shifts slot*SECS+delta left by 9), so RUNS*SECS*512 must fit a \
segment: RUNS*SECS <= 128. At SECS=9 that is 14 runs, and 15 would wrap the \
offset silently - dsk_rah_serve would copy a sector from the front of the \
cache and nothing would report it. More than 63KB needs a segment per slot. \
THE SHIPPED WIDTH IS BELOW THIS CEILING AND IS A DECISION (§18.95.6), so this \
assert no longer picks it: putting DSK_RAH_RUNS back to 14 would assemble, and \
would spend 31KB of every machine to save five int 13h"
%endif
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


dsk_rah_runs: dw 0               ; how many slots the LIVE claim has (SPEC.md
                                 ; 18.95.5), <= DSK_RAH_RUNS. Meaningful only
                                 ; while dsk_rah_seg is non-zero, and .text
                                 ; with a real initialiser for its neighbour's
                                 ; reason: -f bin zeroes no .bss and this
                                 ; bounds a write
dsk_rah_seg:  dw 0               ; the claim, 0 = none. In .text with a real
                                 ; initialiser: -f bin zeroes nothing, this is
                                 ; read before anything sets it, and it is the
                                 ; word mem_pg_own names
dsk_rah_busy: db 0               ; ...and 1 while a fill is in dsk_xfer, so the
                                 ; fill's own read is not itself cached
dsk_rah_cap:  db DSK_RAH_AUTO    ; A CEILING SOMEBODY ELSE SET (SPEC.md
                                 ; 18.95.8): DSK_RAH_AUTO = nobody has, which
                                 ; is the behaviour every machine that never
                                 ; calls the slot has. Otherwise it is a width
                                 ; in SLOTS and dsk_rah_want's solve takes it
                                 ; as a third minimum beside the free run and
                                 ; the arena's share - 0 meaning "hold
                                 ; nothing", which is below DSK_RAH_MIN and so
                                 ; refuses at the same compare the floor does.
                                 ; .text with a real initialiser for its two
                                 ; neighbours' reason, and STICKY on purpose:
                                 ; the command has to survive the mounts that
                                 ; happen while the commander is away, or the
                                 ; next mount hands the cache straight back
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


; -----------------------------------------------------------------------------
; dsk_rah_serve - serve dsk_xfer's run out of the cache, filling if it pays
; in:  DI = the LBA wanted, SI = sectors still wanted, ES:BX = destination
; out: CF=0 with AX = sectors copied (>= 1); CF=1 = read it off the platter
; clobbers: AX (the output), flags
;
; ONE SCAN. This was `dsk_rah_have` + `dsk_rah_take` + four instructions at the
; call site, and the shape cost two table walks on every miss that filled: the
; fill picked a slot, wrote it, and threw the address away, and `dsk_rah_take`
; was then called a second time to find it again "by construction". The slot is
; held in SI across the fill instead, so the post-fill copy is entered with the
; record already in hand and the delta known to be 0.
;
; The scan itself is §18.8.1's key - the volume index and the mount signature -
; and then a containment test against the chunk's own extent. Only the LIVE
; width is walked: the slots above it hold RAH_N = 0 for ever (18.95.5), so
; scanning them would be correct and wasted.
;
; Registers, because there are six values and six registers: SI walks the
; table, CX counts the slots, DX is the caller's run length, DI is the LBA and
; then the copy's length, BX is the caller's destination all the way through,
; and AX is the scratch every compare needs.
; -----------------------------------------------------------------------------
dsk_rah_serve:
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, si                  ; DX = sectors still wanted
    mov si, dsk_rah_tab
    mov cx, [dsk_rah_runs]      ; the LIVE width. Reached only with a claim
.slot:                          ; live, so the count is never 0 here
    cmp word [si+RAH_N], 0
    je .next                    ; the slot says nothing
    mov al, [disk_drive]
    cmp al, [si+RAH_VOL]
    jne .next
    mov ax, [dsk_sigcur]
    cmp ax, [si+RAH_SIG]
    jne .next
    mov ax, di
    sub ax, [si+RAH_LBA]        ; AX = the sector's index inside the chunk
    jc .next                    ; below this chunk (the subtract wrapped)
    cmp ax, [si+RAH_N]
    jb .copy                    ; ...and inside it: a HIT
.next:
    add si, RAH_REC
    loop .slot

    ; --- a miss: would a fill create any SURPLUS, and does it read? ---------
    call dsk_rah_fill           ; DX in, and it answers CF=1 to BOTH questions
    jc .no                      ; - no surplus to cache, or the read failed.
                                ; Either way the caller reads exactly the
                                ; sectors it asked for, with the retry ladder
                                ; it always had (18.91: one bad sector costs
                                ; its own 512 bytes, not the good ones beside
                                ; it)
    xor ax, ax                  ; a fill begins AT the LBA that missed, so the
                                ; delta into the fresh chunk is 0

    ; --- copy: AX = the delta, SI = the slot's record, BX = the destination -
.copy:
    mov di, [si+RAH_N]
    sub di, ax                  ; DI = sectors of this chunk from here on
    cmp di, dx
    jbe .n
    mov di, dx                  ; ...and never more than were asked for
.n:
    push di                     ; the answer, in sectors
    sub si, dsk_rah_tab         ; slot * RAH_REC, and RAH_REC == DSK_RAH_SECS
    add si, ax                  ; (asserted above), so one shift is
    mov cl, 9                   ; slot*9*512 + delta*512
    shl si, cl                  ; SI = the source offset inside the claim
    shl di, cl                  ; DI = bytes, a chunk being 4,608 at most
    mov cx, di
    mov di, bx                  ; ES:BX is the destination
    mov dx, [dsk_rah_seg]
    mov ax, es
    call dsk_copy_seg_x         ; ~1.6 ms a sector against the ~400 ms call it
                                ; is standing in for
    pop ax                      ; sectors copied
    clc
    jmp short .fin
.no:
    stc
.fin:                           ; a pop leaves CF alone, so both answers share
    pop di                      ; one epilogue
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_rah_fill - cache the rest of LBA DI's track, if a fill would pay
; in:  DI = the LBA that missed, DX = sectors the caller still wants
; out: CF=0 the chunk is cached and SI = its record; CF=1 there is nothing to
;      serve - no surplus, or the read failed
; clobbers: SI (the output), flags
;
; The length is the track's remainder from DI, capped by the chunk and by the
; volume's extent. Not the track's START: a chunk that begins before the sector
; asked for buys nothing a forward-only walk will use, and §19.2.3.1 measured
; that exact mistake at 3 calls WORSE than no cache at all.
;
; **THE POLLUTION GATE IS THE SAME QUESTION AND IS NOW THE SAME ROUTINE.** It
; used to be `dsk_rah_len` called from the call site and then called again from
; here - one divide asked twice, and a routine whose own header said its "OTHER
; caller is the pollution gate". A request already covering the whole chunk
; creates no surplus, so a fill would read exactly what the direct path reads,
; copy it out again, and evict a slot for sectors nobody will ask for twice.
; That is what keeps a streaming file read from flushing the directory chunks
; out from under a copy, and it is why the payload pays no copy at all
; (SPEC.md 18.95.1).
;
; **The read re-enters `dsk_xfer`**, with `[dsk_rah_busy]` stopping it caching
; its own fill - so no CHS conversion, no retry ladder and no EOT handling of
; its own.
; -----------------------------------------------------------------------------
dsk_rah_fill:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, dx                  ; BX = what the caller still wants: the divide
                                ; below wants DX and the gate wants this
    mov ax, di
    xor dx, dx
    div word [disk_spt]
    mov ax, [disk_spt]
    sub ax, dx                  ; sectors left in DI's track, >= 1
    cmp ax, DSK_RAH_SECS
    jbe .vol
    mov ax, DSK_RAH_SECS
.vol:
    mov cx, [dsk_totsec]
    sub cx, di                  ; ...and how many the volume has left. At or
    ja .cap                     ; past the end the subtract is 0 or borrows,
    mov cx, 1                   ; and BOTH mean: ask for one and let dsk_xfer
.cap:                           ; refuse it exactly as it does today, which is
    cmp ax, cx                  ; the same MINIMUM the extent takes anyway - so
    jbe .gate                   ; one cap serves both where two branches did
    xchg ax, cx
.gate:
    cmp bx, ax
    jae .nofill                 ; no surplus: there is nothing to cache

    ; --- spend a slot: round robin, a chunk having no better claim than its
    ;     neighbours. [dsk_rah_next] is a SLOT INDEX, so the wrap is a compare
    ;     against [dsk_rah_runs] itself and no width is mirrored anywhere -
    ;     which matters, because kd_shed (96.44.11) narrows that word from
    ;     outside this file and a stale bound fills a slot no memory backs
    push ax                     ; the chunk length, over the multiply below -
                                ; `mul` writes DX:AX and there is no third
                                ; register free to keep it in
.pick:
    mov ax, [dsk_rah_next]
    mov bx, ax
    inc bx
    cmp bx, [dsk_rah_runs]      ; THE ONE BOUND THAT MUST BE DYNAMIC (SPEC.md
    jb .picked                  ; 18.95.5): this decides which slots are ever
    xor bx, bx                  ; WRITTEN. The scans elsewhere are safe at any
.picked:                        ; width, because dsk_rah_flush clears all
    mov [dsk_rah_next], bx      ; DSK_RAH_RUNS records and the ones above the
                                ; live width then stay RAH_N = 0
    push ax                     ; **A SLOT ACROSS A 64KB PAGE IS NOT FILLED**
    mov cx, 288                 ; (SPEC.md 18.95.7.1). The claim has no page
    mul cx                      ; head, so a boundary can fall inside ONE slot,
    add ax, [dsk_rah_seg]       ; and dsk_runcap then splits that fill into two
    mov cx, ax                  ; int 13h - the whole point of a fill being
    add cx, 287                 ; that it is one. The slot's first and last
    xor cx, ax                  ; paragraphs: a different top nibble is a
    pop ax                      ; different page. The claim is under 64KB, so
    test ch, 0xF0               ; at most one slot straddles, and the width is
    jnz .pick                   ; at least DSK_RAH_MIN = 2, so the next one
                                ; does not
    mov cx, 9
    mul cx                      ; AX = slot * RAH_REC, RAH_REC being 9...
    mov bx, ax
    add ax, dsk_rah_tab
    xchg ax, si                 ; SI = the slot's record (the output)
    shl bx, cl                  ; ...and BX = its own run of the claim, RAH_REC
                                ; == DSK_RAH_SECS making slot*512 it, with CL
                                ; still the 9 the multiply was given
    pop dx                      ; DX = the chunk length again
    mov word [si+RAH_N], 0      ; it holds nothing until the read works
    mov [si+RAH_LBA], di
    mov cx, dx
    mov es, [dsk_rah_seg]
    mov ax, di
    inc byte [dsk_rah_busy]     ; ...so this read is not itself cached
    call disk_read_x
    dec byte [dsk_rah_busy]     ; (dec leaves CF alone)
    jc .out
    mov [si+RAH_N], dx          ; DX still holds the length: only now is the
    mov al, [disk_drive]        ; slot allowed to claim it has anything
    mov [si+RAH_VOL], al
    mov ax, [dsk_sigcur]
    mov [si+RAH_SIG], ax
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.nofill:
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; dsk_rah_flush - forget every cached chunk
; out: nothing; every register AND the flags preserved
;
; ALL DSK_RAH_RUNS RECORDS, not [dsk_rah_runs] of them, and that is what makes
; every other loop in this file safe at any width (SPEC.md 18.95.5): the table
; is 63 bytes of .bss whatever the claim is, dsk_rah_fill never writes a slot
; at or above the live width, so the ones above it stay RAH_N = 0 for ever and
; a scan that over-runs finds nothing rather than garbage. It also means the
; count can shrink between claims without leaving a record behind that names a
; chunk in memory somebody else now owns.
; -----------------------------------------------------------------------------
dsk_rah_flush:
    pushf
    push bx
    push cx
    mov bx, dsk_rah_tab
    mov cx, DSK_RAH_RUNS
.slot:
    mov word [bx+RAH_N], 0
    add bx, RAH_REC
    loop .slot
    pop cx
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; dsk_rah_wr - drop any cached chunk this WRITE lands in
; in:  AX = the run's first LBA, CX = its sector count, [disk_drive] = whose
; out: nothing; every register AND the flags preserved
;
; The cache mirrors the disk, so this is the whole coherence rule and it is in
; the one place every changing sector passes through. A RANGE test and not a
; blanket drop, because in a copy the writes are file DATA and dropping on
; those would evict the source's sectors on every chunk. The volume test comes
; first: a write to another volume cannot change this one's sectors, and
; without it the destination's data, whose LBAs are small numbers like
; anybody's, would evict the source's anyway.
;
; The overlap is two unsigned subtracts rather than two sums: the write starts
; inside the chunk when `first - chunk_start < chunk_n`, and the chunk starts
; inside the write when `chunk_start - first < count`. Either is an overlap,
; both wrap harmlessly the wrong way round, and neither touches AX or CX - so
; the caller's two inputs stay put and the scratch is one register instead of
; the save-and-restore dance a sum needed.
; -----------------------------------------------------------------------------
dsk_rah_wr:
    pushf
    push bx
    push dx
    push di
    cmp word [dsk_rah_seg], 0
    je .out                     ; no claim: the table means nothing yet
    mov bx, dsk_rah_tab
    mov dh, [disk_drive]        ; DH = the volume and DL = the slots left: the
    mov dl, [dsk_rah_runs]      ; width is a byte (<= DSK_RAH_RUNS), which buys
.slot:                          ; the volume a register it can keep
    cmp word [bx+RAH_N], 0
    je .next
    cmp dh, [bx+RAH_VOL]
    jne .next
    mov di, ax
    sub di, [bx+RAH_LBA]
    cmp di, [bx+RAH_N]
    jb .drop                    ; the write starts inside this chunk...
    mov di, [bx+RAH_LBA]
    sub di, ax
    cmp di, cx
    jae .next                   ; ...or the chunk starts inside the write
.drop:
    mov word [bx+RAH_N], 0
.next:
    add bx, RAH_REC
    dec dl
    jnz .slot
.out:
    pop di
    pop dx
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; dsk_rah_reloc - the compactor moved the window (§66.2, §50.6.7)
;
; ONE WORD, asc_reloc's shape exactly: mem_pg_own names [dsk_rah_seg] and
; nothing else, dsk_rah_tab holds LBAs and lengths rather than addresses, and
; the one disk read that targets the cache is the fill - which dsk_xfer pins
; for its duration (§66.3 rule 5), so mem_can_move has already refused by then.
; -----------------------------------------------------------------------------
section .text                   ; **.text AND NOT .cold**, and nothing static
                                ; can catch it if this is wrong: mem_reloc_call
                                ; reaches a kernel proc through cw_mem_disp,
                                ; which is `call bp` in KERNEL_SEG, and the
                                ; offset comes out of MC_RLOC at run time - so
                                ; a .cold body here is executed against the
                                ; kernel segment and the machine walks into
                                ; whatever is at that offset. os88ovlchk reads
                                ; CALL SITES and there is no call site to read.
                                ; Measured: the 192KB floor machine stopped
                                ; reaching a desktop at all, because mem_unblob
                                ; compacts at the end of kmain and the window
                                ; is the first thing it moves
dsk_rah_reloc:
    cmp bx, [dsk_rah_seg]
    jne .out
    mov [dsk_rah_seg], dx
.out:
    ret
section .cold                   ; ...and back, or every routine below joins it

; -----------------------------------------------------------------------------
; dsk_rah_redo - give the cache back and solve for it again (SPEC.md 18.95.8)
; preserves every register and the flags
;
; mem_shed_one's ending without its scan, and NOT reached through it: the shed
; picks the cheapest cache the CLAIMANT outranks, and this is a named block
; being given up rather than a claim being made room for.
;
; The order is free-then-forget, which is mem_shed_one's the other way round,
; and the `cli` is what makes that safe: the rule there is that the naming word
; must never point at memory somebody else already holds, and with interrupts
; off there is nobody to hold it - the only claimants are tasks, and no ISR
; allocates. Doing it this way means a refusal leaves the word, the width and
; the block all still agreeing with each other, where forget-then-free would
; have to put them back.
;
; **IT FALLS THROUGH INTO dsk_rah_want**, which is the only thing its one
; caller ever wanted: a drop whose CF nobody read, followed by a solve. A
; refusal here therefore costs nothing extra - `dsk_rah_want` returns at its
; own "already held" guard - and the pair is one `call` at the call site
; rather than two.
; -----------------------------------------------------------------------------
dsk_rah_redo:
    pushf
    push ax
    push bx
    push dx
    cli
    cmp byte [dsk_rah_busy], 0
    jne .out                    ; A FILL IS IN FLIGHT. It cannot be OURS - the
                                ; transfer holds [sch_lock] - but dsk_rah_fill
                                ; sets this two instructions before the lock is
                                ; taken, and a tick landing in that window
                                ; leaves a task which is about to write into
                                ; the claim. Five bytes to make the question
                                ; not arise, rather than an argument about it
    mov dx, [dsk_rah_seg]
    or dx, dx
    jz .out                     ; nothing held: already what was asked for
    mov bx, MEM_P_DIRW
    call mem_free_x             ; the record IS the allocator (SPEC.md 50.3),
    jc .out                     ; so this is one word - and it refuses while a
                                ; relocation proc is standing on the table
                                ; (66.3 rule 3), which is a real answer and not
                                ; a formality
    xor ax, ax
    mov [dsk_rah_seg], ax
    mov [dsk_rah_runs], ax      ; no claim, so no width either - and this pair
.out:                           ; is the whole of what a shed leaves behind
    pop dx                      ; too (mem_pg_forget zeroes the seg; the width
    pop bx                      ; is stale after one, which is why everything
    pop ax                      ; that reads it tests the seg first)
    popf
                                ; ...and falls through

; -----------------------------------------------------------------------------
; dsk_rah_want - take the cache if we do not hold it
; preserves every register and the flags
;
; Called from disk_mount, so a shed is repaired at the next mount rather than
; never. A machine with no room refuses here every time, which costs a
; mem_avail scan - no I/O - per mount, against a mount whose cheapest possible
; outcome is several ~24 ms sectors.
; -----------------------------------------------------------------------------
dsk_rah_want:
    pushf
    push ax
    push bx
    push cx
    push dx
%ifdef DIRW_ONE
    jmp short .out              ; DIRW1=1: never claim, so every read is
%endif                          ; exactly the sectors asked for (SPEC.md 18.95)
    cmp word [dsk_rah_seg], 0
    jne .out                    ; already held
    mov al, MEM_PG_HIGH         ; ...at OUR OWN rank (SPEC.md 50.6.4), so a
    call mem_avail_lvl_x          ; cheaper cache counts as free and a dearer one
                                ; does not. This window is the most expensive
                                ; cache in the system to lose and the raise
                                ; cache is the cheapest, so it may have that
                                ; one and nothing may quietly have this.
                                ; AX = the largest free RUN, which is the one
                                ; that matters: the claim has to be one
                                ; page-safe run, not scattered KB
                                ;
                                ; SOLVE FOR THE WIDTH (SPEC.md 18.95.5). The
                                ; old rule was "twice DSK_RAH_KB or nothing",
                                ; on fr_cache_row's terms (SPEC.md 40.1) - an
                                ; OPTIMISATION must not be why something else
                                ; fails. Asking outright would be worse than a
                                ; refusal: mem_claim SHEDS and retries, so a
                                ; mount on a tight machine would take the
                                ; window raise cache (11.96) away, and take it
                                ; every mount. Keep the rule and move the
                                ; unknown: 2 x n x 4.5KB <= AX is 9n <= AX, so
    ; --- ...AND WHAT THE MACHINE HAS, not only what is free (18.95.5.2) ----
    ; The free-run rule alone reads the machine at ONE INSTANT - `.out` above
    ; means the width is solved once and never re-negotiated, and on a big
    ; machine that instant is the mount at BOOT with the whole arena free. So
    ; it was effectively asking how much RAM the machine HAS, and answering
    ; the ceiling every time. This asks BOTH and takes the smaller.
    ;
    ; `n <= heap / (SHARE * 4.5)` is `(heap / 2) / 9` at SHARE = 4, which is
    ; the SAME divide the free run already needs - so the whole bound is a
    ; subtract, one shift and a compare, and no second division.
    mov bx, [mem_top]
    sub bx, [mem_base]
    mov cl, 7
    shr bx, cl                  ; BX = HALF the whole arena in KB: paragraphs
                                ; to KB is >> 6 and the half is one more, a
                                ; quarter once the 9 has had its turn
    cmp ax, bx
    jbe .solve
    xchg ax, bx                 ; the machine's share binds before the moment's
.solve:
    xor dx, dx                  ; n = AX/9 - and n = 14 still wants the same
    mov cx, 9                   ; 126KB it always did, while 40KB of free run
    div cx                      ; now buys four slots instead of nothing
    cmp ax, DSK_RAH_RUNS
    jbe .cap
    mov ax, DSK_RAH_RUNS        ; the ceiling: 18.95.4's segment-offset bound
.cap:                           ; - AND the one that makes AH zero, so the two
                                ; bounds below are a byte wide
    cmp al, [dsk_rah_cap]       ; ...and A CEILING SOMEBODY ELSE SET (18.95.8),
    jbe .width                  ; which is a third minimum and needs no
    mov al, [dsk_rah_cap]       ; special case: DSK_RAH_AUTO is above every
.width:                         ; legal width, so an uncommanded machine loses
                                ; this compare and carries on
    cmp al, DSK_RAH_MIN
    jb .out                     ; ...and the floor, below which the metadata
    push ax                     ; eats the cache. THE WIDTH THIS CLAIM WANTS -
                                ; banked, because it is only WRITTEN once the
                                ; memory is really there (below)
    mul cx                      ; AX = n*9, CX being the 9 the divide left
                                ; there and nothing between having touched it
    inc ax
    shr ax, 1                   ; ...and KB = ceil(n * 4.5), a chunk being
                                ; 4,608 bytes. Exact at 14: (126+1)>>1 = 63
    mov bx, MEM_P_DIRW          ; NO 64KB PAGE HEAD (§18.95.7). It used to
    call mem_claim_x            ; claim the WHOLE block as one, so that
    pop ax                      ; dsk_runcap never split a fill - and that is
    jc .out                     ; an OPTIMISATION and not a rule: dsk_runcap
                                ; caps a run at the page boundary and dsk_xfer
                                ; carries on from there, so a straddling fill
                                ; is two int 13h and the right data either way.
                                ;
                                ; What it cost was the PLACEMENT. A block that
                                ; must sit inside one physical page may only
                                ; begin in the part of a page it fits in, so
                                ; first fit skipped every hole below the next
                                ; page boundary - measured on the 192KB floor
                                ; machine, 19KB of a 77KB heap walled off at
                                ; the desktop with nothing yet open, and the
                                ; largest free run 32KB where 46 was there
                                ; (§18.95.7). Prince of Persia pays 8 int 13h
                                ; of 148 for it, and no measurable time
    mov [dsk_rah_runs], ax      ; **THE WIDTH, AND ONLY NOW.** It used to be
                                ; stored before the claim and zeroed again on
                                ; the refusal, which is two stores and a jump
                                ; to end where a refusal already ends - and it
                                ; left a window in which the word described a
                                ; cache that did not exist. kerndos/kdshim.inc
                                ; has a paragraph about keeping this routine
                                ; off its own floor test for exactly that
                                ; reason; the order here makes it structural
    mov [dsk_rah_seg], dx
    mov ax, dsk_rah_reloc       ; ...AND MOVABLE (§66.2, §50.6.7), asc_reloc's
    call mem_movable_x          ; shape and for its reason: this claim is taken
                                ; at a MOUNT, so after a shed it comes back
                                ; wherever the arena had room and stands there
                                ; pinned for the session. ONE WORD names it
                                ; (mem_pg_own says so), the slot table holds
                                ; LBAs and not addresses, and a fill in flight
                                ; is already pinned by mem_in_xfer. BX is still
                                ; MEM_P_DIRW - mem_claim preserves it
    call dsk_rah_flush          ; ...and it holds nothing yet. THE ONLY place
                                ; that is established: .bss arrives at whatever
                                ; -f bin left there, and dsk_rah_serve would
                                ; read it as chunks over random LBAs. It clears
                                ; all DSK_RAH_RUNS records and not just the
                                ; live ones, which is what makes every scan
                                ; below safe at any width (18.95.5)
    xor ax, ax
    mov [dsk_rah_next], ax      ; ...and no cursor may point past the width
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; osapi_dsk_cache - COMMAND the read-ahead cache's width (SPEC.md 18.95.8)
; in:  AL = 0                 hold nothing: give the claim back now
;      AL = 1..DSK_RAH_RUNS   hold at most this many chunks (4.5KB each)
;      AL = DSK_RAH_AUTO      no ceiling of mine: the kernel solves it
; out: CF = 0 with AX = the width held NOW, in chunks (0 = none)
;      CF = 1 - AL was neither a legal width nor the sentinel; nothing changed
;
; For a program that is about to take the machine. The cache is an OPTIMISATION
; (SPEC.md 40.1's rule) and it is the most expensive one in the system to lose,
; so nothing sheds it lightly - which is exactly the wrong answer for a caller
; whose next act is to claim every byte of the arena: mem_claim would shed it
; anyway, one 64KB page later, having first failed a claim the machine could
; have satisfied.
;
; **THE CEILING IS WHY THIS IS A SLOT AND NOT A VERB.** A drop alone would be a
; one-way door: the cache is re-taken at a MOUNT, and a DOS session mounts
; nothing, so "give it back" has to be sayable too. And once the door swings
; both ways the width may as well cross it - the caller knows how much RAM it
; needs and the kernel does not, so a session that wants 9KB of cache standing
; and one that wants none are the same call with a different AL.
;
; [dsk_rah_cap] is STICKY, and that is the feature: it survives every mount
; until somebody says otherwise, so the commander can leave and the width
; stays commanded. Putting it back to DSK_RAH_AUTO is the other half of the
; bracket and is the caller's to do.
; -----------------------------------------------------------------------------
osapi_dsk_cache_x:
    cmp al, DSK_RAH_AUTO
    je .legal
    cmp al, DSK_RAH_RUNS
    ja .bad                     ; a width nothing can hold. Refusing is the
                                ; honest answer: silently clamping would report
                                ; success for a ceiling that was never set
.legal:
    cmp al, [dsk_rah_cap]
    je .done                    ; **THE CEILING ALREADY IN FORCE**, so nothing
                                ; happens at all - which is what makes asking
                                ; free (dos_cache_give asks on every paint of a
                                ; page) and what makes an idempotent re-arm
                                ; cost nothing
    mov [dsk_rah_cap], al
    call dsk_rah_redo           ; ...otherwise RE-SOLVE, and the drop is owed
                                ; WHICHEVER WAY THE CAP MOVED. Narrowing needs
                                ; it because a claim cannot shrink in place;
                                ; WIDENING needs it because there is no grow
                                ; path (18.95.5.3) and without one the release
                                ; would hand back the PERMISSION and leave the
                                ; narrow cache standing for the rest of the
                                ; session - a machine that ran one DOS program
                                ; on `9K` keeping 9K for ever. What it costs is
                                ; the cache's CONTENTS, which is the right
                                ; price: the caller that commanded it down is
                                ; the one whose program has just been all over
                                ; the disk.
                                ;
                                ; A REFUSED DROP (a fill in flight) leaves the
                                ; old width standing, the solve it falls into
                                ; returns at its own guard, and AX below
                                ; reports what is really there rather than what
                                ; was asked for
.done:
    mov ax, [dsk_rah_runs]
    cmp word [dsk_rah_seg], 0
    jne .say
    xor ax, ax                  ; A SHED LEAVES THE WIDTH STALE (mem_pg_forget
.say:                           ; zeroes the naming word and nothing else), so
    clc                         ; the seg is what says whether there is a cache
    retf                        ; and the width is only meaningful under it
.bad:
    stc
    retf

; -----------------------------------------------------------------------------
; dsk_dirw_get - the next directory sector's CONTENTS (SPEC.md 19.2)
;
; out: CF=0 - AX = the sector's LBA, and dsk_secbuf holds its 512 bytes
;      CF=1 with AX = 0 - the directory ends here (dsk_dirw_next's own CF)
;      CF=1 with AX = 1 - a read failed; [dsk_ioerr] is int 13h's status
; clobbers: AX (the output), CF
;
; Every directory walk in the kernel used to be dsk_dirw_next followed by a
; one-sector disk_read into dsk_secbuf, spelled out at seven call sites. This
; is that pair, once - and it is ALL it is: §19.2.3's private window lived
; here and has moved under disk_read (§18.95), where the same mechanism also
; covers file data, the FAT window's loads and the icon harvest. What is left
; is the deduplication, which was worth having on its own.
; -----------------------------------------------------------------------------
dsk_dirw_get_x:
    push bx
    push cx
    push dx
    push es
    call dsk_dirw_next_x
    jc .end
    push ax
    push ss                     ; the buffer is in .lowbss, reached through ES
    pop es                      ; - and SS names LOW_SEG (SPEC.md 2.1)
    call dsk_rd1_x
    pop ax
    jc .ioerr
    clc
    jmp short .out
.end:
    xor ax, ax                  ; the directory ended...
    stc
    jmp short .out
.ioerr:
    mov ax, 1                   ; ...as against a read that failed
    stc
.out:
    pop es
    pop dx
    pop cx
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
dsk_find_x:
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
    cmp byte [dsk_vkind], DVK_FILE
    je .fsenum                  ; A REDIRECTED VOLUME (SPEC.md 62.9.17): no
                                ; sectors to walk, so the driver is asked for
                                ; the Nth entry instead
    call dsk_dirw_start_x
.sector:
    call dsk_dirw_get_x           ; dsk_secbuf = the next directory sector, and
    jnc .secok                  ; ES:DI stays the CALLER's buffer throughout
    or ax, ax
    jz .jend
    jmp .ioerr
.jend:                          ; a relay, .jfail1's device one routine along:
    jmp .end                    ; nothing falls into it, and .end is out of
                                ; short range from both of the jumps that
                                ; would otherwise pay five bytes each
.secok:
    mov si, dsk_secbuf
    mov cx, 16                  ; entries to a 512-byte sector
.entry:
    mov al, [ss:si]             ; SS is LOW_SEG (SPEC.md 2.1) and dsk_secbuf
    mov ah, [ss:si+DSK_R_ATTR]  ; is `.lowbss`, so the two reads reach it
                                ; without disturbing ES:DI, which is the
                                ; CALLER's buffer and the reason the pair used
                                ; to be bracketed in a push/pop dance
    test al, al
    jz .jend                    ; 0x00: the end of the directory, per spec
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
    add si, DSK_DE_SIZE         ; the ON-DISK stride, 32 by FAT12. The staged
    loop .entry                 ; listing's is DSK_DE_STRIDE and is narrower
    jmp short .sector

.fsenum:
    ; --- A REDIRECTED VOLUME: ONE FSV_ENUM, BY ORDINAL (SPEC.md 62.9.17) ----
    ; fcp_scan's arm, one cell along and for the same reason: there are no
    ; directory sectors here, no raw 32 bytes to filter and no dsk_synth to
    ; run, so the driver is asked for the Nth entry of this folder and answers
    ; with the same SPEC.md 19.1 staged entry dsk_synth would have built. That
    ; is why this joins the sector arm at .stage rather than repeating its
    ; tail: one copy of "what does the caller's buffer take from an entry".
    ;
    ; THE SPECIES FILTER ABOVE IS THE SECTOR ARM'S AND DOES NOT APPLY: deleted
    ; entries, the dot links, long-name fragments and the volume label are
    ; things a FAT directory holds and a driver's row array does not. Hidden
    ; and system are the exception, and their fence is api_file_find's, above
    ; BOTH arms - so a package still cannot find a system file on either.
    mov cx, bp                  ; the ordinal (BP is this routine's own copy)
    mov bp, FSV_ENUM
    mov dx, ds                  ; DX:BX and not ES:BX (SPEC.md 51.8), and DS
    mov bx, dsk_ent             ; because dsk_ent is `.bss`
    push es                     ; **ES:DI IS THE CALLER'S BUFFER AND A DRIVER
    push di                     ; OWNS EVERY REGISTER THE FSV ABI DOES NOT
    call drv_fs_call            ; RESERVE** (SPEC.md 62.9). RAMDISK.DRV happens
    pop di                      ; to preserve both and the cable's need not;
    pop es                      ; `pop` leaves the flags, so the answer below
    jnc .stage                  ; is still drv_fs_call's
    or ax, ax
    jz .jend2                   ; AX = 0 is "the driver publishes no FSV_ENUM"
                                ; and is drv_fs_call's answer for an absent
                                ; verb rather than the driver's - the same word
                                ; an empty folder gives (62.9.7), so it becomes
                                ; the end of the directory and never an error
    jmp .ioerr                  ; ...and anything else is the driver's own
.jend2:                         ; FERR_*, passed through
    jmp .end

.take:
    ; dsk_synth stages the display name, the type and the cluster into the
    ; kernel's own dsk_ent (SPEC.md 19) - the dsk_get_dir idiom, so the copy
    ; out to the caller is a plain move with no raw-field decoding here.
    push es
    mov dx, LOW_SEG
    mov es, dx
    call dsk_synth_x              ; ES:SI = the raw entry -> dsk_ent, and it
                                ; banks DI itself
    mov bx, si                  ; ...and the raw entry again, for the 32-bit
    mov ax, [es:bx+DSK_R_SIZE]      ; size, which dsk_ent CLAMPS to a word
    mov dx, [es:bx+DSK_R_SIZE+2]    ; and a copy cannot work from
    mov cl, [es:bx+DSK_R_ATTR]      ; ...and the attribute, for the caller
    mov ch, 0                   ; ...and whether it is COMPRESSED, which is
    push ax                     ; the +22 word, and which changes
    mov al, [es:bx+DSK_R_CZM]   ; ...the mark LESS its format (20.14.2.4), so
    sub al, DSK_CZ_MARK         ; 0x5A and 0x5B both pass and nothing else
    cmp al, LZ_LZB
    pop ax
    ja .plain                   ; the SIZE this cell answers: an application
    mov ch, 1                   ; sizes its claim off +18 and must be told
    cmp byte [dsk_fdraw], 0     ; what the file becomes, not what it occupies
    jne .plain                  ; (SPEC.md 20.14.3) - unless the caller asked
                                ; through api_file_find_raw, which is a COPIER
                                ; and wants the length of what it is about to
                                ; move. The FLAG is set either way: bit 0 of
                                ; +22 is a fact about the file, not about which
                                ; of its two sizes was reported
    mov ax, [es:bx+DSK_R_CZL]   ; The hint is read from a
    mov dl, [es:bx+DSK_R_CZH]   ; sector this walk is holding anyway, so a
    mov dh, 0                   ; listing costs exactly what it cost before
.plain:
    pop es
    jmp short .staged
.stage:
    ; ...AND FSV_ENUM's ANSWER ARRIVES HERE (SPEC.md 62.9.17), with dsk_ent
    ; already staged by the driver. There is no raw entry behind it, so the
    ; two fields the sector arm reads out of one come from the staged form
    ; instead: the size is dsk_ent's own dword, and the attribute is 0 -
    ; which is what a redirected volume's entries are, the species bits
    ; living in the driver's row and never on this side of the ABI.
    mov ax, [dsk_ent+20]
    mov dx, [dsk_ent+22]
    xor cl, cl
.staged:
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
    mov cl, ch                  ; +22 the flags word, written HERE because CH
    mov ch, 0                   ; is the last live use of that register and
    mov [es:di+8], cx           ; the alternative is a push between two pops
    mov ax, [dsk_ent+16]
    mov [es:di], ax             ; +14 the type word (1 file, 2 dir, 3 parent)
    mov ax, [dsk_ent+18]
    mov [es:di+2], ax           ; +16 the first cluster
    pop dx
    pop ax
    mov [es:di+4], ax           ; +18 the size, all 32 bits of it
    mov [es:di+6], dx
    mov cx, [dsk_fdord]
    inc cx                      ; the ordinal to ask for next
    pop di
    pop bp
    pop si
    pop dx
    pop bx
    clc
    retf
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
    retf

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
dsk_chdir_x:
    mov [dsk_cwd], ax
    mov byte [dsk_keepcwd], 1
    call disk_mount_x
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
dsk_chdir_q_x:
    call dsk_here_ok            ; already standing here, on media that cannot
    jnc .here                   ; have changed under us? (SPEC.md 18.9.1)
    mov byte [dsk_quiet], 1
    call dsk_chdir_x
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
    cmp byte [dsk_mntok], 0     ; the last mount of this volume worked...
    je .no
    cmp dl, [disk_drive]        ; ...it is this volume...
    jne .no
    cmp ax, [dsk_cwd]           ; ...and this directory
    jne .no
    jmp dsk_media_ok            ; ...and the MEDIA half answers the rest
                                ; (SPEC.md 18.9.1.1). NO PUSH: the identity
                                ; half only COMPARES, so the contract's "every
                                ; register preserved" needs nothing spent to
                                ; keep it, and the media half banks its own
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; dsk_media_ok - can the MEDIUM under this mounted volume have changed?
; in:  DL = volume
; out: CF=0 = it cannot have, so a mount may be skipped; CF=1 = it might have
; clobbers: CF only (every register preserved)
;
; SPEC.md 18.9.1's second half, named. It was the tail of dsk_here_ok and is
; split out because the IDENTITY half and the MEDIA half are two questions and
; only one caller wants both: `fcp_goto` (SPEC.md 18.9.1.1) moves between
; folders INSIDE one mounted volume with no I/O, which is what makes
; OSAPI_FILE_PATH's walk cost one mount and then nothing - so it may not ask
; "are we at the same cluster", and it absolutely must ask "is it the same
; disk". It had been asking neither, and a floppy swapped under a running
; package was served the old disk's directory out of SPEC.md 18.95's cache
; until the package was closed and reopened.
; -----------------------------------------------------------------------------
dsk_media_ok:
    push ax
    push bx
    push es
    mov bl, dl
    call dsk_vol_row_x
    jc .no
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .yes                    ; not removable: nothing to re-validate. Both
                                ; non-BIOS kinds, and a REDIRECTED volume
                                ; (SPEC.md 62.9) is the clearer case of the
                                ; two - there is no BPB behind it to go stale
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
; dsk_dest - say WHERE the next loud mount writes its listing (SPEC.md 22.6,
;            docs/plans/LISTING-HOME-PLAN.md 13)
; in:  DX = the store's segment, 0 = there is nowhere; BX = entry 0's offset
;      in it (ignored when DX is 0)
; out: nothing (all registers preserved)
;
; A listing has no home of its own: it is written where its CALLER keeps one,
; which for a Disk window is the FS_VSEG claim it already holds and for the
; Standard File dialog is one claimed for as long as the dialog is up. A
; caller with nowhere for a listing passes DX = 0 and gets a QUIET mount -
; the volume, the geometry, the FAT window and the current directory, and no
; scan, sort or icon harvest.
;
; **[dsk_ioff] IS DERIVED HERE AND NOWHERE ELSE**, so a store has ONE shape:
; DSK_NENT entries of DSK_DE_STRIDE, then one reference byte each. Passing it
; would let two callers disagree about a layout `fmv_iofs` and the harvest
; both have to read the same way.
; -----------------------------------------------------------------------------
dsk_dest_x:
    push ax
    push dx
    mov [dsk_dseg], dx
    mov [dsk_doff], bx
    mov ax, [dsk_nmax]          ; the index sits just past the entries
    mov dx, DSK_DE_STRIDE
    mul dx                      ; DX:AX, and DX comes back 0: nmax caps at
    add ax, bx                  ; DSK_NENT and the product is 1,536
    mov [dsk_ioff], ax
    pop dx
    pop ax
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

; -----------------------------------------------------------------------------
; dsk_path - the calling directory's path from the volume root (SPEC.md 19.2.4)
; in:  ES:DI = the caller's buffer, CX = its size in bytes (>= 2)
; out: CF=0, a NUL-terminated `\DIR\DIR` written there, CX = its length not
;      counting the NUL, DI unchanged. The root answers `\` and CX=1.
;      CF=1 and AX = FERR_*: FERR_BIG (the buffer cannot hold it - which is
;      also what a CYCLIC '..' chain turns into, see below), FERR_NAME (a
;      '..' link is missing, wild, or would not read), FERR_NOENT (a parent
;      does not name its own child, which is a cross-linked disk).
; clobbers: AX, CX (the outputs), BP, flags.
;
; **WHY THIS IS KERNEL CODE** (docs/plans/PATH-WAVE-PLAN.md). SPEC.md 19.2 says
; going up needs no path stack *because the disk records the parent* - true of
; this file and of nothing outside it, because dsk_find drops the on-disk dot
; links outright (`cmp al, '.'` / `je .skip`) so no package can see '..'.
; Three have each built a descent stack for want of this one answer:
; apps/ftpd's 16-level FD_CDMAX, the DOS box's self-maintained AH=47h string,
; and apps/tank's walk to SYSTEM\APPDATA.
;
; **AND THE READ COUNT IS THE POINT.** A package-side walk would name each
; level with OSAPI_FILE_FIND, which is ordinal-based and restarts the
; directory walk on every call - K far calls for a parent of K entries, each
; re-walking from entry 0. This walks with dsk_dirw_start/dsk_dirw_get, which
; take a CLUSTER and stand nowhere - so there is no chdir and no mount at any
; level, each directory is read ONCE, and the sectors it does read come
; through 19.2.3's cached window (SPEC.md 19.2.4.1 has the measurement, and
; the argument that did not survive it).
;
; **IT IS THREE ROUTINES THIS FILE ALREADY HAD, IN A LOOP**: dsk_up (the
; '..' read dsk_dotdot is, against an argument), dsk_byclus (the by-cluster
; search dskw_rmtree re-finds an emptied folder with) and dsk_synth_name (the
; listing's own formatter). The slot shipped with a private copy of each -
; 189 bytes of second walkers - which is the classic finding (CLAUDE.md,
; docs/INDEX.md's first rule).
;
; It builds BACKWARDS into the tail of the caller's buffer and shuffles the
; result down at the end, because the walk discovers names deepest-first and a
; path reads root-first. Banking the cluster chain and walking down it instead
; would cost a second read of every directory and a resident array.
;
; REGISTERS, NOT MEMORY. BX is the NUL's home, AX and DX are the parent and
; the child, the name stages in dsk_ent, and the buffer's BASE is the DI
; pushed at entry, read through BP = SP - reloaded right before the read,
; because dsk_dirw_get does not preserve BP. (Held in BP across a level, the
; base once went to garbage and the shuffle wrote the path into the middle
; of the CALLER's image, which presented as a window title that read as
; machine code.) The walk used to own five words of .bss for these.
;
; NO DEPTH COUNTER. A '..' that points at a descendant walks in a circle, and
; a DSK_PATH_MAX of 32 levels used to be the bound. The buffer is the bound:
; every level prepends at least `\X`, so a cycle refuses with FERR_BIG within
; size/2 levels, each of them a sector the window already holds - and the
; answer is the same refusal an honest chain that deep would have got.
;
; SEGMENTS: ES is the CALLER's throughout and is never reloaded - dsk_secbuf
; is `.lowbss` and reached through SS (SPEC.md 2.1), the staged name is the
; kernel's own and reached through DS, and the one copy that crosses is a
; plain `rep movsb`, DS:SI -> ES:DI. The shuffle's two ends are BOTH the
; caller's, so it points DS at ES for the one instruction rather than
; overriding movsb's source - see the shuffle for why that is not a style
; choice on this CPU.
; -----------------------------------------------------------------------------
%ifndef KERN_SMALL              ; NO CALLER ON THE SMALL DISKS (SPEC.md 24.5):
                                ; the DOS box is the slot's consumer and
                                ; DOS.O88 ships on kern_big's system disk
                                ; alone, so kern_small carries the cell and
                                ; no body - gfx_spans' precedent
dsk_path_x:
    push bx
    push dx
    push si
    push di                     ; the base: DI's way back, and [bp] below
    cld
    cmp cx, 2
    jb .toobig                  ; cannot hold even `\` and its NUL
    mov bx, di
    add bx, cx
    dec bx                      ; BX = the NUL's home, and the result's end
    mov di, bx
    mov byte [es:di], 0
    mov ax, [dsk_cwd]
.level:
    or ax, ax
    jz .done                    ; 0 is the root: the walk is over
    mov dx, ax                  ; DX = the child being named...
    call dsk_up_x               ; ...AX = its parent, without standing there
    jc .corrupt
    call dsk_byclus_x           ; dsk_ent = the child's name, CX = its length
    jc .noname
    mov si, di                  ; room for the name and its separator?
    sub si, cx
    dec si                      ; SI = where the separator would go...
    mov bp, sp
    cmp si, [bp]                ; ...against the base (the DI pushed at entry)
    jb .toobig                  ; REFUSED, never truncated (SPEC.md 47)
    mov byte [es:si], '\'
    lea di, [si+1]
    push si
    mov si, dsk_ent
    rep movsb                   ; DS:SI is ours, ES:DI is the caller's
    pop di                      ; DI = the separator: the next level goes in
    jmp short .level            ; front of it

.done:
    cmp di, bx
    jne .shuffle
    dec di                      ; standing at the root and nothing was
    mov byte [es:di], '\'       ; prepended: the answer is the one separator
.shuffle:
    mov si, di                  ; [SI .. BX] -> the base, NUL included
    pop di                      ; the base...
    push di                     ; ...which is also DI's way back
    mov cx, bx
    sub cx, si                  ; BX >= SI always, so this leaves CF=0 - and
    inc cx                      ; nothing between here and the retf writes a
    push cx                     ; flag, so that IS the success answer
    push ds                     ; BOTH ends are the caller's (see SEGMENTS),
    push es                     ; so DS = ES for the one copy, and never
    pop ds                      ; `rep es movsb`: an 8086/8088 interrupted
    rep movsb                   ; mid-string resumes with only the prefix
    pop ds                      ; NEXT TO the opcode, and loses the other.
                                ; nasm 2 emits F3 26 (REP lost: the path is
                                ; cut short) and nasm 3 emits 26 F3 (ES lost:
                                ; the rest is read out of the kernel). IRQ0
                                ; alone lands in one 18.2 times a second.
                                ; tests/unit/t_asmrules.py rule 5 refuses it
    pop cx
    dec cx                      ; the length the caller is told excludes the NUL
.out:
    pop di                      ; ...and DI comes back as it arrived
    pop si
    pop dx
    pop bx
    retf                        ; FAR: the only entry here reached from the
                                ; kernel segment (SPEC.md 2.6.1)

.corrupt:
    mov ax, FERR_NAME
    jmp short .fail
.noname:
    mov ax, FERR_NOENT
    jmp short .fail
.toobig:
    mov ax, FERR_BIG
.fail:
    stc
    jmp short .out
%endif                          ; KERN_SMALL

; -----------------------------------------------------------------------------
; dsk_byclus - the subdirectory of directory AX whose first cluster is DX
; in:  AX = a directory's first cluster (0 = the root), DX = the cluster sought
; out: CF=0: SI -> its raw 32-byte entry in dsk_secbuf (SS:SI), and dsk_ent
;      holds its display name with CX = the name's length (dsk_synth_name);
;      CF=1: no such entry, or the directory would not read
; clobbers: CX, SI (the outputs), BP, flags
;
; ONE PASS over the directory's sectors, which is the difference from asking
; across the API: OSAPI_FILE_FIND is ordinal-based and restarts the walk on
; every call, so a parent of K entries would cost K far calls each re-walking
; from entry 0. Here the directory is read once - through 19.2.3's window,
; so a second walk of the same chain costs no read at all (tests/pathcost.py).
;
; TWO CALLERS: dskw_rmtree, which re-finds the folder it just emptied in the
; parent by the cluster it holds rather than by a name it would have had to
; carry down (diskw.inc's dskw_rt_byclus), and dsk_path, which names each
; level of a path the same way. The name is formatted HERE rather than by
; the caller because the raw entry is in the sector window and the formatter
; wants ES = LOW_SEG: one push and pop of ES here rather than at each site.
; -----------------------------------------------------------------------------
dsk_byclus_x:
    push ax
    push bx
    push di
    push es
    call dsk_dirw_start_x
.sector:
    call dsk_dirw_get_x         ; either failure is "not found"; it banks
    jc .none                    ; BX/CX/DX/ES itself
    mov si, dsk_secbuf
    mov cx, 512 / DSK_DE_SIZE
.entry:
    mov al, [ss:si]
    test al, al
    jz .none                    ; 0x00 ends the directory, per spec
    cmp al, 0xE5
    je .next                    ; deleted
    cmp al, '.'
    je .next                    ; the dot links, whatever they point at
    test byte [ss:si+DSK_R_ATTR], 0x10
    jz .next                    ; only a subdirectory can be the one
    cmp dx, [ss:si+DSK_R_CLUS]
    je .found
.next:
    add si, DSK_DE_SIZE
    loop .entry
    jmp short .sector
.found:
    push ss
    pop es                      ; the formatter reads the entry through ES
    call dsk_synth_name         ; dsk_ent = stem[.ext], DI -> its end
    mov cx, di
    sub cx, dsk_ent             ; CX = the length, and CF=0 with it
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_up - the parent of directory AX, WITHOUT standing there (SPEC.md 19.2)
; dsk_dotdot - the same for the CURRENT directory
; in:  AX = a subdirectory's first cluster (non-zero); dsk_dotdot takes
;      nothing, and [dsk_cwd] must be non-zero (inside a subdirectory)
; out: CF=0, AX = the parent's first cluster, 0 = the root; CF=1 = the '..'
;      link is missing, wild, or would not read
; clobbers: AX (the output), flags
;
; Every subdirectory carries '.' and '..' as its first two entries by spec,
; and '..' holds the parent's first cluster - with the FAT convention that
; a parent OF the root is written as cluster 0, which is exactly the value
; this kernel already uses for "the root". So going up needs no path stack
; and no memory of how we got here: the disk itself records it.
;
; ONE BODY. dsk_dotdot is the navigation's (three callers, one of them the
; copy engine's through FCPX) and dsk_up is OSAPI_FILE_PATH's, which needs
; the parent of a cluster it is NOT standing in - standing there is the
; whole cost that slot exists to avoid. They were two routines for one
; cycle: 89 bytes for 32 bytes of work.
;
; The entry is read through SS - dsk_secbuf is `.lowbss` (SPEC.md 2.1) - so
; ES is left alone and no register is borrowed: the '..' test is one word
; compare at a fixed address, and `cmp [dsk_maxclus], ax` is written that
; way round so that CF IS the answer, with no clc.
; -----------------------------------------------------------------------------
dsk_dotdot_x:
    mov ax, [dsk_cwd]           ; ...and fall into dsk_up
dsk_up_x:
    call dsk_dirw_start_x
    call dsk_dirw_get_x         ; the directory's very first sector - the
    jc .bad                     ; walk-then-read PAIR, which this file already
                                ; owns and three callers in diskw.inc already
                                ; use. Both of its failures (end, and I/O) are
                                ; .bad here, so the merge is exact
    cmp word [ss:dsk_secbuf + DSK_DE_SIZE], '..'   ; entry 1 is '..' by spec
    jne .bad
    mov ax, [ss:dsk_secbuf + DSK_DE_SIZE + DSK_R_CLUS]
    cmp [dsk_maxclus], ax       ; 0 (the root) or a walkable cluster; a
    jb .bad                     ; corrupt '..' must not become a cwd. Not
    ret                         ; taken: maxclus >= AX, so CF=0 is the answer
.bad:
    stc
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
; --- dsk_write_chain - dsk_read_chain, the other way (SPEC.md 18.4.7) -------
; in/out: dsk_read_chain's exactly, with ES:BX the SOURCE of the bytes.
;
; ONE BYTE OF STATE AND ONE BRANCH, because the walk below is already
; direction-agnostic: the run coalescing, the corruption tests, the progress
; widget and [dsk_chain_end] are all about the CHAIN, and the only
; read-specific instruction in it is the `call` inside `.flush`. A write gets
; the coalescing a read already had - a contiguous span goes out as one
; dsk_xfer either way.
;
; IT DOES NOT ALLOCATE. The chain has to be long enough already; a link past
; the end is the same corruption a read would report. Growing a file is
; dskw_wabody's AT-END arm, where §18.4's commit order lives (18.4.7.3).
%ifndef KERN_SMALL                 ; its one caller is dskw_wabody's INSIDE arm,
dsk_write_chain_x:              ; which is kern_big's (SPEC.md 18.4.7.4)
    mov byte [dsk_chwr], 1
    call dsk_read_chain_x
    mov byte [dsk_chwr], 0      ; ...and the flags ride through: `mov` leaves
    ret                         ; them alone, so CF is still the walk's answer
%endif

dsk_read_chain_x:
    call kentc_bp               ; KENT: push ax..bp







    push ax                     ; the whole file's extent IS the widget's
    push dx                     ; scale, and this is the only place that knows
    mov ax, dx                  ; it: dsk_xfer sees one coalesced run at a time
    mov cx, 512                 ; (SPEC.md 12.8). This caller is handed SECTORS
    mul cx                      ; and holds no finer figure, so the byte count
    mov cx, ax                  ; is exact to the sector and the last partial
    call ct_fpg_begin                         ; one rounds up here exactly as it used to
    pop dx                      ; round up inside the widget (12.8.1)
    pop ax                      ; ...AX is the first cluster either side

    ; --- a REDIRECTED volume: ONE FSV_READ, by the handle in AX (62.9.1) ----
    ; This is the loader's and assoc's path, and it is the one caller that
    ; arrives holding a handle rather than a name - the staged entry's
    ; first-cluster word IS the driver's handle on such a volume - so it reads
    ; directly and never resolves. fpg_begin above has already taken the
    ; sector count as its scale, which is as true of a cable as of a floppy.
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: the chain reader's
    cmp byte [dsk_vkind], DVK_FILE
    jne .walk
    mov [dsk_chain_end], ax     ; the handle, where .done's own store would
                                ; have put it: SI is READAT's OFFSET below,
                                ; so it cannot bank it any more. Nothing has
                                ; ever read it on this arm - fcp_rdnext is
                                ; the one caller that resumes a chain and it
                                ; branches to its own redirected arm long
                                ; before here - and it is kept rather than
                                ; dropped at three bytes (SPEC.md 62.9.18)
    push ax                     ; the handle, across the arithmetic
    mov ax, dx                  ; the capacity is exactly what was asked for:
    mov cx, 512                 ; DX sectors, in 32 bits, so the driver can
    mul cx                      ; clamp against it and can never overrun
    mov di, dx                  ; DI:CX = the 32-bit capacity
    mov cx, ax
    mov dx, es                  ; DX:BX = the destination - DX and not ES,
    pop ax                      ; because ES is KERNEL_SEG inside a driver
    push bp
    ; **THE VERB IS READAT AND NOT READ** (SPEC.md 62.9.18). This routine's
    ; contract is `read DX SECTORS`, which the FAT walk below does by simply
    ; stopping; FSV_READ is `the WHOLE file` and a driver serving it must
    ; REFUSE a file longer than the buffer rather than truncate, because
    ; OSAPI_FILE_READ sits on the same verb. The two agree for every file at
    ; most as long as the ask and disagree for exactly one species - a
    ; package carrying PARTS, whose file is bigger than the image step 6 asks
    ; for (SPEC.md 20.12) - which is why every package on a RAM disk loaded
    ; until one had parts. READAT at offset 0 is the same bytes, CLAMPED.
    mov bp, FSV_READ            ; over 64KB there is no 16-bit verb to ask
    or di, di                   ; with, and no caller of THIS arm can ask it:
    jnz .fsgo                   ; the loader's is a region (APP_MAX_SIZE) and
    mov bp, FSV_READAT          ; assoc's is ASC_KB*2 sectors (54.7.1)
    xor si, si                  ; DI:SI = offset 0, DI being zero already
.fsgo:
    call drv_fs_call
    pop bp
    jc .fsfail
    jmp .done2                  ; the shared tail: fpg_end, the pops, CF=0 -
                                ; joined BELOW the chain-end store, which
                                ; this arm has already made
.fsfail:
    jmp .fail1                  ; a refusal is LD_EDISK, the same verdict a
                                ; failed sector read gets - the loader has no
                                ; finer question to ask about a wire
%endif                          ; OS88_REDIR
.walk:
    mov si, ax                  ; SI = current cluster
    xor cx, cx                  ; CX = run_len (0 = no pending run)
    xor di, di                  ; DI = run_lba (dead while run_len = 0)
.next:
    mov ax, si
    call dsk_clus2lba_x
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
    call dsk_next_clus_x
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
.done:
    mov [dsk_chain_end], si     ; where a chunked caller resumes from
.done2:                         ; ...and where the redirected read above joins,
    clc                         ; having stored its handle already
    call ct_fpg_end                           ; preserves CF, so it sits either side of the
                                ; clc without comment (SPEC.md 12.8)
    jmp kretc_bp
.fail1:
    mov byte [dsk_cherr], 1     ; disk_read failed
    jmp short .failc
.fail2:
    mov byte [dsk_cherr], 2     ; chain corrupt
.failc:
    call ct_fpg_end                           ; a failed read owes the bar back exactly as
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
.flush:                         ; move the pending run (DI = LBA, CX = len)
    push ax                     ; to or from ES:BX; advance BX; CF through. AX
    mov ax, di                  ; is the caller's take count - preserved
%ifndef KERN_SMALL
    cmp byte [dsk_chwr], 0      ; THE ONE DIRECTION-SPECIFIC INSTRUCTION in
    jne .flwr                   ; this whole routine (SPEC.md 18.4.7.1)
%endif
    call disk_read_x              ; in: AX=LBA, CX=count, ES:BX
%ifndef KERN_SMALL
    jmp short .flgot
.flwr:
    call disk_write_x
.flgot:
%endif
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
dsk_copy_seg_x:
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
dsk_copy_in_x:
    push ax
    push dx
    mov dx, LOW_SEG
    mov ax, KERNEL_SEG          ; destination = the kernel segment. NAMED, not
                                ; taken from CS: this code is .cold now, where
                                ; CS is COLD_SEG - SPEC.md 2.6 rule 2
    call dsk_copy_seg_x
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

; -----------------------------------------------------------------------------
; dsk_streq - do the NUL strings at DS:SI and DS:DI match? (SPEC.md 4)
; out: CF=0 = equal; every register preserved
;
; The `.cold` segment's string compare, beside its counted copy and for the
; same reason: this file is the first `.cold` %include, so fdlg.inc, files.inc
; and dsk_find_name below all reach it with a backward near call. It was
; fdlg_streq, which is what kept the other three writing it out again.
; -----------------------------------------------------------------------------
dsk_streq:
    push ax
    push si
    push di
.c:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_ncopy - CX bytes from DS:SI to DS:DI, one at a time (SPEC.md 4)
; in:       SI = source, DI = destination, CX = count
; out:      nothing (all registers preserved)
;
; The .cold segment's counted copy, and the reason it is a byte loop rather
; than `rep movsb` is at dskw_copy11: ES belongs to the caller's buffer at
; three of the sites and must not be reloaded to move eleven bytes.
;
; It lives HERE because disk.inc is the first `.cold` %include, so every one
; of the thirteen sites in diskw.inc, filecp.inc, files.inc and fdlg.inc is a
; BACKWARD near reference inside one section. It was four separate copies of
; this body, one per file. NOT to be confused with fm_ncpy, which is a
; NUL-stopping bounded copy and answers a different question.
; -----------------------------------------------------------------------------
dsk_ncopy:
    push ax
    push cx
    push si
    push di
.b:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .b
    pop di
    pop si
    pop cx
    pop ax
    ret


%ifdef DSK_WANT_ICONS           ; the store's gate (SPEC.md 25.9): with no icon
                                ; family there is no reference to settle
; -----------------------------------------------------------------------------
; dsk_icopass - settle every entry's icon REFERENCE with no sector read at all
; in:  BP = the entry count; the listing standing in [dsk_dseg]/[dsk_doff]
; out: nothing (AX, DX and SI are spent)
;
; SPEC.md 18.3 step 4a'. `ico_ref_try` per entry: a folder references the
; built-in body in `.text`, a package already in the store takes its row, and
; a document is `dsk_docpass`'s business. A MISS is simply not tested - there
; is no sector to go and read on either of this routine's two paths.
;
; **IT HAS TWO CALLERS AND THEY ARE DIFFERENT KINDS OF THING** (SPEC.md
; 25.9.5). One is the mount's own pass over a REDIRECTED volume, which has no
; sectors to peek at; the other is the repair after a shed, walking a WINDOW's
; own listing to re-resolve reference bytes whose rows went with the store.
; What makes one body serve both is that the reference index follows the
; listing into whatever store `dsk_dest_x` was given, so neither caller names
; a segment and the walk is the same instruction either way.
; -----------------------------------------------------------------------------
dsk_icopass:                    ; NO PROLOGUE, and the header above is where
                                ; that was always written down: AX, DX and SI
                                ; are SPENT. Both callers are already past
                                ; needing them - the mount's `.fsicons` arm
                                ; goes straight to `.done`, which reads BP
                                ; alone, and `fm_focus_x` banks AX, CX, DX and
                                ; SI itself before it ever reaches the repair
    xor dx, dx
.ip:
    cmp dx, bp
    jae .ip_done
    mov ax, dx
    call dsk_get_dir_x          ; SI -> dsk_ent
    call ico_ref_try
    inc dx
    jmp short .ip
.ip_done:
    ret

%ifdef OS88_ASSOC
; -----------------------------------------------------------------------------
; dsk_docpass - compose a document body for every type-0 entry (SPEC.md 54.3)
; in:  BP = the entry count; the listing standing in [dsk_dseg]/[dsk_doff]
; out: nothing (AX, BX, DX and SI are spent)
;
; The SECOND pass, and the order is the whole reason it is one - see the body.
; Factored for `dsk_icopass`'s reason: the repair after a shed has to compose
; these again too, and a document body costs no I/O, so the two passes are
; exactly what a repair may run and a re-list is exactly what it need not.
; -----------------------------------------------------------------------------
dsk_docpass:                    ; `dsk_icopass`'s reason for having no
                                ; prologue, one register wider: the header
                                ; says AX, BX, DX and SI are spent and no
                                ; caller wants them back
    ; --- 4b. document icons (SPEC.md 54.3) ------------------------------
    ; A SECOND pass, and the order is the whole reason it is one: pass A is
    ; what teaches assoc_note_app where each program is and what it looks
    ; like, and the listing is sorted by name (SPEC.md 19.4), so a document
    ; can perfectly well precede its program - BEACH.BMP before PAINT.O88.
    ; Composing in pass A would draw the page bare for everything above the
    ; program in the sort. No I/O here at all: both passes read data already
    ; in hand.
    xor dx, dx
.dp:
    cmp dx, bp
    jae .dp_done
    mov ax, dx
    call dsk_get_dir_x
    cmp word [si+16], 0         ; type 0 alone: a package draws its own icon
    jne .dp_next               ; and a folder has dsk_folder_ico
    pushf                       ; dsk_ico has FIVE tenants (assoc.inc says
                                ; which, above assoc_docicon)
    cli                         ; and this one stages it with the gfx lock
                                ; FREE, so a tick here could switch to a
                                ; package worker whose wm_show repaints the
                                ; dock through inst_icon_ptr - the fifth
                                ; tenant - and the document would cache 64
                                ; bytes of that package's icon. A compose
                                ; and a 64-byte copy, well under one tick
    call assoc_docicon_x          ; CF=0 and dsk_ico holds the composed body
    jc .dp_skip
    mov al, [assoc_docslot]     ; ...keyed by the SLOT it composed from, so
    call ico_key_doc            ; every document of one association is one row
    mov ax, dx
    mov bx, dsk_ico
    push dx
    mov dx, KERNEL_SEG          ; ...the kernel segment, named (SPEC.md 2.6
    call ico_put                ; rule 2: CS is COLD_SEG here)
    pop dx
.dp_skip:
    popf
.dp_next:
    inc dx
    jmp .dp
.dp_done:
    ret
%endif                          ; OS88_ASSOC
%endif                          ; DSK_WANT_ICONS

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

; -----------------------------------------------------------------------------
; dsk_get_dir - stage one synthesized directory entry in the kernel segment
; in:       AX = entry index (0..31; the caller validates against disk_nfiles)
; out:      SI -> dsk_ent, holding that entry's 32 bytes
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
dsk_get_dir_x:
    push ax
    push cx
    push dx
    push di
    call dsk_ent_ofs            ; SI = the entry's offset in [dsk_dseg]
    mov di, dsk_ent
    mov cx, DSK_DE_STRIDE       ; what the LISTING holds; dsk_ent is wider and
    mov dx, [dsk_dseg]          ; its 24..31 are left as they were, which no
                                ; reader of a staged entry looks at
    mov ax, KERNEL_SEG          ; ...the kernel segment, named (SPEC.md 2.6
                                ; rule 2: CS is COLD_SEG here)
    call dsk_copy_seg_x
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
dsk_find_name_x:
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
    call dsk_get_dir_x            ; SI -> dsk_ent, in our own segment
    mov di, dx
    call dsk_streq              ; CF=0 = the same name, to the NUL - a FOURTH
    jc .nextent                 ; copy of this compare until it had a home
    mov ax, bx
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
; dsk_ent_ofs - one listing slot's offset (module internal)
; in:       AX = entry index
; out:      SI = its offset inside [dsk_dseg]
; clobbers: SI (the output), flags
;
; A multiply and not a shift: DSK_DE_STRIDE is 24 and no longer a power of
; two. Writing it as one operation is what stops the two halves of the pair
; from disagreeing when the cap changes.
; -----------------------------------------------------------------------------
dsk_ent_ofs:
    push ax
    push cx
    push dx
    mov cx, DSK_DE_STRIDE
    mul cx
    add ax, [dsk_doff]
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
    mov cx, DSK_DE_STRIDE       ; the meaningful prefix; 19.1's 24..31 are
    mov ax, [dsk_dseg]          ; zero and the listing has no room for them
    mov dx, KERNEL_SEG          ; ...the kernel segment, named (SPEC.md 2.6
                                ; rule 2: CS is COLD_SEG here)
    call dsk_copy_seg_x
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
%ifdef DSK_WANT_ICONS           ; **THE DISK WINDOW'S, AND kern_dos HAS NO
                                ; WINDOW** (SPEC.md 96.44.7). It is also the
                                ; only thing in this file that claims heap,
                                ; and kerndos/kdos.asm includes disk.inc
                                ; without memory.inc - so MEM_K_ICO,
                                ; mem_claim_x and mem_movable_x are not
                                ; symbols there at all
; =============================================================================
; THE MACHINE-WIDE ICON STORE (SPEC.md 25.9)
; =============================================================================
; One 64-byte body per DISTINCT icon, for the whole machine rather than per
; listing and per window. A listing holds a one-byte REFERENCE per entry and
; owns no bodies at all.
;
; **THE IDENTITY IS THE DIRECTORY'S OWN (name, size)**, which costs nothing to
; read and is the same on every volume - so the copy of a package on the system
; disk and the copy on the apps disk are ONE row. Measured over the six shipped
; 360KB disks: 53 package copies, 29 distinct keys, 24 copies collapsed and no
; false merge (docs/plans/ICON-IDENTITY-PLAN.md 4). A CONTENT HASH was refused
; because it costs the very sector read SPEC.md 54.7's cache exists to avoid,
; and an id in the .o88 header because SPEC.md 20.2 fills all 32 bytes.
;
; Two entry kinds never take a row at all. A FOLDER references ICO_R_FOLDER,
; the built-in body in `.text`, which is why a DOS game directory - LEMMINGS 68
; entries, F15 66, TD1 41 - costs the store NOTHING. And a DOCUMENT's composed
; body (SPEC.md 54.3) is a pure function of its APP SLOT, so its key is a
; synthetic one and every document of one association shares a row.
;
; SIZED FOR THE MACHINE AND NOT FOR A VOLUME. Distinct package icons demandable
; at once, measured: 9 with the system disk alone, 26 with system + apps, 29
; with all six mounted, plus at most ASSOC_NAPP composed bodies. The file's own
; cap (ASC_NAPP) stays 32 because that is a VOLUME's question and the busiest
; holds 15.
ICO_R_NAME  equ 0               ; WHAT NAMES THE PACKAGE...
%ifdef OS88_ASSOC
ICO_R_SIZE  equ 8               ; ...the 8-BYTE SPACE-PADDED STEM on this
ICO_KEY     equ 10              ; kernel, which is SPEC.md 54.2's own shape -
                                ; the shape `ASSOC.DAT` stores and
                                ; `assoc_stem_of` answers - and the low word
                                ; of the size beside it.
                                ;
                                ; **IT LOSES NO IDENTITY.** A key is built for
                                ; a TYPE-1 entry and for nothing else
                                ; (`ico_ref_try` asks the type first), and
                                ; every package is a `.O88` (SPEC.md 20.1) -
                                ; so `<stem>` and `<stem>.O88` name exactly
                                ; the same set and the four extension bytes
                                ; carry no information at all.
                                ;
                                ; What it buys is that the two builders AGREE
                                ; BY CONSTRUCTION rather than by composition:
                                ; `ico_key_stem` copies the cache row's eight
                                ; bytes verbatim where it used to rebuild
                                ; `<STEM>.O88` and NUL-pad it, and
                                ; `ico_key_of` takes `assoc_stem_of`'s answer,
                                ; which is the same eight bytes from the other
                                ; end. 121 bytes of key builder against 84,
                                ; and ICO_ROW 86 -> 82 takes ICO_KB from 5 to
                                ; 4 - a KILOBYTE off a claim that stands
                                ; in front of a DOS program
%else                           ; ...and the 12-byte 8.3 NAME as SPEC.md 19.1
ICO_R_SIZE  equ 12              ; staged it on kern_small, which has no
ICO_KEY     equ 14              ; `ASSOC.DAT` (SPEC.md 54.0) and so ONE key
                                ; builder, nothing to agree with, and no
                                ; `assoc_stem_of` to call: measured, the stem
                                ; walk it would have to grow for itself is
                                ; **45 bytes against 22**, so the same change
                                ; that is worth 37 on the machine with memory
                                ; costs 23 on the machine with 128KB. That is
                                ; CLAUDE.md's own rule about `gfx_points`,
                                ; one feature along: the answer may differ per
                                ; build, and here it does
%endif
ICO_R_BODY  equ ICO_KEY         ; ...and THE BODY STRAIGHT BEHIND IT. It was
                                ; 16, two pad bytes past a 14-byte key, and
                                ; the only alignment that binds here is
                                ; dsk_copy_seg_x's `rep movsw` - which wants
                                ; an even offset, and ICO_KEY is one. Two
                                ; bytes a row, and one `add` in ico_add, which
                                ; now walks on from where the key left DI
%ifdef OS88_ASSOC
ICO_R_GLYPH equ ICO_R_BODY + DSK_ICO_SIZE   ; ...AND THE 8-BYTE SHIPPED GLYPH
ICO_ROW     equ ICO_R_GLYPH + 8 ; (SPEC.md 54.3.2), which is here because
                                ; SPEC.md
                                ; 54.7.4 freed the cache's claim: the glyph a
                                ; package SHIPS used to be read off the cache
                                ; ROW at a hit, and a hit has no row now. It
                                ; is not derivable from the body either -
                                ; that is the whole point of 54.3.2, the
                                ; reduction of a CRT icon being an empty
                                ; block - so a store that carried only the
                                ; body would answer a hit with the reduction
                                ; and DOWNGRADE a slot the seed had resolved.
                                ; 8 bytes x 48 rows = 384, against the 3,072
                                ; the claim gave back
%if ASC_ROWGLY - ASC_ROWICO != ICO_R_GLYPH - ICO_R_BODY
%error "asc_gly_pair walks an ASSOC.DAT row and a store row with ONE body of \
code: the body-to-glyph delta has to be the same in both"
%endif
%else                           ; ...and kern_small has no glyphs at all
ICO_ROW     equ ICO_R_BODY + DSK_ICO_SIZE   ; (SPEC.md 54.0), so it carries no
%endif                          ; column for them
%ifdef KERN_SMALL
ICO_NROW    equ 24              ; SPEC.md 51.0 leaves this kernel no
%else                           ; driver-backed volume and fewer packages
ICO_NROW    equ 48
%endif
ICO_KB      equ (ICO_NROW*ICO_ROW + 1023) / 1024

ICO_R_FOLDER equ 0xFE           ; not a row: the built-in folder body
ICO_R_NONE   equ 0xFF           ; ...and SPEC.md 25's generic icon

; -----------------------------------------------------------------------------
; ico_need - the store's claim, made on the first body that wants one
; out:      CF = 0 and ES = the claim; CF = 1 = no store, every lookup misses
; clobbers: ES (an output) and the flags
;
; LAZY on purpose: a machine that never opens a Disk window never claims it,
; and a refusal is the documented fallback rather than an error - every entry
; then references ICO_R_NONE and draws the generic icon (SPEC.md 25).
; -----------------------------------------------------------------------------
ico_need:
    push ax
    push bx
    push dx
    mov dx, [ico_seg]
    or dx, dx
    jnz .have
    mov ax, ICO_KB
    mov bx, MEM_P_ICO
    call mem_claim_x
    jc .no
    mov [ico_seg], dx
    mov byte [ico_n], 0
    mov ax, ico_reloc
    mov bx, MEM_P_ICO
    push dx                     ; ...and a refusal here only PINS it, which is
    call mem_movable_x          ; a compaction that achieves less rather than
    pop dx                      ; anything wrong (SPEC.md 66.2). The claim is
                                ; banked across it rather than re-read out of
                                ; [ico_seg], which is two bytes and asks
                                ; nothing about what that routine does to DX
.have:
    mov es, dx
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ico_demote - the shed took the machine-wide icon store (SPEC.md 25.9.5)
; out: nothing; DI is spent and so are the flags. **THAT IS THE CALLER'S
;      CONTRACT AND NOT A RELAXATION OF IT**: the only way in is
;      `mem_pg_forget`'s `.ico` arm, and that routine brackets every demoter
;      arm with its own `pushf` and `push ax / bx / cx / si` - so banking any
;      of those again here restores values its `.out` restores a second time
;
; `mem_pg_forget`'s `.ico` arm, `dsk_fatw_demote`'s twin and `fmv_demote`'s:
; this claim has MORE NAMING WORDS THAN THE PROTOCOL CARRIES, so the holder
; is handed the owner and clears them itself.
;
; **THE COUNT IS THE ONE THAT WAS MISSED.** The protocol zeroes `[ico_seg]`
; alone, and `[ico_n]` sits beside it saying how many rows the store holds -
; so after a shed the store was gone and still claiming to hold 28 bodies.
; That is not cosmetic: SPEC.md 54.7.4 put a second half on `asc_use`'s
; re-entry compare *for exactly this case* - `cmp byte [ico_n], 0`, so that a
; stamp still vouching for the volume cannot make the next mount skip the
; three-sector `ASSOC.DAT` re-read - and the byte it reads was never cleared,
; so the guard could not fire. The mount skipped the one read and then paid a
; sector per package instead, ~400 ms of `int 13h` each on the target machine.
; `ico_need` does zero it, but not until the first body the harvest wants,
; which is AFTER `asc_use` has already declined.
;
; And the reference bytes are the other half: every listing in the machine
; holds row numbers into what just went, which is `fmv_icostale`'s business.
; -----------------------------------------------------------------------------
ico_demote:
    mov word [ico_seg], 0       ; NEITHER A FRAME NOR A `pushf` HERE - see the
    mov byte [ico_n], 0         ; header: `mem_pg_forget` has banked the lot
    jmp fmv_icostale            ; already. And `fmv_icostale` ENDS IN A `ret`
                                ; whose caller is this routine's caller, so it
                                ; IS this routine's epilogue and the tail jump
                                ; is the whole of it. Both are `.cold`, so the
                                ; jump is near (SPEC.md 2.6); `fmv_icostale`
                                ; is a plain callable routine and not a
                                ; continuation, which is what SPEC.md
                                ; 22.3.0.1 rule 3 makes the difference about

; -----------------------------------------------------------------------------
; ico_find - which row holds ico_keyb's key? (module internal)
; in:       ico_keyb holds the key (ico_key_of, ico_key_stem or ico_key_doc)
; out:      CF = 0 and AL = the row; CF = 1 = not stored
; clobbers: AL (an output) and the flags
;
; **THE KEY IS ALWAYS ico_keyb, so it is not an argument.** It was SI, with a
; nine-byte `ico_have` in front of it whose whole body was `mov si, ico_keyb`,
; and `ico_put` and the absorb each writing that same load before their own
; `ico_add`. Three loads of one constant for a routine no caller can pass
; anything else to - the key builders write THERE and nowhere else.
;
; TWO OTHER THINGS CAME OUT WITH IT. `[ico_n]` is compared in place rather
; than staged into DL, which deletes the load AND the DX the load had to be
; preserved in; and a full `repe cmpsb` that ends EQUAL leaves CF = 0 from its
; own last compare, so the hit arm needs no `clc` and no jump over the miss.
; -----------------------------------------------------------------------------
ico_find:
    push di
    push es
    call ico_scan
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; ico_scan - ...and WHERE it is, which ico_add wants and nobody else does
; in:       ico_keyb holds the key
; out:      CF = 0 and AL = the row, with ES:DI standing ON it;
;           CF = 1 and AL = [ico_n], ES:DI standing on the first FREE row -
;           which is past the end when the store is full, so `cmp al,
;           ICO_NROW` is the whole of that test. With NO store at all AL is
;           0xFF and ES:DI mean nothing, and the same compare refuses that too
; clobbers: AL, ES and DI (outputs) and the flags
;
; **ico_add RE-DERIVED ALL THREE OF THESE.** It called `ico_need` a second
; time, read `[ico_n]` a second time and ran `ico_rowoff` over it - when the
; scan that had just missed ended standing exactly on the row it was about to
; take. The walk is the same walk; only the two answers past AL were being
; thrown away.
; -----------------------------------------------------------------------------
ico_scan:
    push cx
    push si
    call ico_need
    jc .nostore
    xor al, al
    xor di, di
    mov si, ico_keyb
    cld                         ; ...ONCE, not per row: nothing below sets DF
.row:
    cmp al, [ico_n]
    jae .no
    push si
    push di
    mov cx, ICO_KEY
    repe cmpsb                  ; DS:SI is the key, ES:DI the row - DS is the
    pop di                      ; kernel segment (SPEC.md 1) and ES the claim
    pop si
    je .out                     ; ...and CF = 0 already, from the compare that
    add di, ICO_ROW             ; made them equal. `pop` writes no flag
    inc al
    jmp short .row
.nostore:
    mov al, 0xFF                ; no claim: past ICO_NROW, so the "is it full"
.no:                            ; test refuses this without a second question
    stc
.out:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; ico_rowoff - DI = row AL's offset in the claim (module internal)
; in:       AL = the row
; out:      DI = its offset; clobbers DI and the flags
;
; **THE 8-BIT MULTIPLY, NOT A SHIFT LADDER.** It was three shifts and two adds
; keyed on ICO_ROW's value - which meant a `%if` arm per row width, and 80 and
; 88 needed different ones. `mul ah` takes ANY width in two bytes: 9 bytes
; against 23, and the product cannot leave AX because the store's widest row
; is ICO_NROW-1 = 47 of them (4,136, and 255 x 88 would still fit). The
; 8088 prices `mul r8` at ~70 clocks against the ladder's ~50, and this is
; called once per icon RESOLVED - 32 of them on a full listing, so 0.2 ms of
; a repaint that spends 71 ms on one row of text (PERFORMANCE.md part 2).
; assoc.inc's asc_row already uses this form and says so in as many words.
; -----------------------------------------------------------------------------
ico_rowoff:
    push ax
    mov ah, ICO_ROW
    mul ah                      ; AX = AL * AH, and AH is a constant
    mov di, ax
    pop ax
    ret

%ifdef OS88_ASSOC
; -----------------------------------------------------------------------------
; ico_glyph_put - a row's SHIPPED glyph, from wherever it was read
; in:       AL = the row, DX:SI -> eight glyph bytes
; out:      nothing (all registers preserved)
;
; Separate from ico_add rather than a fifth argument to it, because the two
; have different sources and only sometimes both: a body comes off a package
; header or a cache row, a glyph only off a version 2 cache row or a v3 image
; that ships one. A row whose glyph is never put keeps the zeros ico_add
; wrote, which is exactly SPEC.md 54.2's unresolved sentinel.
; -----------------------------------------------------------------------------
ico_glyph_put:
    push ax
    push cx
    push si
    push di
    push es
    call ico_body               ; ...WHICH IS THIS ROUTINE'S OWN ADDRESSING,
    jc .out                     ; and it was written out a second time here:
    add di, ICO_R_GLYPH - ICO_R_BODY    ; the claim, the stale-row guard and
    mov cx, 8                   ; the row offset are three things `ico_body`
    mov ax, es                  ; already does, and the glyph is a fixed
    call dsk_copy_seg_x         ; distance past what it answers. DX = source
.out:                           ; segment, AX = the claim
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

%endif                          ; OS88_ASSOC

; -----------------------------------------------------------------------------
; ico_add - store a body under ico_keyb's key, or find the row that has it
; in:       ico_keyb holds the key, DX = the body's segment, BX = its offset
; out:      CF = 0 and AL = the row; CF = 1 = no store, or it is full
; clobbers: AL (an output) and the flags
; -----------------------------------------------------------------------------
ico_add:
    push bx
    push cx
    push dx
    push di
    push si
    push es
    call ico_scan               ; IDEMPOTENT: the second copy of a package is
    jnc .fin                    ; the same row, which is the whole point - and
                                ; a MISS leaves ES:DI standing on the row this
                                ; would take, which is the whole of why the
                                ; claim, the count and the row offset are not
                                ; asked for again here
    cmp al, ICO_NROW
    jae .no                     ; full, or no store at all: SPEC.md 25's
                                ; generic icon, and the caller reads CF=1 as
                                ; exactly that
    push ax
    mov si, ico_keyb
    mov cx, ICO_KEY
    cld
    rep movsb                   ; the KEY, DS:SI -> ES:DI
%if ICO_R_BODY != ICO_KEY
    add di, ICO_R_BODY - ICO_KEY
%endif                          ; ...AND DI IS ALREADY THERE: `rep movsb` left
                                ; it ICO_KEY along and the body starts at
                                ; ICO_KEY, so no row base is re-derived and no
                                ; `add` is emitted. The `%if` is what keeps
                                ; that true if the layout ever gains a pad
    mov si, bx                  ; ...then the BODY, out of its own segment
    mov cx, DSK_ICO_SIZE
    mov ax, es
    call dsk_copy_seg_x         ; DX = source segment, AX = the claim
%ifdef OS88_ASSOC
    add di, DSK_ICO_SIZE        ; ...and the glyph column CLEARED - STRAIGHT
    mov cx, 8                   ; PAST THE BODY, dsk_copy_seg_x having
    xor al, al                  ; preserved DI - because mem_claim does not
.gz:                            ; clear the block and an UNRESOLVED glyph is
    mov [es:di], al             ; all-zero by definition (SPEC.md 54.2):
    inc di                      ; whatever the heap handed out last would
    dec cx                      ; otherwise read as a shipped one
    jnz .gz
%endif
    pop ax
    inc byte [ico_n]
    clc
    jmp short .fin
.no:
    stc
.fin:
    pop es
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ico_body - where row AL's body lives
; in:       AL = the row
; out:      CF = 0, ES = the claim and DI = the body's offset in it;
;           CF = 1 = no store
; clobbers: ES and DI (outputs) and the flags
; -----------------------------------------------------------------------------
ico_body:
    call ico_need
    jc .out
    cmp al, [ico_n]             ; **A STALE REFERENCE AFTER A SHED** (SPEC.md
    cmc                         ; 50.6): the claim is purgeable, so a listing
    jc .out                     ; staged before it went holds row numbers that
    call ico_rowoff             ; no longer exist - and ico_need has just
    add di, ICO_R_BODY          ; re-claimed an EMPTY store, where row N's
    clc                         ; offset is 64 bytes of whatever the heap
.out:                           ; handed out. CF=1 draws the generic icon,
    ret                         ; which is what every other refusal here
                                ; already means - and the compare ALREADY
                                ; answers it: `cmp` borrows when the row is in
                                ; range, so one `cmc` is the arm that was a
                                ; branch, a `stc` and a jump

section .text                   ; ...AND THE KERNEL SEGMENT FOR BOTH OF THESE
                                ; (SPEC.md 2.6 rule 1 for the data, and
                                ; SPEC.md 66.2 for the proc)
; -----------------------------------------------------------------------------
; ico_reloc - the store is MOVABLE (SPEC.md 66.2)
;
; **IT HAS TO BE IN `.text` AND IT WAS IN `.cold`.** mem_compact loads
; `[ss:si+MC_RLOC]` into BP and calls it THROUGH THE OWNER'S SEGMENT, which for
; a MEM_K_* tag is the kernel's - so a COLD_SEG offset called there lands on
; whatever kernel code happens to sit at that offset. Silent, and only on a
; compaction: `tests/regpin.py` caught it as *"the pass moved nothing"*, which
; is what a jump into the middle of an unrelated routine looks like from
; outside. assoc.inc's asc_reloc is in `.text` for exactly this reason.
; -----------------------------------------------------------------------------
ico_reloc:
    cmp bx, [ico_seg]
    jne .out
    mov [ico_seg], dx
.out:
    ret

ico_seg:    dw 0                ; the store's claim, 0 = none (refused, or no
                                ; listing has wanted a body yet)
ico_n:      db 0                ; rows in use. It only grows: the store is
                                ; retired by a PURGE and not by a mount, which
                                ; is what makes it machine-wide
section .cold

; -----------------------------------------------------------------------------
; ico_key_of - bank this entry's identity before the harvest overwrites it
; in:       SI -> the staged 19.1 entry
; out:      ico_keyb holds it; nothing else (all registers preserved)
; -----------------------------------------------------------------------------
ico_key_of:
    push ax
    push cx
%ifdef OS88_ASSOC
    push si
    push di
    mov ax, [si+20]             ; the size FIRST, while SI is still the entry
    mov [ico_keyb+ICO_R_SIZE], ax
    call assoc_stem_of          ; ...and SPEC.md 54.2's stem of its name, which
                                ; is `assoc_stemb` and leaves SI alone. THE
                                ; SAME EIGHT BYTES `ico_key_stem` reads out of
                                ; a cache row, which is the whole of why an
                                ; absorbed body and a harvested one land on
                                ; one row
    mov si, di
    mov di, ico_keyb
    mov cx, 8
    call dsk_ncopy              ; DS:SI -> DS:DI, SPEC.md 4's counted copy
    pop di
    pop si
%else
    push di
    mov di, ico_keyb
    mov cx, 12                  ; the 8.3 name as 19.1 staged it, NUL-padded
    call dsk_ncopy              ; DS:SI -> DS:DI, THROUGH DS BOTH WAYS and not
                                ; `rep movsb`: ico_keyb is `.bss` and so the
                                ; KERNEL segment, while the harvest runs with
                                ; ES = SS (it does `push ss / pop es` before
                                ; the loop). A string move here wrote twelve
                                ; bytes into `.lowbss` instead - measured, it
                                ; corrupted the very reference array it was
                                ; meant to feed. THAT IS SPEC.md 4's OWN
                                ; COUNTED COPY, which preserves SI as well as
                                ; DI - so the entry is still addressable below
                                ; and this routine banks SI not at all, where
                                ; the hand-rolled loop had to and got the pop
                                ; order wrong once doing it
    mov ax, [si+20]
    mov [ico_keyb+ICO_R_SIZE], ax
    pop di
%endif
    pop cx
    pop ax
    ret

%ifdef OS88_ASSOC               ; kern_small composes no document icon at all
                                ; (SPEC.md 54.0), so its only caller is out
; -----------------------------------------------------------------------------
; ico_key_doc - the SYNTHETIC key a composed document body is stored under
; in:       AL = the app slot it was composed from
; out:      ico_keyb holds it; nothing else (all registers preserved)
;
; A real SPEC.md 19.1 name is a NUL-terminated 8.3 of printable characters, so
; a leading 0xFF cannot collide with one.
; -----------------------------------------------------------------------------
ico_key_doc:
    push ax
    push bx
    push cx
    push si
    push di
    mov byte [ico_keyb], 0xFF
    mov [ico_keyb+1], al
    mov bl, al                  ; ...AND THE GLYPH IT COMPOSED FROM, which is
    call assoc_glyph_di         ; the half that was missing (BH is zeroed
                                ; there, so BL alone is enough). The body this
                                ; key names is assoc_compose's OUTPUT, so it is
                                ; derived from state that MOVES - the baked
                                ; table, the seed, a hit and a harvest all
                                ; write assoc_glyph - and a row keyed on the
                                ; SLOT alone goes on answering with the
                                ; picture composed from whatever the glyph
                                ; used to be. SPEC.md 54.7.3's own complaint,
                                ; one layer along: a slot assoc_app_new
                                ; created UNRESOLVED composes the bare page,
                                ; and without this the documents of that
                                ; association keep drawing it for the rest of
                                ; the session even after the folder their
                                ; program lives in is browsed. Keyed on the
                                ; glyph, a changed glyph is simply a different
                                ; row and the stale one is never asked for
                                ; again - it costs a row rather than a wrong
                                ; picture, and the shed reclaims it.
                                ;
                                ; WHERE a slot's glyph lives is
                                ; `assoc_glyph_di`'s question, and it was
                                ; asked again here, shift and all; the copy is
                                ; SPEC.md 4's counted one, which is DS to DS
                                ; and so right for a harvest running with
                                ; ES = SS
    mov si, di
    mov di, ico_keyb+2
    mov cx, 8
    call dsk_ncopy
%if ICO_KEY > 10
    xor ax, ax                  ; ...and the tail, to the end of the key: a
    mov [ico_keyb+10], ax       ; composed body reports no size, and two word
    mov [ico_keyb+12], ax       ; stores are shorter than the loop that was
%endif                          ; here - which needed no string op either, for
                                ; the ES = SS reason above. AND THERE IS NO
                                ; TAIL on a 10-byte key: 0xFF, the slot and
                                ; eight glyph bytes ARE the whole of it
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif                          ; OS88_ASSOC

%ifdef OS88_ASSOC               ; its only caller is asc_row_take, and kern_small
                                ; has no ASSOC.DAT to absorb (SPEC.md 54.0)
; -----------------------------------------------------------------------------
; ico_key_stem - the key an ASSOC.DAT row's body is stored under (SPEC.md 25.9.4)
; in:       ES:SI -> the row's 8-byte space-padded stem, DX = the size it
;           reports
; out:      ico_keyb holds it; nothing else (all registers preserved)
;
; THE SAME TWELVE BYTES ico_key_of BUILDS, composed from the other end, and
; that identity is the whole of why ASSOC.DAT's buffer can be given back. A
; row names a package by STEM (SPEC.md 54.2) and every package is a `.O88`
; (20.1), so `<STEM>.O88` NUL-padded to twelve is exactly what the listing
; will ask with when it reaches that entry. Composing it means an ABSORBED
; body and a HARVESTED one land on one row and answer one another's lookups -
; where a second key shape would have made them two rows holding one picture,
; which is the duplication this whole design is against.
; -----------------------------------------------------------------------------
ico_key_stem:
    push cx
    push si
    push di
    mov di, ico_keyb
    mov cx, 8
    call assoc_stage            ; ES:SI -> DS:DI, SPEC.md 20.2's staging idiom.
                                ; VERBATIM, where this used to walk the stem to
                                ; its first space, write `.O88` after it and
                                ; NUL-pad the rest to the size word - a
                                ; reconstruction of what `ico_key_of` would
                                ; build, which had to be got exactly right or
                                ; the two keys for one package differed in
                                ; their tail and the cache silently did not
                                ; work. Both sides copy the same eight bytes
                                ; now, so there is nothing left to get wrong
    mov [ico_keyb+ICO_R_SIZE], dx
    pop di
    pop si
    pop cx
    ret
%endif                          ; OS88_ASSOC

; -----------------------------------------------------------------------------
; ico_ref_set - entry AX references body BL (module internal)
; in:       AX = entry index, BL = a row, ICO_R_FOLDER or ICO_R_NONE
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
ico_ref_set:
    push si
    push es
    mov si, ax
    add si, [dsk_ioff]          ; THE INDEX FOLLOWS THE LISTING into whatever
    mov es, [dsk_dseg]          ; store the caller supplied (13.1). It was
    mov [es:si], bl             ; `[ss:si+dsk_icoix]` while every listing was
    pop es                      ; in `.lowbss`; the entries and their
    pop si                      ; reference bytes are one block now, so the
    ret                         ; two halves cannot name different segments

; -----------------------------------------------------------------------------
; ico_put - store this entry's body and record the reference
; in:       AX = entry index, DX = the body's segment, BX = its offset,
;           ico_keyb = the key
; out:      AL = the row it took, or ICO_R_NONE; clobbers AX
;
; THE ROW IS AN OUTPUT because a caller may have more to say about this
; package than its body - the 8-byte glyph it SHIPS (SPEC.md 54.3.2) - and
; nothing else here can tell it which row to say it about.
;
; AX is pushed LAST so it pops FIRST, after ico_add's answer has been banked
; into BL - the entry index and the row number both want AL, and unwinding in
; the other order silently stores the index as the reference.
; -----------------------------------------------------------------------------
ico_put:
    push bx                     ; TWO REGISTERS, where it banked six: `ico_add`
    push ax                     ; clobbers AL alone and `ico_ref_set` nothing
                                ; at all, so CX, DX, SI and DI were pushed and
                                ; popped here against callees that already
                                ; promise them
    call ico_add                ; DX:BX is the body, and ico_keyb the key
    mov bl, ICO_R_NONE
    jc .set                     ; no store, or full: SPEC.md 25's generic icon
    mov bl, al
.set:
    pop ax                      ; ...the entry index, and AL is free again
    call ico_ref_set
    mov al, bl                  ; ...and the row is what the caller is told
    pop bx
    ret

; -----------------------------------------------------------------------------
; ico_ref_try - settle entry DX's reference WITHOUT reading a sector
; in:       DX = the entry index, SI -> its staged SPEC.md 19.1 entry
; out:      CF = 0 = the reference is set and this entry is done;
;           CF = 1 = a package whose body is NOT in the store, so the caller
;           reads its first sector or gives the entry up
; clobbers: AL and the flags
;
; **THE MOUNT HAS TWO ICON PASSES AND THIS IS THE HALF THEY SHARE.** Step 4
; walks a real volume and step 4a' a REDIRECTED one (SPEC.md 62.9.2.1), and
; the classification in front of both is identical to the instruction: bank
; the identity before `.h_read` can stage a package header over it, send a
; folder to the built-in body in `.text`, leave a document to pass 4b, and
; spend a store hit. They differed only in what a MISS means - a sector read
; on one pass, nothing at all on the other - which is exactly what a carry
; says, so the second pass ignores it and the first branches on it.
; -----------------------------------------------------------------------------
ico_ref_try:
    push ax
    push bx
    mov ax, [si+16]             ; the type, read ONCE (SPEC.md 19.5)
    cmp ax, 2                   ; a folder or the parent link: the one icon
    jae .folder                 ; that is not harvested off the disk but is
    dec ax                      ; built in. Below it, 1 is a package and 0 a
    jnz .done                   ; document, whose blank slot already IS
                                ; SPEC.md 25's generic icon and which pass 4b
                                ; may compose over - and `dec` is one byte
                                ; where a second `cmp` is four
    call ico_key_of             ; BANK ITS IDENTITY NOW (SPEC.md 25.9):
                                ; .h_read stages the package's own header over
                                ; dsk_ent, so this is the last moment the name
                                ; is here - and AFTER the type tests, not in
                                ; front of them: it is the PACKAGE arm that
                                ; needs it, and a DOS directory of 68 entries
                                ; with no package in it was banking twelve
                                ; bytes and a size word sixty-eight times for
                                ; nobody
    call ico_find               ; already in the store (SPEC.md 25.9)? -
    jc .miss                    ; absorbed from this volume's ASSOC.DAT
                                ; (54.7.4), or harvested on an earlier mount,
                                ; or the same package on a disk already
                                ; browsed. Then it costs NO sector read, and
                                ; that is BOTH kernels now: the question is
                                ; the store's and kern_small has a store
%ifdef OS88_ASSOC
    call asc_take_x             ; ...and the location and glyph the read would
%endif                          ; have given (54.7)
    mov bl, al
    jmp short .set
.folder:                        ; every folder REFERENCES the built-in body,
    mov bl, ICO_R_FOLDER        ; which is already in `.text` - so it takes no
.set:                           ; row, no copy and no byte of the store, and a
    mov ax, dx                  ; DOS game directory costs it nothing at all
    call ico_ref_set
.done:
    clc
    jmp short .out
.miss:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsk_ico_stage - stage the body a REFERENCE names into dsk_ico
; in:       AL = a row, ICO_R_FOLDER or ICO_R_NONE
; out:      SI -> dsk_ico, holding 64 bytes (all-zero = no icon)
; clobbers: SI (the output) and the flags
;
; THREE PLACES A BODY CAN BE, which is what the reference byte buys: the
; store's claim, the kernel segment for the one built-in body every folder
; shares, and nowhere at all for the generic icon. It takes a REFERENCE rather
; than an entry index because a WINDOW holds its own reference bytes (SPEC.md
; 22.6.1): a background window's listing may be a volume the global snapshot
; has since replaced, so it answers from its OWN byte and then resolves that
; byte against the one store, which is exactly what makes the store
; machine-wide rather than per volume.
;
; **IT WAS TWO ROUTINES WITH A CARRY BETWEEN THEM** - one answering *where*
; and one *stage it, or blank it* - for a single caller six instructions
; along: a whole frame, a call and a flag to hand over an address.
; -----------------------------------------------------------------------------
dsk_ico_stage:
    push ax
    push cx
    push dx
    push di
    push es
    cmp al, ICO_R_FOLDER        ; ONE COMPARE FOR BOTH SENTINELS: 0xFE and
    jae .built                  ; 0xFF are the top two values, so everything
    call ico_body               ; below them is a store row
    jc .none
    mov si, di
    mov dx, es
    jmp short .copy
.built:
    ja .none                    ; 0xFF: SPEC.md 25's generic icon
    mov si, dsk_folder_ico      ; every folder shares the one built-in body
    mov dx, KERNEL_SEG          ; ...the kernel segment, named (SPEC.md 2.6
.copy:                          ; rule 2: CS is COLD_SEG here)
    mov di, dsk_ico
    mov cx, DSK_ICO_SIZE
    mov ax, KERNEL_SEG
    call dsk_copy_seg_x
    jmp short .out
.none:                          ; SPEC.md 25.8: no body, so hand back the
    mov di, dsk_ico             ; all-zero every caller already reads as "no
    mov cx, DSK_ICO_SIZE/2      ; icon". THROUGH DS and not a `rep stosw`:
    xor ax, ax                  ; dsk_ico is `.bss` and so KERNEL_SEG, which
.bz:                            ; DS already is (SPEC.md 1), and setting ES
    mov [di], ax                ; here would cost more than the loop
    inc di
    inc di
    loop .bz
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    mov si, dsk_ico
    ret

; -----------------------------------------------------------------------------
; dsk_get_icon - stage one harvested icon slot in the kernel segment
; in:       AX = entry index (0..31)
; out:      SI -> dsk_ico, holding that entry's 64 bytes (all-zero = no icon)
; clobbers: SI (the output), flags
;
; The INDEX form of the routine above, and the whole of the difference: the
; GLOBAL reference array answers for this entry, where a window with a cache
; of its own reads its own byte (SPEC.md 22.6.1) and hands it over directly.
; -----------------------------------------------------------------------------
dsk_get_icon_x:
    push ax
    push es
    mov si, ax
    add si, [dsk_ioff]          ; ...and reads it back out of the same store
    mov es, [dsk_dseg]          ; (ico_ref_set above)
    mov al, ICO_R_NONE          ; **NO STORE, NO REFERENCE**: a mount with no
    cmp word [dsk_dseg], 0      ; destination built no listing, so there is
    je .none                    ; nothing to have a reference IN - and the
    mov al, [es:si]             ; segment this would read is 0, which is the
.none:                          ; interrupt vector table drawn as an icon
    pop es
    call dsk_ico_stage
    pop ax
    ret
%endif                          ; DSK_WANT_ICONS

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above, so every routine here keeps its
; ordinary near `ret`.
; =============================================================================
dkf_dsk_batch_begin: call dsk_batch_begin_x
                 retf
dkf_dsk_chdir:       call dsk_chdir_x
                 retf
dkf_dsk_chdir_q:     call dsk_chdir_q_x
                 retf
dkf_dsk_copy_seg:    call dsk_copy_seg_x
                 retf
dkf_dsk_vol_fixed:   call dsk_vol_fixed_x
                 retf
dkf_dsk_vol_slot:    call dsk_vol_slot_x
                 retf


; =============================================================================
; PARKING THE HEADS ON THE WAY OUT (SPEC.md 18.100)
;
; `int 19h` RESETS NO HARDWARE, and §18.97's probe decides on a LEVEL READ of
; the drive's own TRACK 0 line - so the next boot reads the head where this
; session left it. A session that touched drive B: leaves it off track 0, the
; probe's one-instruction fast path is missed on every restart after that, and
; a head parked above cylinder 77 hands the probe the exact ST0 signature
; §18.97.4 retires a drive on. §18.97.4 recorded that as a warm-restart
; property and fixed the READ; this fixes the STATE, one step earlier.
;
; It is the ROM's own `int 13h AH=00h` - RESET DISK SYSTEM, which recalibrates
; the head to track 0 - once per claimed unit. The BIOS does the handshake,
; the wait and the retry; this reads no result, because the `int 19h` two
; instructions along resets the FDC whatever state it is left in. Verified to
; move the head from `ST3 = 29` (TRK0 clear) to `39` (set) on BOTH the field
; GLaBIOS and the genuine IBM 5150 27-Oct-82 ROM §18.97.4 was reported on.
;
; A recalibrate needs no MEDIA - it is a head-carriage move, not a read - so a
; Restart whose dialog has just told the user to take the floppy OUT (§52.10.6)
; parks the empty drive's head all the same, which an `AH=02h` read could not.
;
; Reached only from `ui_cmd_reboot`, AFTER `sched_unhook`: the scheduler is
; down by then, so no task can be switched onto a different stack in the middle
; of the BIOS call - which is the one thing `dsk_dbg_raw`'s `sch_lock` guards
; and this does not need. In .cold (§2.6): resident, outside KERN_CODE_MAX, run
; once per session, and small enough that no rung moves.
;
; IT CANNOT BE AN ON-DEMAND MODULE (§2.8): a module is read off the system
; disk, and the caller has just asked for the floppy to come OUT.

; -----------------------------------------------------------------------------
; dsk_fdd_park_x - FAR: recalibrate every claimed floppy head to track 0.
; in:  nothing;  out: DOES NOT RETURN - the `int 0x19` at its tail is the
;      last instruction of the GUI.  clobbers: AX, CX, DX, ES, flags
;
; EVERY UNIT THE EQUIPMENT WORD CLAIMED, not every unit with a volume row - so
; a drive §18.97.2 wrongly retired, which has no row, is still recalibrated and
; handed back on the next boot rather than lost on every restart after. int 11h
; claims it afresh each boot; park what int 11h claims.
; -----------------------------------------------------------------------------
%ifndef NO_FDDPARK
dsk_fdd_park_x:
    mov cl, [fdd_dbg_eqp]       ; drives int 11h claimed (0..4), banked by
                                ; desk_init before anything contested it (§57.5)
.unit:
    dec cl                      ; ...counted DOWN, so 0 claimed falls straight
    js  dsk_rb_go               ; through the same test the last unit exits on
    mov dl, cl                  ; this unit; DH is irrelevant to AH=00
    xor ah, ah                  ; AH=00 RESET DISK SYSTEM = recalibrate to trk0
    int 0x13                    ; the ROM handshakes, waits and retries. No
                                ; result is read: the int 19h below resets the
                                ; FDC whatever this leaves behind (§18.100)
    jmp short .unit             ; the ORDER units are parked in does not matter
dsk_rb_go:                      ; the last instruction of the GUI, in .cold
    int 0x19
    jmp $
%endif                           ; NO_FDDPARK

; =============================================================================
; THE FLOPPY-PRESENCE PROBE (SPEC.md 18.97)
;
; In the BOOT OVERLAY (SPEC.md 2.5), which is where this belongs twice over:
; it is asked once and never again, and the overlay costs no footprint and no
; KERN_CODE_MAX at all - it is read into the FAT window and written over by
; drv_boot's first mount. clk_init's RTC ladder and drv_snd_sniff's OPL2 dance
; are the precedents; this is the same shape, a cheap port-level probe run
; before the first paint to settle a default the machine can answer better
; than a configuration byte can.
;
; Reached by a near call from desk_init, which is also .ovl (same section, same
; vstart), and disk.inc is %included first so it is a backward reference.
; =============================================================================
%ifndef KD_BUILD                    ; **THE BOOT OVERLAY IS NOT IN kern_dos**
                                    ; (SPEC.md 96.43.2): there is no kmain to
                                    ; run it, the DPT arrives in the launch
                                    ; block (96.40.2) and the volume the
                                    ; program came off is named rather than
                                    ; enumerated - so nothing in that root
                                    ; calls one of these and they are 762
                                    ; bytes of a build whose every byte is a
                                    ; byte off the DOS program
section .ovlw
%ifndef NO_FDDPROBE              ; `make FDDPROBE=0` takes the BODY out too, not
                                 ; just desk_init's call: the A/B is meant to be
                                 ; the pre-18.97 kernel, and 381 bytes of
                                 ; uncallable overlay is 381 bytes the boot
                                 ; sector still reads off the floppy

FDC_DOR      equ 0x3F2           ; digital output: motors, select, reset, DMA
FDC_MSR      equ 0x3F4           ; main status
FDC_FIFO     equ 0x3F5           ; data
FDC_M1       equ 0x20            ; DOR bit 5: unit 1's motor
fdd_mbit:    db 0x10, 0x20, 0x40, 0x80  ; ...and one per unit (SPEC.md 18.98):
                                 ; DOR bits 4..7 are motor enable for A..D,
                                 ; which is the same four the BIOS drives

FDD_HANDTO   equ 12000           ; spins waiting for RQM on one byte. Each is
                                 ; an `in`, a mask, a compare and a branch -
                                 ; call it 4us on a 4.77MHz 8088, so ~50ms,
                                 ; against a handshake that really takes
                                 ; microseconds. A ceiling, not a delay
FDD_SEEKW    equ 20              ; ticks (~1.1s) to let the recalibrate finish.
                                 ; 77 steps at the DPT's rate is about half a
                                 ; second, so this is generous on purpose - and
                                 ; only a machine heading for the absent verdict
                                 ; ever waits it out
FDD_SIN      equ 6               ; sense-interrupt results to drain before
                                 ; giving up on the queue ever emptying: four
                                 ; is one per drive after a reset, and the
                                 ; bound is what stops a controller answering
                                 ; 0FFh from spinning here for ever
FDD_MOTORW   equ 12              ; ticks (~660ms) for the motor line to take
                                 ; effect. NOT dsk_dpt+10's 8 x 1/8 s: nothing
                                 ; here reads a disk, so what this waits for is
                                 ; a gate coming off, not a spindle reaching
                                 ; speed - and only a machine heading for the
                                 ; absent verdict ever pays it
FDD_SELW     equ 4000            ; a short settle after the select reaches the
                                 ; cable, before ST3 means anything

; -----------------------------------------------------------------------------
; dsk_fdd_probe - is unit 1 really there? (SPEC.md 18.97)
; in:  nothing
; out: AL = 0 the drive is ABSENT, 1 keep it
; clobbers: AX, flags
;
; THE DISCRIMINATOR IS TRACK 0, and the reason is mechanical: every
; media-dependent signal answers the same for "no drive" and "drive with no
; disk in it" - an empty 5.25" DD drive delivers no index pulses, so a read or
; a verify times out identically either way. TRK0 is a position sensor on the
; head carriage, so a drive that is there asserts it with or without media,
; and a select line with nothing on the end of it is held inactive by the
; controller's own pull-up.
;
; EVERY FAILURE KEEPS THE DRIVE. The two errors are not symmetric - a phantom
; icon costs a wasted click, a hidden real drive costs the user their second
; floppy - so this can only ever remove what int 11h claimed, never add one,
; and it is only ever asked about unit 1. Unit 0 is where the kernel just came
; from and is not a question.
;
; THE MOTOR IS THE SECOND STEP AND NOT THE FIRST, which is the whole of why a
; correctly-switched two-drive machine pays nothing: a drive parked at track 0
; answers ST3 with no motor at all, in microseconds. Only an inconclusive read
; spins one up and asks again - so the machine about to make the DESTRUCTIVE
; decision is the one that pays for the most favourable conditions to make it
; under, and a drive that (against every datasheet) needs its spindle turning
; before it will report TRK0 still gets a second chance.
;
; IT DISTURBS NOTHING THE BIOS BELIEVES: no controller reset (that would
; invalidate unit 0's calibration and cost the next read a recalibrate), the
; DOR restored with the motor bits it was found with, and the only BIOS
; variable written is 0040:003E - bit 1, so unit 1 is re-seeked if it is ever
; used, and bit 7, which our own recalibrate's IRQ6 set.
; -----------------------------------------------------------------------------
dsk_fdd_probe:
%ifdef FDD_FORCE_ABSENT
    ; `make FDDABSENT=1` (SPEC.md 18.97.2): answer ABSENT for unit 1 with no
    ; port touched at all. NOT shipped, and it stubs the CONVERSATION only -
    ; the block is filled exactly as the real absent path fills it, so
    ; desk_init's decision, the retire, the published bytes and sysbench's
    ; report are all the genuine article. It exists because no emulator here
    ; can produce an absent verdict, so without it the one path that removes a
    ; drive - and 18.97.2's tier test on top of it - can only ever be reasoned
    ; about, which is what put this bug in the field in the first place.
    cmp al, 1                   ; unit 1 only: the externals are 18.98.1's and
    jne .real                   ; are never judged, so forcing them proves
                                ; nothing and would hide their real answers -
                                ; and having branched on it, this addresses
                                ; unit 1's row by NAME rather than deriving it
                                ; again, which is the encoding 18.98 spent a
                                ; paragraph removing
    or byte [fdd_dbg_ran], 2    ; bit 1 = unit 1 was asked
    mov byte [fdd_dbg_u+FDU_ST3],  0x21 ; ST3 is 21 on BOTH field machines -
    mov byte [fdd_dbg_u+FDU_ST3B], 0x21 ; that is 18.97.3's whole point - so
                                        ; only ST0 below differs between them
%if FDD_FORCE_ABSENT == 2
    mov byte [fdd_dbg_u+FDU_ST0],  0x21 ; FDDABSENT=2: the PACKARD BELL's
                                        ; present 1.2MB drive - IC 00, SE, no
    mov byte [fdd_dbg_u+FDU_STEP], FDD_S_SEEKST0    ; EC, unit 1. Kept on EVERY
    mov byte [fdd_dbg_u+FDU_VRD],  1                ; tier, 18.97.3's branch
    mov al, 1
%else
    mov byte [fdd_dbg_u+FDU_ST0],  0x71 ; FDDABSENT=1: the 5150's genuinely
                                        ; absent drive - IC 01, SE, EC, unit 1
    mov byte [fdd_dbg_u+FDU_STEP], FDD_S_NOTRK0
    mov byte [fdd_dbg_u+FDU_VRD],  0
    xor al, al
%endif
    ret
.real:
%endif
    push bx
    mov [fdd_unit], al          ; SPEC.md 18.98: which unit. It was unit 1 by
                                ; construction - three encodings of it, in the
                                ; DOR select, the motor bit and RECALIBRATE's
                                ; argument, plus a fourth inside fdd_st3
    push cx
    push dx
    push si
    push es

    ; SI = this unit's row in the published block, for the whole routine
    ; (SPEC.md 57.5). Held in a register rather than recomputed because every
    ; store below used to be an absolute address and a fifth encoding of "unit
    ; 1" is exactly what 18.98 spent a paragraph removing. None of fdd_dor /
    ; fdd_st3 / fdd_put / fdd_get / fdd_res / fdd_sidrain / fdd_wait touches SI.
    mov bl, al
    xor bh, bh
    mov si, bx
    shl si, 1
    shl si, 1
    add si, bx                  ; unit * FDU_SIZE...
    add si, fdd_dbg_u - FDU_SIZE    ; ...and unit 1 is the first row
    mov cl, al
    mov al, 1
    shl al, cl                  ; a BITMAP: this unit was asked
    or [fdd_dbg_ran], al

    mov ax, 0x40                ; there is no readable DOR, so the BIOS's own
    mov es, ax                  ; MOTOR_STATUS is the only place the current
    mov al, [es:0x3F]           ; motor state exists
    and al, 0x0F
    mov cl, 4
    shl al, cl                  ; ...and the DOR keeps them in 4..7
    mov bh, al                  ; BH = the motor bits, for the duration

    mov al, bh
    or  al, 0x0C                ; not-reset (2) + DMA/IRQ (3)...
    or  al, [fdd_unit]          ; ...and DOR bits 0..1 select the unit
    call fdd_dor
    call fdd_st3                ; --- step 1: no motor, and usually enough ---
    jc .nordy
    mov [si+FDU_ST3], al       ; fdd_st3 ANSWERS in AL and banks nothing - it
                                ; is called twice and the two reads are two
                                ; different fields
    test al, 0x10               ; TRACK 0
%ifdef FDD_NO_FAST
    jmp .recal                  ; `make FDDSLOW=1` (SPEC.md 18.97.5): take the
                                ; SLOW path whatever ST3 says. Neither emulator
                                ; can reach it naturally - MartyPC synthesizes
                                ; TRK0 set and QEMU derives it from a track
                                ; that is 0 for an absent drive (18.97.2) - so
                                ; without this the reorder that moved the motor
                                ; wait could only ever be tested in the field
%else
    jz .recal
%endif
    mov byte [si+FDU_STEP], FDD_S_TRK0
    jmp .keep                   ; near: .recal below is longer than a short
                                ; displacement reaches

.recal:                         ; --- step 2: make it step, then ask the LINE
    call fdd_sidrain            ; whatever the controller still has queued is
                                ; not ours - see fdd_sidrain, and SPEC.md
                                ; 18.97.1 for the field run that found it
    mov al, 0x07                ; RECALIBRATE
    call fdd_put
    jc .nordy
    mov al, [fdd_unit]          ; ...this unit
    call fdd_put
    jc .nordy
    mov cx, FDD_SEEKW           ; 77 steps at the DPT's rate is about half a
    call fdd_wait               ; second. A FIXED bound and not a poll: MSR's
                                ; drive-busy bit is a promise about a seek the
                                ; controller accepted, and the case this exists
                                ; for is the one where there is no drive to
                                ; accept it
    call fdd_st3                ; ...and ask the SAME LINE step 1 asked
    jc .nordy
    mov [si+FDU_ST3B], al
    push ax
    call fdd_res                ; our recalibrate's own interrupt, taken so the
    jc .nores                   ; next BIOS floppy operation does not find it
    mov [si+FDU_ST0], al       ; waiting. Reported, never branched on
.nores:
    call fdd_sidrain
    pop ax
    test al, 0x10               ; TRACK 0: did the head reach it?
    jnz .seekok
                                ; ...ST3 says no, and SPEC.md 18.97.3 says ask
                                ; ST0 before believing it. The Packard Bell's
                                ; present 1.2MB drive and the 5150's absent one
                                ; give the SAME ST3 - 21 both, twice each - and
                                ; ST0 tells them apart outright: 21 there
                                ; against 71 here. A recalibrate that ended
                                ; NORMALLY is the controller saying it found
                                ; track 0, which is positive evidence of a
                                ; drive whatever the status line does
                                ;
                                ; **THE TEST IS FOR ABSENCE, NOT FOR PRESENCE
                                ; (SPEC.md 18.97.4).** It read the other way -
                                ; keep on exactly 20 and retire on everything
                                ; else - so a sense that answered NOTHING
                                ; USEFUL retired a drive, which is the one
                                ; thing this routine's header forbids. That is
                                ; not a corner: measured on two cycle-accurate
                                ; machines with a present, working drive B and
                                ; the branch forced, the drained ST0 reads 04
                                ; and 01. Both fail `cmp al, 0x20`, and only
                                ; TRK0 coming up first kept the drive.
    mov al, [si+FDU_ST0]
    and al, 0xF8                ; IC (7-6), SE (5), EC (4), NR (3)
    cmp al, 0x70                ; IC=01 ABNORMAL + seek end + EQUIPMENT CHECK,
    jne .keepst0                ; drive ready: the 765 saying it stepped 77
                                ; times and never saw TRK0, which is the only
                                ; positive evidence of absence anybody has
                                ; recorded (the field 5150's missing drive B,
                                ; ST0 = 71). FF - a sense that never answered -
                                ; is IC=11 and fails this, so a failure keeps
                                ; the drive the way everything else here does
    mov al, [si+FDU_ST0]
    and al, 3                   ; ...and it is THIS unit's result, not one the
    cmp al, [fdd_unit]          ; queue was still holding (18.97.1's whole
    jne .keepst0                ; lesson - the drain makes it ours, this proves
    jmp short .lastchance       ; ...and NOT straight to .gone: the motor has
                                ; not had its say yet, and this is the one
                                ; path that reaches it (SPEC.md 18.97.5)
.keepst0:
    mov byte [si+FDU_STEP], FDD_S_SEEKST0
    jmp short .keep
.lastchance:                    ; --- step 3: spin it up before REMOVING it ---
    mov al, bh                  ; NOT to read ST3 again - the recalibrate below
    or  al, 0x0C                ; searches for the same TRK0 and answers the
    or  al, [fdd_unit]          ; same question, so a second read would only
    push bx
    mov bl, [fdd_unit]
    xor bh, bh
    or  al, [cs:fdd_mbit+bx]    ; ...this unit's motor, not unit 1's. Read
                                ; through CS: this table is .ovl DATA, and the
                                ; overlay cannot reach its own data through DS
                                ; (SPEC.md 2.5) - DS is still KERNEL_SEG here
    pop bx
    call fdd_dor
    mov cx, FDD_MOTORW          ; ...and NOW pay it, on the one path that can
    call fdd_wait               ; remove a drive - which is what 18.97 wanted
                                ; the wait for: the removal decision running
                                ; under the most favourable conditions the
                                ; probe can arrange, against the (undocumented,
                                ; and on every datasheet untrue) possibility of
                                ; a drive that gates TRK0 on its spindle.
                                ;
                                ; RE-READ rather than re-STEP. The recalibrate
                                ; above already ran; the head is wherever it
                                ; left it, and the hypothetical this defends
                                ; against is about REPORTING TRK0 and not about
                                ; reaching it. A second recalibrate would cost
                                ; another FDD_SEEKW and answer the same
                                ; question, which is what would have made this
                                ; a regression for the one machine the probe
                                ; exists for (18.97.5).
    call fdd_st3
    jc .nordy                   ; 18.97's rule: every failure keeps the drive
    mov [si+FDU_ST3B], al       ; the read the VERDICT is taken on, which is
                                ; what 57.5's `ST3 after seek` should carry
    test al, 0x10
    jnz .seekok
                                ; ...and only now may it fall into .gone
.gone:
    mov byte [si+FDU_STEP], FDD_S_NOTRK0
    mov byte [si+FDU_VRD], 0
    xor al, al                  ; THE one path that removes a drive
    jmp short .out
.seekok:
    mov byte [si+FDU_STEP], FDD_S_SEEKOK
    jmp short .keep
.nordy:
    mov byte [si+FDU_STEP], FDD_S_NORDY
.keep:
    mov byte [si+FDU_VRD], 1
    mov al, 1
.out:
    push ax
    mov al, bh                  ; the DOR back: the motor bits it was found
    or  al, 0x0C                ; with, unit 0 selected, ours off
    call fdd_dor
    and byte [es:0x3E], 0x7D    ; re-seek unit 1 (bit 1); forget the interrupt
    pop ax                      ; our recalibrate raised (bit 7)

    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dsk_fdd_retire - unit 1 is not there: take the whole ROW out (SPEC.md 18.97)
; in:  nothing
; out: nothing (all registers preserved)
;
; DV_KIND and not merely DV_FLAGS, which is the difference between this and
; what desk_init has always done to a floppy the equipment word did not claim:
; the flag governs the DESKTOP alone, and fdlg_nextvol's Drive button walks
; DV_KIND - so a zone-only retirement leaves the Standard File dialog still
; cycling the user onto a drive that is not there.
;
; This is dsk_vol_del's "the mounted volume just went away" ending (SPEC.md
; 18.7) reached from the one place a BIOS row may be dropped. dsk_vol_del
; itself still refuses one and must: a DRIVER may not unmount a floppy. Only
; the flag, the kind, the label and the two words a mount would fill are
; touched - DV_UNIT stays, because a free row's unit is not read.
;
; [disk_drive] IS 1 here, so the fallback is the live case rather than a
; hypothetical: it is initialised to B: and drv_boot's drv_mounted, which is
; what moves it to the boot volume, has not run yet.
; -----------------------------------------------------------------------------
dsk_fdd_retire:
    push bx
    mov bx, dsk_vtab + DV_SIZE  ; row 1 - a compile-time constant, so this
                                ; needs no ovw_dsk_vol_slot shim out here
    mov byte [bx+DV_KIND], DVK_FREE
    mov byte [bx+DV_FLAGS], 0
    mov byte [bx+DV_LBL], 0     ; DV_SECS is left alone: it still reads 0 from
                                ; the initialiser here, and a row dsk_vol_add
                                ; later takes is filled by it
    cmp byte [disk_drive], 1
    jne .out
    mov byte [disk_drive], 0    ; nothing may be left naming a free row
.out:
    pop bx
    ret

; fdd_dor - AL to the digital output register, then let the cable settle.
;           Preserves everything but the flags.
fdd_dor:
    push cx
    push dx
    mov dx, FDC_DOR
    out dx, al
    mov cx, FDD_SELW
.w: dec cx
    jnz .w
    pop dx
    pop cx
    ret

; fdd_st3 - SENSE DRIVE STATUS on unit 1 -> AL. CF = 1: no answer.
;
; A LEVEL READ of the selected drive's own status lines, which is the whole
; reason the decision hangs off it rather than off SENSE INTERRUPT STATUS: it
; has no queue, no ordering and no unit ambiguity - the unit is in the command
; byte and the answer is whatever that drive is putting on the bus right now.
fdd_st3:
    mov al, 0x04
    call fdd_put
    jc .bad
    mov al, [fdd_unit]          ; this unit, head 0
    call fdd_put
    jc .bad
    jmp short fdd_get           ; ...whose CF and AL are ours
.bad:
    stc
    ret

; fdd_res - ONE SENSE INTERRUPT STATUS result. AL = ST0, CF = 1 = no answer.
;
; The second result byte (PCN) is taken only if the controller offers one: a
; sense with nothing pending answers with ST0 alone, so a blind second read
; sits out a whole handshake timeout for a byte that is not coming.
fdd_res:
    push cx
    push dx
    mov al, 0x08
    call fdd_put
    jc .bad
    call fdd_get
    jc .bad
    push ax
    mov cx, FDD_SELW
.w: dec cx
    jnz .w                      ; let the MSR settle before asking again
    mov dx, FDC_MSR
    in al, dx
    and al, 0xC0
    cmp al, 0xC0                ; is a second byte on offer?
    jne .one
    mov dx, FDC_FIFO
    in al, dx                   ; PCN, discarded
.one:
    pop ax
    pop dx
    pop cx
    clc
    ret
.bad:
    pop dx
    pop cx
    stc
    ret

; fdd_sidrain - take every queued SENSE INTERRUPT STATUS result and drop it.
;
; SPEC.md 18.97.1: a controller reset leaves one status-change result PER
; DRIVE and the 765 hands them out in order, so a single sense answers the
; question the QUEUE is on rather than the one you asked. Measured on the
; field 5150: a sense issued after a recalibrate of unit 1 returned ST0 = C2 -
; interrupt code 11, and unit TWO. Nothing branches on ST0 any more, but the
; queue is still drained: at both ends, so the one this probe publishes is its
; own, and so the next BIOS floppy operation does not inherit ours.
fdd_sidrain:
    push ax
    push cx
    mov cx, FDD_SIN
    call fdd_res
    jc .out
.next:
    and al, 0xC0
    cmp al, 0x80                ; interrupt code 10 = invalid, i.e. the
    je .out                     ; controller has nothing left to hand over
    dec cx
    jz .out
    call fdd_res
    jnc .next
.out:
    pop cx
    pop ax
    ret

; fdd_put - hand one byte to the FDC, MSR-handshaked and BOUNDED: the one way
;           to hang here is to wait forever for a bit that never changes on a
;           machine where every read is 0FFh (SPEC.md 37.90's rule).
; in:  AL = the byte.  out: CF = 1 = it was never asked for.
fdd_put:
    push ax
    push cx
    push dx
    mov ah, al
    mov dx, FDC_MSR
    mov cx, FDD_HANDTO
.wait:
    in al, dx
    and al, 0xC0                ; RQM (7) and DIO (6)
    cmp al, 0x80                ; ready, and expecting a byte FROM us
    je .go
    dec cx
    jnz .wait
    pop dx
    pop cx
    pop ax
    stc
    ret
.go:
    mov dx, FDC_FIFO
    mov al, ah
    out dx, al
    pop dx
    pop cx
    pop ax
    clc
    ret

; fdd_get - take one result byte.  out: AL = it, CF = 1 = none came.
fdd_get:
    push cx
    push dx
    mov dx, FDC_MSR
    mov cx, FDD_HANDTO
.wait:
    in al, dx
    and al, 0xC0
    cmp al, 0xC0                ; ready, and the byte is coming TO us
    je .go
    dec cx
    jnz .wait
    pop dx
    pop cx
    stc
    ret
.go:
    mov dx, FDC_FIFO
    in al, dx
    pop dx
    pop cx
    clc
    ret

; fdd_wait - CX ticks, and nothing else. The scheduler is live by now but
;            task 0 is the only task there is (inst_init: a clean boot has no
;            instances), so there is nobody to yield to.
fdd_wait:
    push ax
    push cx
    add cx, [ticks]
.spin:
    mov ax, [ticks]
    sub ax, cx
    js .spin
    pop cx
    pop ax
    ret

%endif                           ; NO_FDDPROBE
%endif                              ; KD_BUILD
section .text

; -----------------------------------------------------------------------------
; disk_dir, disk_icons and dsk_secbuf were HERE, and are in kernel/dskwin.inc
; now. Nothing about them changed except where in `.lowbss` they land: they are
; the rung's first bytes rather than its last, so that they and the FAT window
; under them are one contiguous 8,192-byte region that is dead until the first
; mount (SPEC.md 2.1.2). Reached exactly as before, through SS.

section .bss
; --- the per-volume banked BPB (SPEC.md 18.9.2) ------------------------------
; A FIXED disk cannot be swapped, so a boot sector that validated once is still
; the same boot sector on every later switch to that volume - and re-reading it
; is a seek to cylinder 0, a revolution, and a seek back, per switch. dsk_bpbv
; keeps the 64-byte staged head so dsk_bpb_check can re-run against it with no
; I/O at all; dsk_bpbsg keeps the signature dsk_fatw_pick needs to decide window
; reuse, which is otherwise computed off the sector we would have read.
;
; **FLOPPIES BANK HERE TOO, and this comment used to say they did not.** It was
; written before SPEC.md 18.9.3 landed and left in the future tense - "the batch
; bracket is what WILL let one skip this" - so it read as a statement that rows
; 0 and 1 never touch this array. They are its HEAVIEST users: inside a batch a
; floppy banks with [dsk_bpbok] = 2, and that is the whole of 18.9.3's measured
; win (an install's BPB reads 41 -> 2, the floppy side 356 -> 163 sectors).
;
; So this is DVOL_MAX * 64 and cannot be indexed from volume 2 to save the two
; rows 18.98 added. That was proposed, on the strength of the stale comment
; above, and it would have taken the bracket away from A: and B: - the two
; volumes it was written for. The split 18.9.2 makes is between PERMANENT
; (fixed, 1) and BATCH-SCOPED (floppy, 2), not between volumes that use this
; and volumes that do not.
dsk_bpbv:     resb DVOL_MAX * DSK_BPB_SZ
dsk_bpbsg:    resw DVOL_MAX
dsk_bpbok:    resb DVOL_MAX     ; 0 = nothing banked, 1 = banked and good
                                ; for ever (a fixed disk), 2 = good only until
                                ; this batch ends (SPEC.md 18.9.3)
dsk_bpbpre:   resb 1            ; dsk_bpb already holds a banked head

dsk_ent:    resb DSK_DE_SIZE    ; one staged directory entry (dsk_get_dir);
                                ; also the mount's synthesis/harvest scratch.
                                ; DSK_DE_SIZE and NOT DSK_DE_STRIDE, and the
                                ; difference is not slack: osapi_fs_ent copies
                                ; a driver's whole 32-byte record in here, and
                                ; the harvest stages a package's 32-byte
                                ; HEADER over it (a bare `mov cx, 32`, which
                                ; no grep for the constant finds). The LISTING
                                ; is what got narrower; this did not
dsk_ico:    resb DSK_ICO_SIZE   ; one staged icon slot (dsk_get_icon)
%ifdef DSK_WANT_ICONS           ; ICO_KEY is gated with the store (SPEC.md 25.9)
ico_keyb:   resb ICO_KEY        ; THIS ENTRY'S (name, size), taken at the top
                                ; of the harvest loop and used by every path
                                ; below it (SPEC.md 25.9). It is taken THERE
                                ; because .h_read stages the package's own
                                ; HEADER over dsk_ent, so by the time a body
                                ; exists the name is gone - the bug that
                                ; costs is reading dsk_ent after that and
                                ; getting four characters of a header
%endif                          ; DSK_WANT_ICONS

; mount-derived FAT state (SPEC.md 18.1) - valid only after a successful
; mount; every consumer is already gated by disk_nfiles != 0
dsk_bpbh:     resb DSK_BPB_SZ   ; staged BPB fields 11..28 (mount scratch)
dsk_bpb       equ dsk_bpbh - DSK_B_BPS  ; ...addressed as sector offsets, so
                                ; [dsk_bpb+DSK_B_*] is the field for every
                                ; DSK_B_* from DSK_B_BPS up. DSK_B_JMP is
                                ; asked of dsk_secbuf and never through this
dsk_fattype:  resb 1            ; 0 = FAT12, 1 = FAT16 (SPEC.md 19)
dsk_spc:      resb 1            ; BPB_SecPerClus (validated power of two)
dsk_fatlba:   resw 1            ; = RsvdSecCnt (FAT1 start LBA)
dsk_fatsz:    resw 1            ; = FATSz16 - capped at DSK_FAT_SECS only on
                                ; a BIOS floppy (SPEC.md 18.2 rule 10)
dsk_fatwn:    resw 1            ; ...and how many are resident. On a floppy
                                ; this is the whole FAT and the window never
                                ; moves - the degenerate case, byte for byte
dsk_vcls:     resb 1            ; the class osapi_vol_fence matched, for
                                ; dsk_vol_add to stamp into DV_CLASS
dsk_vkind:    resb 1            ; the mounted volume's DVK_* (SPEC.md 18.7):
                                ; its TRANSPORT, which is what dsk_xfer needs
dsk_vflop:    resb 1            ; ...and whether it is a FLOPPY, which is what
                                ; BPB rules 10..13 need and is no longer the
                                ; same question (SPEC.md 52.10.3): a boot
                                ; partition is a DVK_BIOS row with a 64-sector
                                ; FAT, and rule 10 caps a floppy's at 9
dsk_vsecs:    resw 1            ; ...and the sector count its driver declared,
                                ; which is rule 13's replacement
dsk_fslist:   resb 1            ; --- a REDIRECTED volume's mount (SPEC.md
                                ; 62.9.1) --- 1 = we are inside FSV_LIST, and
                                ; that is the whole gate on OSAPI_FS_ENT: an
                                ; append outside a mount would leave
                                ; disk_nfiles describing a listing the sort
                                ; never saw
dsk_fsn:      resw 1            ; how many entries have been appended, parent
                                ; link included. THE KERNEL COUNTS, because
                                ; the count is disk_nfiles' and a driver has
                                ; no business holding it
dsk_fsup:     resw 1            ; the current folder's PARENT handle, answered
                                ; by FSV_CHDIR and spent by dsk_synth_up.
                                ; Opaque: only the driver knows what it means,
                                ; and there is no '..' on a disk to read it
                                ; from
dsk_nfats:    resb 1            ; 1 or 2
dsk_totsec:   resw 1            ; TotSec16, the volume's extent in sectors
dsk_rootlba:  resw 1            ; first root-dir LBA
dsk_rootsecs: resw 1            ; root-dir sector count (<= 32)
dsk_datalba:  resw 1            ; FirstDataSec
dsk_maxclus:  resw 1            ; CountOfClusters+1 = highest valid cluster
dsk_chain_end: resw 1           ; dsk_read_chain: the last cluster it touched,
                                ; so a chunked read can resume (SPEC.md 18)
dsk_cherr:    resb 1            ; dsk_read_chain: failure code across the
                                ; register-restore epilogue
%ifndef KERN_SMALL
dsk_chwr:     resb 1            ; ...and which way it is moving (SPEC.md
                                ; 18.4.7.1). Zero is a READ, which is what
                                ; .bss already is and what every existing
                                ; caller means
%endif

; --- the directory walker's scratch (SPEC.md 19.2) --------------------------
; Safe in .bss: dsk_dirw_start writes all three before any walk reads them.
; [dsk_cwd] and [dsk_keepcwd] are NOT - see the module data block up top.
dsk_wclus:    resw 1            ; dirw: the cluster being walked, 0 = root
dsk_wsec:     resw 1            ; dirw: sector index inside it
dsk_wguard:   resw 1            ; dirw: sectors left before the walk gives up

; --- the read-ahead cache's bookkeeping (SPEC.md 18.95) ---------------------
; Safe in .bss because nothing reads any of it while [dsk_rah_seg] is 0, and
; the claim that sets that word non-zero (dsk_rah_want) flushes the table and
; zeroes dsk_rah_next before anything can look. [dsk_rah_seg] itself is NOT -
; it is in .text with a real initialiser, because a shed reads it and
; mem_pg_own names it.
dsk_rah_tab:  resb DSK_RAH_RUNS * RAH_REC   ; one record per cached chunk
dsk_rah_next: resw 1            ; the next slot to spend (round robin), as a
                                ; SLOT INDEX - so the fill's wrap is a compare
                                ; against [dsk_rah_runs] itself and no second
                                ; copy of the width exists to go stale. It was
                                ; a byte offset, with `runs * 9` recomputed at
                                ; every fill to bound it. THREE more words
                                ; stood here - the slot being filled, and the
                                ; two words the scan-and-copy halves used to
                                ; pass an answer through before they became
                                ; dsk_rah_serve - and all three went with the
                                ; routines that passed them (18.95.1)
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
dsk_fdraw:    resb 1            ; dsk_find: report the ON-DISK size rather
                                ; than the unpacked one (SPEC.md 20.14.3) -
                                ; api_file_find_raw's whole difference, and a
                                ; byte rather than a register because both
                                ; cells reach the body through one fence
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
                                ; (a dsk_wpad was here to keep the words below
                                ; even-aligned. The target is an 8088 and its
                                ; bus is EIGHT BITS WIDE, so a word costs two
                                ; cycles at either parity and the byte bought
                                ; nothing; dsk_mntok above is `mov byte` /
                                ; `cmp byte` at all six sites, so nothing
                                ; wider was writing through it either)
dsk_rover:    resw 1            ; next cluster the allocator examines
dsk_fatd1:    resw 1            ; the dirty range's high end; the low end is
                                ; dsk_fatd0 in .text (it must read CLEAN before
                                ; the first mount, and .bss reads as 0)

section .text

section .cold                   ; ...the far entries themselves live HERE, or
                                ; their `call <body>` would cross too
; -----------------------------------------------------------------------------
; Far entries for the ON-DEMAND MODULES (SPEC.md 2.8)
;
; The floppy formatter is a module now (SPEC.md 18.96), and sectors are what
; it is FOR: its whole outbound surface is these three plus a duplicated error
; mapper. A module runs from a heap claim with a CS of its own, so a near call
; from `.modf` into this segment is the bug tools/os88ovlchk.py refuses - the
; same one SPEC.md 2.6 refuses between `.text` and `.cold`.
; -----------------------------------------------------------------------------
dskf_disk_read:          call disk_read_x
                    retf
dskf_disk_write:         call disk_write_x
                    retf
dskf_dsk_vol_row:        call dsk_vol_row_x
                    retf

section .text
