; kernel/cpwfmt.inc - CPWIDE.DAT's layout (SPEC.md 135.6, 135.7).
;
; Shared by the wide Control Panel (kernel/cpw*.inc, assembled into
; CPWIDE.DRV) and checked by tools/cpwdat.py, which builds the file from
; kernel/cpwart/*.png and drivers/catalog.toml and refuses to write one that
; disagrees with these numbers. Constants only: no code, no data, no section.

; ---- header -----------------------------------------------------------------
CWD_MAGIC    equ 'CW'          ; +0  dw, reads 'C','W'
CWD_VERSION  equ 1
CWD_H_MAGIC  equ 0             ; dw  'CW'
CWD_H_VER    equ 2             ; db  CWD_VERSION
CWD_H_N16    equ 3             ; db  16x16 icon count
CWD_H_N32    equ 4             ; db  32x32 icon count
CWD_H_NCAT   equ 5             ; db  catalog records
CWD_H_OFF16  equ 6             ; dw  offset of the 16x16 block
CWD_H_OFF32  equ 8             ; dw  offset of the 32x32 block
CWD_H_OFFCAT equ 10            ; dw  offset of the catalog
CWD_H_SIZE   equ 12            ; dw  the file's length
CWD_H_STR    equ 14            ; dw  offset of the description string block
CWD_HDR      equ 16

; ---- icons ------------------------------------------------------------------
; 4bpp packed, high nibble leftmost (SPEC.md 5.4), then the mask: one bit a
; pixel, bit 15 of each word the leftmost, 1 = opaque. A transparent pixel's
; nibble is 0 and the drawer substitutes the ground colour.
CWD_I16_PIX  equ 128           ; 16 rows x 8 bytes
CWD_I16_STR  equ 8             ; stride
CWD_I16_SZ   equ 160           ; + 16 mask words
CWD_I32_PIX  equ 512           ; 32 rows x 16 bytes
CWD_I32_STR  equ 16
CWD_I32_SZ   equ 640           ; + 32 rows x 2 mask words (left word first)

; 16x16 indices = cells of kernel/cpwart/cp-icons-16.png, row-major
CWI16_SCHED  equ 0
CWI16_TIME   equ 1
CWI16_KEYB   equ 2
CWI16_FLOPPY equ 3
CWI16_DRV    equ 4
CWI16_DISP   equ 5
CWI16_SOUND  equ 6
CWI16_THEME  equ 7
CWI16_DOCK   equ 8
CWI16_HDD    equ 9
CWI16_NET    equ 10
CWI16_DEBUG  equ 11
CWI16_GEAR   equ 12
CWI16_MOUSE  equ 13
CWI16_MEM    equ 14
CWI16_PORTS  equ 15
CWI16_C_DISP equ 16            ; driver categories and status marks
CWI16_C_SND  equ 17
CWI16_C_STOR equ 18
CWI16_C_CD   equ 19
CWI16_C_RAMD equ 20
CWI16_C_NET  equ 21
CWI16_C_INP  equ 22
CWI16_C_MEM  equ 23
CWI16_C_SYS  equ 24
CWI16_C_SAVR equ 25
CWI16_C_USB  equ 26
CWI16_C_VM   equ 27
CWI16_C_UNK  equ 28
CWI16_OK     equ 29
CWI16_ERR    equ 30
CWI16_WARN   equ 31
CWI16_JOY    equ 32            ; the joystick (row 2, col 0; SPEC.md 135.6)
CWI16_N      equ 33

; 32x32 indices = cells of kernel/cpwart/cp-icons-32.png
CWI32_SCHED  equ 0
CWI32_TIME   equ 1
CWI32_KEYB   equ 2
CWI32_FLOPPY equ 3
CWI32_DRV    equ 4
CWI32_DISP   equ 5
CWI32_SOUND  equ 6
CWI32_THEME  equ 7
CWI32_DOCK   equ 8
CWI32_HDD    equ 9
CWI32_NET    equ 10
CWI32_GEAR   equ 11
CWI32_JOY    equ 12            ; the joystick (cell 12 of the single row)
CWI32_N      equ 13

; ---- the driver catalog (SPEC.md 135.7) -------------------------------------
CWD_CAT_SZ   equ 64            ; one fixed record per driver FILE
CWD_C_FILE   equ 0             ; db [12]  8.3 name, NUL-padded: the key
CWD_C_CAT    equ 12            ; db       CWC_*
CWD_C_ICON   equ 13            ; db       16x16 icon index
CWD_C_LONG   equ 14            ; db [26]  long name, NUL-terminated, <= 25
CWD_C_D1     equ 40            ; dw       offset (from file start) of line 1
CWD_C_D2     equ 42            ; dw       ...line 2 (0 = none)
CWD_C_PAD    equ 44            ; 20 reserved bytes, 0
CWD_DESC_MAX equ 46            ; a description line, characters, NUL-terminated

CWC_UNK      equ 0
CWC_DISP     equ 1
CWC_SOUND    equ 2
CWC_STOR     equ 3
CWC_NET      equ 4
CWC_INPUT    equ 5
CWC_MEM      equ 6
CWC_SYS      equ 7
CWC_N        equ 8
