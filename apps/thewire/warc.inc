; =============================================================================
; os8088 - apps/thewire/warc.inc
;
; THE ARCHIVE FORMAT, version 1 (SPEC.md 88.13) - offsets and limits only.
;
; **EVERY EQU HERE IS MIRRORED IN tools/os88wire.py** and tests/unit/t_wire.py
; compares the two in the FAST tier, exactly as wcat.inc's are. The website's
; tools/wire.py is a second writer of this format and the OS's verifier is
; what checks what it wrote.
;
; Nothing in this file emits a byte. It is included by thewire.asm beside
; wcat.inc, and by nothing else on the machine.
; =============================================================================

; --- the 32-byte header ------------------------------------------------------
WA_MAGIC    equ 0               ; 4 bytes, 'WPAK'
WA_VER      equ 4               ; byte, = WARC_VER
WA_FLAGS    equ 5               ; byte, WAH_*
WA_N        equ 6               ; word, the entry count, 1..WARC_NMAX
WA_TOTAL    equ 8               ; dword, the unpacked total
WA_NEEDKB   equ 12              ; word, the RAM disk KB the tree needs
WA_MAXENT   equ 14              ; word, the largest unpacked entry, bytes
WA_HOME     equ 16              ; 12, a NUL-terminated 8.3 folder, or all-NUL
WARC_HDR    equ 32

; --- one 64-byte entry header, followed at once by its body -------------------
WA_METHOD   equ 0               ; byte, WAM_*
WA_DEPTH    equ 1               ; byte, 0..WARC_DEPTH folders before the name
WA_STORED   equ 4               ; dword, the body's bytes on the wire
WA_SIZE     equ 8               ; dword, the unpacked size, <= WIRE_FILEMAX
WA_PATH     equ 16              ; WARC_SLOTS slots of WARC_SLOT bytes
WARC_ENT    equ 64
WARC_SLOT   equ 12
WARC_SLOTS  equ 4
WARC_DEPTH  equ 3

; --- WA_FLAGS ----------------------------------------------------------------
WAH_PROGRAM equ 1               ; the LAST entry is a package to run

; --- WA_METHOD ---------------------------------------------------------------
WAM_STORED  equ 0
WAM_LZSS    equ 1               ; SPEC.md 88.13's decoder: distance 1..4096,
                                ; length 3..18, flags LSB first

; --- what the reader will hold ------------------------------------------------
WARC_VER    equ 1
WARC_NMAX   equ 255
WARC_DISTMAX equ 4096
WARC_LENMIN equ 3
WARC_LENMAX equ 18
