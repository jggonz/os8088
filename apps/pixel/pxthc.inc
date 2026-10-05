; =============================================================================
; os8088 - apps/pixel/pxthc.inc
;
; THE FOLDER'S SHARED NUMBERS (SPEC.md 106.21): the folder list's record, the
; thumbnail store's layout, a thumbnail's KEY and the cache file's header -
; included by the resident package (pxfolder.inc) and by the FOLDER part
; (pxfold.asm), which both read and write them, so the two cannot disagree.
; Constants only: a linked part learns the package's VARIABLES from
; build/pxlink.inc (SPEC.md 106.20).
; =============================================================================

PX_NREC     equ 20                  ; THE FOLDER LIST: a record a picture -
NR_FLAGS    equ 13                  ; the 8.3 name and its NUL, flags,
NR_CLUS     equ 14                  ; the file's first cluster
NR_SIZE     equ 16                  ; and its size
NRF_PACKED  equ 1                   ; packed on the disk (SPEC.md 20.14)
NRF_NOTH    equ 2                   ; no thumbnail could be made of it

PX_TW       equ 72                  ; A THUMBNAIL at most: 4:3 exactly
PX_TH       equ 54
PX_TSLOTS   equ 6                   ; the thumbnails the store holds
PX_TDATA    equ PX_TW * PX_TH       ; a slot: the thumbnail's bytes, then its
PX_TKSZ     equ 32                  ; key
PXT_SLSZ    equ PX_TDATA + PX_TKSZ
PXT_HDR     equ 0                   ; THE STORE: the cache's header, as read...
PXT_PLAN    equ 0x0820              ; ...the cube's 4bpp plans (768)...
PXT_T1      equ 0x0B20              ; ...its 1bpp thresholds (256)...
PXT_MAP     equ 0x0C20              ; ...a palette onto the cube (256)...
PXT_BUF     equ 0x1000              ; ...a band, or a cache entry in or out...
PXT_ROW     equ 0x1F00              ; ...a band row's values (past the band)
PXT_SL0     equ 0x2000              ; ...and the slots

TK_NAME     equ 0                   ; THE KEY: 12, the 8.3 name, NUL-padded
TK_DIR      equ 12                  ; word: the folder's first cluster
TK_CLUS     equ 14                  ; word: the file's first cluster
TK_SIZE     equ 16                  ; dword: its size
TK_VOL      equ 20                  ; byte: its drive
TK_CMP      equ 21                  ; (the bytes two keys are compared by)
TK_TW       equ 21                  ; byte: the thumbnail's width...
TK_TH       equ 22                  ; ...and height
TK_FLAGS    equ 23                  ; byte: TKF_*
TK_STAMP    equ 24                  ; word: the cache's clock when last used
TKF_VALID   equ 1
TKF_NEW     equ 2                   ; (the store's: not in the cache yet)

CH_VER      equ 4                   ; THE CACHE'S HEADER: 'PXTC', the
CH_COUNT    equ 5                   ; version, the entries used, the clock,
CH_CLOCK    equ 6                   ; and at CH_KEYS a key an entry
CH_KEYS     equ 32
PX_CMAX     equ 64

PF_VISIT    equ 0                   ; THE FOLDER PART'S VERBS (CL, through its
PF_WRITE    equ 1                   ; DECODE vector): the cache read for the
PF_MAKE     equ 2                   ; cards on show, a STEP of the cache's
PF_WALL     equ 3                   ; write (one entry, or the header last:
                                    ; AX = 2 more to do, 0 done), a thumbnail
                                    ; made from [px_cur]'s master, and the
                                    ; whole write in one call (the window
                                    ; closing)
