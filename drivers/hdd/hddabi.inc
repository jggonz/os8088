; =============================================================================
; os8088 - HDD.DRV / hddabi.inc
;
; What HDD.DRV and HDDTOOL.DRV both have to agree about (SPEC.md 52.11).
;
; The hard-disk driver is two images. HDD.DRV is resident from the moment the
; Drivers page is ticked and holds the probe, the transport, the volume table
; and the Control Panel page; HDDTOOL.DRV is read off the system volume when
; the user clicks Format or Install, and holds the partitioner, the formatter
; and the installer. Neither is a driver of the other's class - the tool is
; not in drv_tab at all and the kernel never sees it - so everything that
; crosses between them crosses through this file.
;
; Three kinds of thing are in here and the distinction matters:
;
;   CONSTANTS   are simply duplicated. An `equ` costs no bytes in either
;               image, so the on-disk shapes and table strides live here and
;               both sides assemble their own copy. Nothing to keep in step.
;   VERBS       are the two dispatch vocabularies - HDT_* is what the resident
;               may ask the tool to do, HSV_* what the tool may ask of the
;               resident. Each is one far call.
;   THE REQUEST BLOCK is how a verb carries more than the registers hold.
;
; **A NUMBER IN HERE IS AN ABI AND MUST NOT BE RE-USED.** The two images are
; separate files on the disk and there is nothing that makes a machine's
; HDDTOOL.DRV the same vintage as its HDD.DRV - an old tool beside a new
; resident is exactly what a half-copied floppy leaves. HD_ABI_VER is checked
; at load and a mismatch refuses, so add verbs at the end and bump the
; version; never renumber one. This is SPEC.md 20.8 rule 4's reasoning applied
; to a private interface, for the same reason and not by analogy.
; =============================================================================

HD_ABI_VER   equ 2              ; bumped whenever a verb changes meaning
HD_TOOLFILE  equ 0              ; (the name itself is in the resident: a
                                ; string is not an ABI number)

HD_MAXDEV    equ 4              ; devices the probe will report
HD_MAXVOL    equ 4              ; volumes mountable at once: ONE PER FAT
                                ; PARTITION, because four primaries is the
                                ; whole of what a partition table holds and
                                ; each is its own drive letter and its own
                                ; desktop icon (SPEC.md 52.4). It matches the
                                ; kernel's own spare rows (DVOL_MAX - 2)

; --- one device row, stride HDD_SIZE ----------------------------------------
HDD_KIND     equ 0              ; db: HDK_*
HDD_UNIT     equ 1              ; db: int 13h drive (80h/81h), or 0/1 on the
                                ; IDE channel in HDD_BASE
HDD_FLAGS    equ 2              ; db: bit 0 = geometry known
HDD_PAD      equ 3
HDD_CYL      equ 4              ; dw: cylinders
HDD_HEADS    equ 6              ; dw: heads      } 1..255
HDD_SPT      equ 8              ; dw: sectors    } 1..63
HDD_BASE     equ 10             ; dw: the IDE task file's base port
HDD_SIZE     equ 12

