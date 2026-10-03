; =============================================================================
; os8088 - apps/pixel/pxhelp.asm
;
; PiXEL's KEYBOARD CARD, as a far-called lazy PART (SPEC.md 106.5). It is the
; first part of PIXEL.O88 and the one that proves the boundary before any
; decoder depends on it: View > Keyboard Help fetches it, calls its INIT
; vector (which answers PXP_PROBE), calls INFO to copy the card's lines into
; the package, and drops it again - so a card the user looks at twice a
; session costs no resident byte and no claim while it is not on screen.
; tests/pxparts.py drives exactly that, twice, and checks the heap after.
;
; It obeys apps/pixel/pxpart.inc's four rules: its text is read through CS,
; it touches nothing of the package's but the buffer it was handed, it never
; speaks, and it answers in CF/AX.
;
; THE LINES ARE PADDED TO ONE WIDTH ON PURPOSE. The card is drawn by
; os88ui_about_d, which CENTRES every line; lines of equal length centre to
; the same left edge, so the key column stays a column.
; =============================================================================

%include "pxpart.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD ph_init, ph_decode, ph_info, ph_decode

; PXV_INIT - out AX = PXP_PROBE, CF = 0
ph_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_DECODE, PXV_HEAD - this part decodes nothing
ph_decode:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_INFO - copy the card into ES:DI
; in:  ES:DI = the buffer, CX = its size in bytes
; out: CF = 0, AX = the number of lines, the buffer holding that many NUL
;      strings back to back; CF = 1, AX = PXE_ROOM when CX is too small (and
;      nothing written). Preserves every other register.
ph_info:
    cmp cx, ph_end - ph_text
    jb .room
    push cx
    push si
    push di
    mov si, ph_text
    mov cx, ph_end - ph_text
    cld
.copy:
    mov al, [cs:si]             ; rule 1: the part's own bytes through CS
    stosb
    inc si
    loop .copy
    pop di
    pop si
    pop cx
    mov ax, PH_LINES
    clc
    retf
.room:
    mov ax, PXE_ROOM
    stc
    retf

; The card. Every line the same width (see above), and only keys that DO
; something in this build: a card that lists a key which does nothing is a
; card that is wrong.
ph_text:
    db 'PiXEL - keyboard', 0
    db 0
    db 'Ctrl+O       Open a picture       ', 0
    db 'H  Z  M      Hand, Zoom, Select   ', 0
    db 'C  E  R      Crop, Dropper, Rotate', 0
    db 'Tab          Next panel (1 column)', 0
    db 'F1  or  ?    This card            ', 0
    db 0
    db 'A key or a click closes it.', 0
ph_end:
PH_LINES equ 9
