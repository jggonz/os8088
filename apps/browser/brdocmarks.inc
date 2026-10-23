; The formatted document stream's markers, shared by browser.asm and the lazy
; Download Manager part (brdownload_part.asm) so the two cannot disagree.
D_END        equ 0                  ; end of document
D_PARA       equ 1                  ; paragraph break (a blank line)
D_BR         equ 2                  ; line break (no blank line)
D_HR         equ 3                  ; a rule across the band
D_LI         equ 4                  ; list item - the bullet is drawn, not stored
D_HEAD       equ 5                  ; + 1 byte level 1..6
D_CEN1       equ 6                  ; centre on
D_CEN0       equ 7                  ; centre off
D_PRE1       equ 8                  ; preformatted on
D_PRE0       equ 9                  ; preformatted off
D_TAB1       equ 10                 ; table start / end, and one marker per
D_TAB0       equ 11                 ; row and cell inside it (BROWSER-PLAN 3.2)
D_ROW        equ 12
D_CELL       equ 13
D_LNK1       equ 14                 ; a link opens, and the THREE BYTES after it
                                    ; are its index: low, middle and high nibbles
                                    ; each tagged into 16..31.
                                    ; **THAT RANGE IS THE WHOLE TRICK.** Every
                                    ; walker in this file already skips a byte
                                    ; below 0x20 as "a marker, not a cell", so
                                    ; the payload costs not one of them a line
                                    ; of change; and the markers above 15 -
                                    ; D_Q1..D_CMETA, 16..24 - are all ones a
                                    ; BACKWARD scan may step over as if they
                                    ; were payload, so it can still tell a
                                    ; payload byte from D_LNK1/D_LNK0 and
                                    ; answer "which link is this offset
                                    ; inside" without a table. The one byte
                                    ; that is NEITHER is D_CMETA's own payload
                                    ; (0..23, which can read as any marker),
                                    ; so every walker consumes the triple and
                                    ; br_linkat refuses a D_LNK1 found straight
                                    ; after a D_CMETA
D_LNK0       equ 15                 ; ...and closes
D_Q1         equ 16                 ; quote indent starts; D_LNK1 consumes its
D_Q0         equ 17                 ; ...three payload nibbles before layout sees them
D_B1         equ 18                 ; bold starts (zero-width, paired controls)
D_B0         equ 19
D_I1         equ 20                 ; italic starts
D_I0         equ 21
D_U1         equ 22                 ; underline starts
D_U0         equ 23
D_CMETA      equ 24                 ; one byte after D_CELL: span (bits 0..2),
                                    ; alignment (bits 3..4); zero-width control

D_NBSP       equ 0x7F               ; A NO-BREAK SPACE, and it is 0x7F rather
                                    ; than a marker because it is TEXT: it
                                    ; occupies a cell, counts toward the wrap
                                    ; width and is copied like any other byte -
                                    ; it simply is not the ' ' that br_layout
                                    ; breaks at. br_build renders it as a
                                    ; space. 0x7F is past the last ROM glyph
                                    ; (0x20..0x7E), so it can never reach
                                    ; font_char even if a walk here is wrong
