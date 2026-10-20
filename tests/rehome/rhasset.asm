; =============================================================================
; os8088 - tests/rehome/rhasset.asm
;
; Part 1 of REHOME.O88: the thing the LOADER loads and the PROGRAM finds.
;
; It is deliberately not code. What it is for is check 2 of tests/rehome's
; four - that the segment the loader wrote into the program's bss is where the
; standard actually put these bytes - and a signature answers that with
; nothing called. A far call would prove the same thing and would also prove
; that the part is executable, which is tests/multiseg's subject and not this
; one.
; =============================================================================
    cpu 8086
    bits 16
    org 0

    db 'R', 'A'                     ; +0 the signature the program compares
    dw 0x5AA5                       ; +2 ...and a value only this part carries
    db 'rehome asset', 0
    times 256 - ($ - $$) db 0DBh    ; a whole sector's worth, so a short read
                                    ; would leave zeros where 0xDB should be
