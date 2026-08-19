; =============================================================================
; os8088 - RAMDISK.DRV / rdabi.inc
;
; What RAMDISK.DRV and RAMPAGE.DRV both have to agree about (SPEC.md 62.9.9).
;
; The RAM disk is two images. RAMDISK.DRV is resident from the moment the
; Drivers page is ticked and holds the store, the FSV_* verbs, the settings
; and the mount; RAMPAGE.DRV is read off the system volume the first time
; somebody selects `Ram Disk` in the Control Panel and holds that page's
; layout, its strings, its paint, its click and its file-dialog completion.
; Neither is a driver of the other's class - the page is not in drv_tab at all
; and the kernel never sees it - so everything that crosses between them
; crosses through this file.
;
; It is hddabi.inc's shape and hddabi.inc's rules, which is deliberate: the
; two splits are the same mechanism and a reader who knows one knows the
; other. Three kinds of thing are in here and the distinction matters:
;
;   CONSTANTS   are simply duplicated. An `equ` costs no bytes in either
;               image, so the store's shape and the status block's stride
;               live here and both sides assemble their own copy.
;   VERBS       are the two dispatch vocabularies - RDT_* is what the resident
;               may ask the page to do, RSV_* what the page may ask of the
;               resident. Each is one far call.
;   THE STATUS BLOCK is how RSV_STATE carries more than the registers hold.
;
; **A NUMBER IN HERE IS AN ABI AND MUST NOT BE RE-USED.** The two images are
; separate files on the disk and nothing makes a machine's RAMPAGE.DRV the
; same vintage as its RAMDISK.DRV - an old page beside a new resident is what
; a half-copied floppy leaves. RD_ABI_VER is checked at load and a mismatch
; refuses, so add verbs at the end and bump the version; never renumber one.
; =============================================================================

RD_ABI_VER   equ 2              ; bumped whenever a verb changes meaning

; --- the store (SPEC.md 62.9.10) ---------------------------------------------
; EXTENTS CHAIN THROUGH A TABLE THAT IS ITSELF A HEAP CLAIM, which is what a
; disk does and what the first version of this did not: a byte-per-extent
; array in the driver's own image capped the whole store at 224KB, and a
; 16-bit offset capped one FILE at 63.5KB. On a machine with memory above 1MB
; both of those throw away the reason to put a RAM disk up there.
;
; So the chain is a WORD per extent in a claim beside the store, and the
; EXTENT SIZE IS DERIVED FROM THE STORE'S: the smallest power of two that
; keeps the table inside RD_MAXEXT entries. That is SPEC.md 52.3's capacity
; table in miniature - a FAT volume picks its cluster size from how big it is,
; for the same reason and with the same consequence, that two volumes of
; different sizes have different granules.
;
;     store          extent   entries   table
;     8KB .. 2MB       1KB    8..2048   16B .. 4KB
;     2MB .. 4MB       2KB      2048    4KB
;     4MB .. 8MB       4KB      2048    4KB
;     8MB .. 16MB      8KB      2048    4KB
;
; What is NOT derived is the ceiling: RD_MAXKB is the arithmetic limit, and
; what a machine will actually give is asked of it (SPEC.md 62.9.10.1).
RD_MAXEXT    equ 2048            ; chain entries, so the table is at most 4KB
RD_TABMAXKB  equ RD_MAXEXT * 2 / 1024   ; ...which is that, in KB. Named
                                 ; because it is what CONVENTIONAL memory has
                                 ; to fund for an EXTENDED store, and naming it
                                 ; is half of not confusing that gate with a
                                 ; ceiling (SPEC.md 62.9.10.3)
RD_EXTMINKB  equ 1               ; ...and the smallest extent
RD_EXTMAXKB  equ 8               ; ...and the largest, which is what makes
                                 ; RD_MAXKB the product of the two above
