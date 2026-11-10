; os8088 - tests/pdcrypt/pdchar.asm
; The raw harness image tests/pdcrypt/pdc_diff.py runs under Unicorn: it
; %includes the SHIPPING pdc*.inc files and exports a table of entry points at
; offset 3 (one `dw` per `T` line, in order), so the Python side needs no map
; file. DS = CS; bss follows the image in the same segment.
cpu 8086
bits 16
org 0
section .text   start=0
section .data   follows=.text align=2
section .bss    follows=.data align=2 nobits
section .text
        jmp     near pdch_start
pdch_table:
%macro T 1
        dw %1
%endmacro
%include "pdchar.tab"
pdch_start:
        hlt
%include "pagedesk/pdcmd5.inc"
%include "pagedesk/pdcrc4.inc"
%include "pagedesk/pdcaes.inc"
%include "pagedesk/pdcsha2.inc"
section .data
align 2, db 0
pdch_image_end:
