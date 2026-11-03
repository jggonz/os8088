; =============================================================================
; os8088 - tests/dostrap/exenext.asm - the program AFTER an .EXE
; (SPEC.md 96.8.3)
;
; ONE source, TWO programs, run one after the other from one DOS prompt:
;
;   nasm -f bin -DEXE -o XFIRST.EXE tests/dostrap/exenext.asm
;   nasm -f bin       -o CNEXT.COM  tests/dostrap/exenext.asm
;
; XFIRST.EXE is a hand-built MZ whose ENTRY is 0x400 bytes into its image and
; whose stack is its own; it prints `XFIRST ran` and exits with code 88 ('X').
; CNEXT.COM is under 0x100 bytes; it prints `CNEXT ran` and exits with code
; 67 ('C').
;
; The pair is the experiment. A DOS that remembers the .EXE's entry point
; after the .EXE ended (os8088's DOS did: `[dos_isexe]` was set by the loader
; for an .EXE and cleared for a .COM only on AH=4Bh's path, never on the
; prompt's) enters CNEXT at XFIRST's CS:IP - which is still in memory, 0x400
; bytes in, past the few bytes CNEXT's own image overwrote - so the .COM
; prints `XFIRST ran` and ends with code 88. That is IBM DOS 3.30's REPLACE
; followed by SYS, which hung, with no owner binary in it. Both run under a
; real IBM DOS unchanged and print one line each.
; =============================================================================
cpu 8086

%ifdef EXE
ENTRY   equ 0x400
IMAGE   equ 0x500                   ; image bytes after the header
org 0
    db 'MZ'
    dw (32 + IMAGE) % 512           ; bytes in the last page
    dw (32 + IMAGE + 511) / 512     ; pages, header included
    dw 0                            ; no relocations
    dw 2                            ; header paragraphs (32 bytes)
    dw 0x0010                       ; minalloc
    dw 0xFFFF                       ; maxalloc
    dw 0                            ; SS, relative to the load segment
    dw IMAGE                        ; SP: the top of the image
    dw 0                            ; checksum
    dw ENTRY                        ; IP
    dw 0                            ; CS, relative
    dw 0x1C                         ; relocation table
    dw 0                            ; overlay
    times 32 - ($ - $$) db 0
image:
    times ENTRY - ($ - image) db 0  ; zeros, so a stale entry is not saved by
                                    ; luck landing on code
    push cs                         ; CS = the load segment, so the message
    pop ds                          ; is CS-relative
    mov dx, msg - image
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C58                  ; 'X'
    int 0x21
msg: db 'XFIRST ran', 13, 10, '$'
    times IMAGE - ($ - image) db 0
%else
org 0x100
    mov dx, msg
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C43                  ; 'C'
    int 0x21
msg: db 'CNEXT ran', 13, 10, '$'
%endif