RD_MINKB     equ 8
RD_MAXKB     equ RD_MAXEXT * RD_EXTMAXKB
RD_DEFKB     equ 64              ; what a machine with no saved settings gets
RD_STEPKB    equ 8               ; the smallest step of - and +, and the
                                 ; granule every size is rounded to. It is
                                 ; what keeps a preserve's LAST chunk on a
                                 ; 4,096-byte boundary (SPEC.md 62.9.12)
RD_MAXENT    equ 96              ; directory rows over the WHOLE volume. The
                                 ; kernel's own per-folder listing cap is 32
                                 ; (SPEC.md 19), so a single folder still
                                 ; answers to that first
RD_NOEXT     equ 0xFFFF          ; RDE_EXT: no content (a folder, or empty)
RD_CFREE     equ 0               ; a chain cell: this extent is free...
RD_CEND      equ 0xFFFF          ; ...or allocated and the chain ends here.
                                 ; Otherwise it is `next + 1`, so that 0 can
                                 ; mean free and extent 0 is still linkable -
                                 ; the byte table's convention, widened
RD_FREE      equ 0xFF            ; RDE_TYPE: this row is not in use

RDL_CONV     equ 0               ; RDC_LOC / RDS_LOC: the claim heap (SPEC.md 50)
RDL_XMS      equ 1               ; ...or the store above 1MB (SPEC.md 41)

; --- a directory row, in the resident's image --------------------------------
; THE HANDLE IS THE ROW INDEX PLUS ONE, which is what makes rd_row one multiply
; instead of a scan and 0 a natural "the root" (SPEC.md 62.9.1: it is opaque to
; the kernel). RDE_EXT is now the FIRST extent of a chain rather than the only
; one, and nothing above it noticed.
RDE_PAR      equ 0               ; db: the parent's handle, 0 = the root
RDE_TYPE     equ 1               ; db: 0 file / 1 package / 2 folder / RD_FREE
RDE_EXT      equ 2               ; dw: its first extent, or RD_NOEXT. It was a
                                 ; BYTE with a spare beside it, so widening it
                                 ; cost the row nothing and RDE_SZ did not move
RDE_SIZE     equ 4               ; dd: bytes
RDE_NAME     equ 8               ; db[16]: NUL-terminated 8.3 display name
RDE_SZ       equ 24

; --- the settings blob, SPEC.md 51.9.1's key 'RD' ----------------------------
; DRV_RBLOB_SZ bytes carried in SYSTEM.CFG by a kernel that does not know what
; is in them. A blob nobody has written reads back as zeroes, so RDC_VER is
; what tells a saved state from the defaults - SPEC.md 51.5 rule 2's mechanism
; one level down, and the reason this has a version byte of its own.
RDC_VER      equ 0               ; db: 1 = this shape
RDC_KB       equ 1               ; dw: the store's size in KB
RDC_LOC      equ 3               ; db: RDL_*
RDC_FLAGS    equ 4               ; db: bit 0 = restore RDC_NAME at boot
RDC_DRV      equ 5               ; db: the volume that image is on
RDC_CWD      equ 6               ; dw: its folder's first cluster
RDC_NAME     equ 8               ; db[13]: its 8.3 name, NUL
RDC_SZ       equ 21              ; ...of DRV_RBLOB_SZ (24), the rest spare
RDCF_BOOT    equ 1               ; RDC_FLAGS bit 0

RDC_VER_V    equ 1