HDK_NONE     equ 0
HDK_BIOS     equ 1              ; int 13h (an ST-11M's ROM, or an AT BIOS)
HDK_IDE      equ 2              ; the task file, directly

; What [hd_mbrok] says about hd_mbr. HMB_BAD and HMB_NONE are NOT the same
; answer: one means the disk has no partitions, the other means we do not know
; what it has, and only the first makes writing a blank table safe.
HMB_BAD      equ 0              ; the read failed - hd_mbr is a fabrication
HMB_OK       equ 1              ; it came off the disk with a signature
HMB_NONE     equ 2              ; it read fine and is unpartitioned

; --- one mounted volume, stride HDV_SIZE ------------------------------------
HDV_USED     equ 0              ; db: 1 = live
HDV_DEV      equ 1              ; db: which device row
HDV_VOL      equ 2              ; db: the kernel's volume index
HDV_PART     equ 3              ; db: which partition slot (0..3)
HDV_BASE     equ 4              ; dd: its first LBA on the device
HDV_SECS     equ 8              ; dw: its length, <= 65,535 by construction
HDV_LSEG     equ 10             ; dw: the listing claim, 0 = none
HDV_VOLH     equ 12             ; db: OUR handle for it - the row's own index,
                                ; which is what comes back in DSV_BLK's AH
HDV_SIZE     equ 16

; --- the partition table's on-disk shape, at LBA 0 --------------------------
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
HD_MBRCODE   equ 446            ; the space in front of the table

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

; =============================================================================
; HDT_* - what the RESIDENT asks the TOOL to do
;
; One far call to the tool's dispatcher at PKG_DISP with **BP = the tool's
; ENTRY OFFSET, out of its own header at +6, and the verb in AL** - which is
; drv_call's shape exactly (SPEC.md 51.2), and it is drv_call's shape because
; the dispatcher is `call bp / retf`: BP is an ADDRESS the dispatcher jumps
; to, never a number it passes on. Getting that wrong calls offset 1 of the
; image, runs through the header into its own `retf` byte, and returns having
; done nothing - no crash, no window, and nothing on screen to say so.
; =============================================================================
HDT_INIT     equ 0              ; in: DX = the resident's segment, BX = hd_svc's
                                ; offset in it. DX and not AX, because AL is
                                ; the verb. Called once
                                ; immediately after the image is read, before
                                ; anything else. out: CF = 1 = refuse (an ABI
                                ; mismatch), and the resident frees the image
HDT_FORMAT   equ 1              ; open the disk tool  (SPEC.md 52.2)
HDT_INSTALL  equ 2              ; open the installer  (SPEC.md 52.10.4)
HDT_SHUT     equ 3              ; close every window, free every claim. Must
                                ; leave NOTHING of the tool on screen: the
                                ; resident frees this image the moment it
                                ; returns, and a window whose W_SEG names
                                ; freed memory is a paint into whatever
                                ; claims it next
HDT_BUSY     equ 4              ; out: CF = 1 = a window of mine is still
                                ; VISIBLE, so do not unload me. Visible and not
                                ; merely allocated: closing one of these is a
                                ; hide (SPEC.md 38.1), so a non-zero handle
                                ; outlives the window the user shut and would
                                ; pin this image for the session

; =============================================================================
; HSV_* - what the TOOL asks of the RESIDENT
;
; One far call the other way, AL = the verb and SI = a request block in the
; TOOL's segment (the resident knows that segment: it loaded it). The block
; rather than the registers, because HSV_RAW alone wants an op, a device, a
; 32-bit LBA, a far buffer and a count - and a uniform shape means a verb
; added later needs no new calling convention.
;
; Every verb preserves what a near proc would and answers in CF.
; =============================================================================
HSV_RAW      equ 0              ; a raw transfer to a DEVICE, outside any
                                ; volume. The request block below
HSV_SYNC     equ 1              ; copy the device table, [hd_ndev] and
                                ; [hd_sel] into the tool's own copies, which
                                ; is what lets hd_dev_row and hd_chs stay
                                ; ordinary near code in both images
HSV_MOUNT    equ 2              ; in: HRQ_DEV/HRQ_SLOT -> mount that partition
                                ; out: CF, AL = the kernel volume index
HSV_UNMOUNT  equ 3              ; in: HRQ_DEV/HRQ_SLOT -> drop EVERY volume
                                ; mounted on that partition (it subsumes the
                                ; volume-table walk the tool used to do, so
                                ; hd_vols need not be shared at all)
HSV_CFGMARK  equ 4              ; stage the settings blob (SPEC.md 52.6). No
                                ; disk I/O - the panel's close writes it
HSV_MSG      equ 5              ; in: AH = an HDM_* -> the page's caption.
                                ; A CODE AND NOT A POINTER: font_str reads
                                ; through DS and the page is drawn with DS =
                                ; the RESIDENT, so a string in the tool's
                                ; segment would render that image's own bytes
HSV_PAGE     equ 6              ; the Control Panel owes its page a repaint
HSV_CTRS     equ 7              ; SPEC.md 52.10.9's DEVICE counters into
                                ; HRQ_C0..C2. Implemented only in an INSTBENCH
                                ; build - the number is allocated here
                                ; unconditionally all the same, because an
                                ; `equ` costs no bytes and a verb number that
                                ; existed in one build and not another is
                                ; exactly the ABI drift this file forbids

; --- HDM_*: the captions the tool may ask the page to show -------------------
HDM_PICK     equ 0              ; 'Pick a drive, then a button'

; --- the request block, in the TOOL's segment --------------------------------
HRQ_DEV      equ 0              ; db: a device index (not a row pointer - the
                                ; row is in the resident's table)
HRQ_SLOT     equ 1              ; db: a partition slot, 0..3
HRQ_OP       equ 2              ; db: HSV_RAW: 0 = read, 1 = write
HRQ_RES      equ 3              ; db: what a verb answers, where a byte is
                                ; the whole answer - HSV_MOUNT's volume index,
                                ; HSV_UNMOUNT's did-anything-go
HRQ_LBA      equ 4              ; dd: HSV_RAW: the device-absolute LBA
HRQ_SEG      equ 8              ; dw: HSV_RAW: the buffer, in the TOOL's
HRQ_OFS      equ 10             ; dw: segment
HRQ_CNT      equ 12             ; dw: HSV_RAW: sectors
HRQ_VOL      equ 14             ; db: HSV_UNMOUNT: the LAST kernel volume
                                ; index it dropped, 0xFF = none. HRQ_RES
                                ; already says whether anything went; the
                                ; installer needs to know WHICH, because the
                                ; volume that goes away may be the one the
                                ; user is standing on and both halves of
                                ; SPEC.md 51.5.2's bank name it (52.10.8)
HRQ_PAD      equ 15
HRQ_C0       equ 16             ; dw: HSV_CTRS: device commands issued,
HRQ_C1       equ 18             ; dw: ...sectors they carried,
HRQ_C2       equ 20             ; dw: ...and controller resets
HRQ_SIZE     equ 22
