; =============================================================================
; os8088 - apps/thewire/wcat.inc
;
; THE CATALOG FORMAT, version 1 (SPEC.md 88.2) - offsets and limits only.
;
; **EVERY EQU HERE IS MIRRORED IN tools/os88wire.py** and tests/unit/t_wire.py
; compares the two files in the FAST tier. There is no linker in this tree, so
; a format that lives on both sides of a wire is a number typed out twice; the
; comparison is what stops the two spellings drifting, exactly as
; tests/unit/t_mirror.py does for the constants the kernel and the SDK share.
;
; Nothing in this file emits a byte. It is included by thewire.asm at the top,
; beside os88api.inc, and by nothing else on the machine: SPEC.md 88.1's
; boundary is that the PACKAGE is the only reader of this format here.
; =============================================================================

; --- the 32-byte header ------------------------------------------------------
WC_MAGIC    equ 0               ; 4 bytes, 'WIRE'
WC_VER      equ 4               ; byte, = WIRE_VER
WC_RSZ      equ 5               ; byte, the record size in 16-byte units
WC_N        equ 6               ; word, the record count
WC_HSZ      equ 8               ; word, = WIRE_HDR
WC_SCOFF    equ 10              ; word, the sidecar table, from file start
WC_SCN      equ 12              ; word, its entry count
WC_DATE     equ 14              ; 8 bytes, 'YYYYMMDD'
WIRE_HDR    equ 32

; --- one 256-byte record, at WIRE_HDR + WIRE_REC*i ---------------------------
WC_STEM     equ 0               ; 8, space-padded uppercase
WC_TITLE    equ 8               ; 24, NUL-padded
WC_KIND     equ 32              ; byte, WK_* below
WC_TIER     equ 33              ; byte, 0..3 - the RECOMMENDED machine
WC_FLAGS    equ 34              ; byte, WF_* below
WC_NSIDE    equ 35              ; byte, 0..WIRE_SCMAX
WC_SIDE0    equ 36              ; word, the first sidecar's table index
WC_SIZE     equ 38              ; dword, the .O88's size
WC_TOTAL    equ 42              ; dword, it plus every sidecar - an archive's
                                ; UNPACKED total
WC_NEEDKB   equ 46              ; word, an archive's RAM disk KB (88.13's
                                ; header figure); 0 on every other record
WC_ICON     equ 48              ; 64: 16 mask words then 16 data words
WC_DESC     equ 112             ; WC_DESCN lines of WC_DESCW, pre-wrapped
WIRE_REC    equ 256
WC_DESCN    equ 5
WC_DESCW    equ 28

; --- one 16-byte sidecar entry, at [WC_SCOFF] + WIRE_SC*j --------------------
WC_SCNAME   equ 0               ; 12, a NUL-terminated 8.3 name
WC_SCSIZE   equ 12              ; dword
WIRE_SC     equ 16

; --- WC_FLAGS ----------------------------------------------------------------
WF_DISK     equ 1               ; its files must be ON A DISK: an overlay,
                                ; parts, or sidecars. Load Program refuses
                                ; with that reason; Add to Disk works
WF_PIC      equ 2               ; /wire/pic/<STEM>.PIC exists
WF_NEW      equ 4               ; new on the Wire
WF_FLOPPY   equ 8               ; only as a floppy image - its files live in a
                                ; folder tree the Wire does not create. Both
                                ; actions refuse - UNLESS WF_ARC is set too,
                                ; which is what the writer does for every
                                ; archive so a reader from before 88.13 greys
                                ; it rather than fetching a .O88 that is not
                                ; there
WF_ARC      equ 16              ; an ARCHIVE, /wire/pkg/<STEM>.WPK (SPEC.md
                                ; 88.13): one stream, a folder tree. n = 0.
                                ; Load Program unpacks into a RAM disk and
                                ; runs it (88.14); Add to Disk unpacks where
                                ; the dialog points

; --- WC_KIND -----------------------------------------------------------------
; Only 0..2 are used today; the field is the room the brand leaves to grow,
; and a reader shows a kind it does not know AS A PROGRAM rather than
; refusing the record (SPEC.md 88.2).
WK_PROGRAM  equ 0
WK_GAME     equ 1
WK_UTILITY  equ 2
WK_DOCUMENT equ 3
WK_UPDATE   equ 4
WK_SHARED   equ 5

; --- what the reader will hold ------------------------------------------------
WIRE_VER    equ 1
WIRE_CATMAX equ 16384           ; the claim is made before Content-Length is
                                ; known, so this is the claim AND the ceiling
WIRE_FILEMAX equ 64512          ; 63 KB: one claim, one OSAPI_FILE_WRITE.
                                ; **IT BOUNDS A RECORD'S WC_SIZE ONLY WHEN
                                ; WF_ARC IS CLEAR** - see WIRE_ARCMAX below
WIRE_ARCMAX equ 0x100000        ; 1 MB, and it is THE FIRST SIZE REFUSED: an
                                ; archive's WC_SIZE must be BELOW it, which is
                                ; one compare on the high word rather than
                                ; three on the pair. There is never a claim for
                                ; the whole of a .WPK - the reader decodes one
                                ; entry at a time into a claim sized from
                                ; WA_MAXENT (SPEC.md 88.13) - so this is a
                                ; SANITY bound and not a capacity one, which is
                                ; why it is a round number rather than a
                                ; derived figure. RunCPM's master disk is
                                ; 231,463 bytes on the wire, and the 16-bit
                                ; rule that fits every other record refused it
                                ; outright
WIRE_NMAX   equ 255             ; the format's ceiling. WIRE_CATMAX is the
                                ; binding one - 16,384 bytes is 63 records
                                ; with no sidecar table at all - and it is
                                ; what keeps WIRE_HDR + WIRE_REC*i inside 16
                                ; bits, which the reader's addressing needs
WIRE_SCMAX  equ 8               ; sidecars per record

; --- the picture (SPEC.md 88.3) ----------------------------------------------
WIRE_PICW   equ 128
WIRE_PICH   equ 64
WIRE_PICB   equ 16              ; bytes a row
WIRE_PICSZ  equ 1024