; --- the .RAM image on disk (SPEC.md 62.9.12) --------------------------------
; THREE BLOCKS: the header and directory, then the chain table, then the arena
; verbatim. It was two, and the chain table moved out of the first when it
; became a WORD per extent and stopped fitting beside the directory.
;
; Every block boundary is a multiple of 4,096, which is a multiple of every
; cluster size this OS makes (512, 1024, 2048, 4096) - which is what
; OSAPI_FILE_APPEND and OSAPI_FILE_READ_AT between them require.
RDI_META     equ 4096            ; the header and the directory
RDI_CTAB     equ 4096            ; ...the chain table, all RD_MAXEXT of it
RDI_ARENA    equ RDI_META + RDI_CTAB     ; ...and where the store itself starts
RDI_CHUNK    equ 32768           ; one arena append, conventional
RDI_XCHUNK   equ 4096            ; ...or through the bounce, in extended memory
RDI_MAGIC0   equ 0x384F          ; 'O8'
RDI_MAGIC1   equ 0x4452          ; 'RD'
RDI_VER_V    equ 2               ; VERSION 2: the chain is a word and it has a
                                 ; block of its own. A version 1 image is
                                 ; REFUSED rather than misread - its byte
                                 ; chain would decode as extents that do not
                                 ; exist, which is a volume that mounts and
                                 ; then serves nonsense

RDI_MAG      equ 0               ; dw, dw
RDI_VER      equ 4               ; dw
RDI_EXTB     equ 6               ; dw: the extent size it was written with, in
                                 ; BYTES. It is DERIVED from the size now
                                 ; (SPEC.md 62.9.10), so this is read and
                                 ; adopted rather than compared - an image made
                                 ; at 2KB extents restores at 2KB extents
RDI_NENT     equ 8               ; dw: live rows recorded below
RDI_BYTES    equ 10              ; dd: arena bytes that follow the chain block
RDI_KB       equ 14              ; dw: the store size it was preserved from
RDI_DIR      equ 32              ; ...and the records start here
RDI_ESZ      equ 24              ; one record:
RDI_E_IDX    equ 0               ;   db: THE ROW INDEX - see SPEC.md 62.9.12
RDI_E_PAR    equ 1               ;   db: the parent's handle
RDI_E_TYPE   equ 2               ;   db
RDI_E_PAD    equ 3               ;   db
RDI_E_EXT    equ 4               ;   dw: its first extent
RDI_E_SIZE   equ 6               ;   dd
RDI_E_NAME   equ 10              ;   db[14]
%if RDI_DIR + RD_MAXENT * RDI_ESZ > RDI_META
  %error "ramdisk: the directory does not fit RDI_META"
%endif
%if RD_MAXEXT * 2 > RDI_CTAB
  %error "ramdisk: the chain table does not fit RDI_CTAB"
%endif

; =============================================================================
; RDT_* - what the RESIDENT asks the PAGE
;
; One far call to the page's dispatcher at PKG_DISP with **BP = the page's
; ENTRY OFFSET, out of its own header at +6, and the verb in AL** - drv_call's
; shape exactly (SPEC.md 51.2), because the dispatcher is `call bp / retf`: BP
; is an ADDRESS the dispatcher jumps to, never a number it passes on.
; =============================================================================
RDT_INIT     equ 0              ; in: DX = the resident's segment, BX =
                                ; rd_svc's offset in it. DX and not AX,
                                ; because AL is the verb
RDT_PAINT    equ 1              ; in: DI = pane left, DX = pane top. The gfx
                                ; lock is held and the pane is white-filled
RDT_CLICK    equ 2              ; in: DI = pane left, DX = pane top, CX = the
                                ; click's pane-relative x, BX = its y
RDT_DLGDONE  equ 3              ; in: AH = the mode (AL being the verb), ES:DI
                                ; = the chosen name in KERNEL_SEG, DX:CX = its
                                ; size (SPEC.md 51.10)
RDT_KEY      equ 5              ; in: BL = ASCII, BH = the scan code (AL being
                                ; the verb). out CF = 0 = the page took it.
                                ; The gfx lock is HELD, exactly as for a click,
                                ; and the page repaints whatever it changed
RDT_BUSY     equ 4              ; out: CF = 1 = do not unload me. Asked at the
                                ; panel's close and never volunteered - a "you
                                ; may free me now" call would be answered by
                                ; freeing the image it has to return into
RDT_UP       equ 6              ; in: the same four registers RDT_CLICK takes,
                                ; at the RELEASE (SPEC.md 13.8.4). This is
                                ; where the page ACTS; RDT_CLICK only arms
RDT_DRAG     equ 7              ; ...and the same again while the button is
                                ; HELD, so the pressed control can follow the
                                ; pointer off itself and back (SPEC.md 13.8.1)

; ADDING A VERB IS AN ABI CHANGE even though rp_entry refuses an unknown one:
; a resident that arms a press and then finds no RDT_UP over there has a page
; whose buttons never act at all, which is worse than a refusal to load. Hence
; RD_ABI_VER 1 -> 2 above; the two images are built and shipped together, so
; the only pair this can separate is a half-copied floppy - which is exactly
; what the version check is for.

; =============================================================================
; RSV_* - what the PAGE asks the RESIDENT
;
; in:  AL = the verb, DS = the resident's (the page sets it before the far
;      call, so this is ordinary near code over there), other registers per
;      the verb
; out: CF, and AL where the verb says so
; =============================================================================
RSV_STATE    equ 0              ; SI = an RDS_SZ block in the PAGE's segment;
                                ; filled from the live state. Asked on every
                                ; paint, because the panel stays clickable and
                                ; a mount can happen underneath
RSV_SIZE     equ 1              ; BX = KB. Clamped here, not there - and BX
                                ; and not AX, because AL is the VERB on every
                                ; call in this table and an argument that
                                ; needs a whole word cannot share it
RSV_LOC      equ 2              ; BL = RDL_*
RSV_MOUNT    equ 3              ; out CF = 1 and AL = RDERR_*
RSV_UNMOUNT  equ 4              ; cannot fail
RSV_PRESERVE equ 5              ; SI = a NUL name in the PAGE's segment, or 0
                                ; = the remembered one. out CF = 1 and AL =
                                ; RDERR_*
RSV_LOAD     equ 6              ; SI = a NUL name in the PAGE's segment
RSV_CLEAR    equ 7              ; forget the boot image (not the file)
RSV_DLG      equ 8              ; BL = FDLG_OPEN / FDLG_SAVE, SI = a default
                                ; name in the PAGE's segment or 0. The page
                                ; cannot ask the kernel itself: OSAPI_DRV_DLG
                                ; resolves a drv_tab row from the CALLING
                                ; SEGMENT and an overlay has none (SPEC.md
                                ; 51.10)

; --- RSV_STATE's block -------------------------------------------------------
RDS_KB       equ 0              ; dw: the configured size
RDS_LOC      equ 2              ; db: RDL_*
RDS_MNT      equ 3              ; db: 1 = the volume is up
RDS_VOL      equ 4              ; db: ...and its kernel volume index
RDS_XMS      equ 5              ; db: 1 = this machine has extended memory to
                                ; offer, which is what greys the Store radios
                                ; on a FACT rather than a guess (SPEC.md 47)
RDS_FREEK    equ 6              ; dw: free KB, 0 with nothing mounted
RDS_NFILE    equ 8              ; dw: live rows
RDS_BOOT     equ 10             ; db: 1 = a boot image is remembered
RDS_ERR      equ 11             ; db: the last RDERR_*, READ-AND-CLEAR - the
                                ; page is the only reader and a verdict that
                                ; outlived its cause would be reported again
                                ; on the next unrelated paint
RDS_NAME     equ 12             ; db[13]: the remembered image's name
RDS_SZ       equ 26

; --- what went wrong (RDS_ERR, and RSV_MOUNT's AL) ---------------------------
RDERR_NONE   equ 0
RDERR_MEM    equ 1              ; the store could not be claimed
RDERR_VOL    equ 2              ; the kernel's volume table is full
RDERR_MNT    equ 3              ; ...or it refused the mount
RDERR_FILE   equ 4              ; the image could not be read or written
RDERR_FMT    equ 5              ; that file is not a RAM disk image
RDERR_BIG    equ 6              ; ...or it came off a store bigger than this
RDERR_NOIMG  equ 7              ; Preserve with nothing remembered to preserve to
RDERR_NERR   equ 8
