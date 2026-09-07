; =============================================================================
; os8088 - apps/apple2/hosttest/a2memtest.asm
;
; A BOOT SECTOR THAT TESTS apps/apple2/a2mem.inc AND apps/apple2/a2band.inc ON
; A REAL x86 - the two files where this package loads ES and runs a string
; instruction (docs/APPLE2-SPEC.md section 3.4). The host harness cannot reach
; them (it substitutes C), tools/cc8086.py never sees hand-written assembly,
; and a routine that got a segment wrong would write into the kernel and NOT
; FAULT (LESSONS.md 4). So, as apps/c64/hosttest/c64memtest.asm does for the
; 6510's movers and apps/runcpm/hosttest/rcmemtest.asm for the Z80's, this
; runs the SHIPPING text - %included, not copied - under the one condition
; that makes this OS unlike every other target: SS != DS.
;
; THE RULE IS WIDER THAN THE MOVERS, and that is why a2band.inc is here too:
; a2_rowspan's forward and REVERSE `repe cmpsb` set DF on purpose, and a
; reverse scan that forgot to clear it would leave every later string
; instruction in the task running backwards.
;
; Here CS = DS = 0 stand in for the package (a2mem.inc reads the claims
; through DS, exactly as it does in a package where CS = DS), SS = 0x1000 is
; the task stack, ES = 0x3000 stands in for KERNEL_SEG and must come back
; intact, 0x4000 is the Apple's RAM claim and 0x5000 its ROM part.
;
; WHAT EACH CASE CHECKS, and none of it is the routine's own logic restated:
;
;   1. bytes written through a2_wr read back through a2_rd and through a
;      direct far read of the claim, little-endian through a2_rd16, and
;      a2_rom_rd reads the OTHER claim;
;   2. **THE WRITE FENCE, AND THE SCRATCH'S ZERO STATED DEVIATIONS**
;      (section 3.3): a write at $C000 and above is DROPPED - so the core's
;      scratch at $CF00 is unreachable from the emulated machine and needs no
;      deviation paragraph at all, where the C64's $FFC0 needed two - and
;      $BFFF is still real RAM;
;   3. the DIRTY BIT and the WRITE WINDOW a write leaves behind (7.5): the
;      right bit of the right byte, the lowest/highest address, and - the row
;      that matters once there is a core - that a write OUTSIDE THE WATCH
;      RANGE sets the dirty bit and the "something was written" byte and does
;      NOT widen the window;
;   3b. a2_dirty_take, the ONE call a flush makes for all of that: the 32-byte
;      bitmap and the window's two words land in the PACKAGE's memory in the
;      documented slots (dst[0..31], dst[32..35]), the 37th byte is a guard it
;      must not reach, and the scratch is RESET in the same pass;
;   3c. a2_wrote answers that one byte, and answers 0 after the take;
;   4. every mover leaves the destination equal to the source byte for byte
;      and touches nothing outside it (a guard byte each side), and
;      a2_chargen masks each byte to seven bits on the way across;
;   5a. **a2band.inc's CROSS-SEGMENT COMPOSER**, which section 3.4's rule is
;      actually about: a2_band_text composing one group out of a screen row in
;      the RAM claim through the near _a2_chr, with one cell per branch of the
;      per-cell XOR mask - NORMAL, INVERSE and FLASHING - and the SEVEN-BIT
;      PACKING checked against a hand-computed byte, because eight cells to
;      seven bytes is the one piece of arithmetic in this package that a
;      transcription can get plausibly wrong;
;   5b. a2_rowsig answers a different signature when one byte of the claim
;      moves, and a2_rowflash finds a $40-$7F byte and only that;
;   5c. a2_rowspan answers "same" on equal rows and the exact first/last
;      differing BYTE on unequal ones, and a2_rowcopy makes them equal again;
;   6. after every call ES, DS, BP and SP are what they were and DF IS CLEAR;
;   7. **FOUR NEGATIVE CONTROLS** - one each for the ES, DF, BP and DS the
;      discipline check claims to check. A harness that cannot see a broken
;      routine has proved nothing;
;   8. a2_x2init's table and a2_band_x2's doubling, which is the routine the
;      C64 shipped WRONG IN TWO PLACES with nothing able to see it.
;
; RUN IT:  apps/apple2/hosttest/a2memtest.sh
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000
SEG_RAM     equ 0x4000
SEG_ROM     equ 0x5000
IMG_SECTORS equ 64

%macro PUSHI 1
    mov ax, %1
    push ax
%endmacro

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector reads the rest of the image in
; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
.err:
    mov al, 'D'
    call putc
    jmp halt
lba: dw 0

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld
    xor bp, bp                      ; failures
    mov word [_a2_m+AM_RAMSEG], SEG_RAM
    mov word [_a2_m+AM_ROMSEG], SEG_ROM

    mov si, msg_hello
    call puts

    call ramclear
    call romfill
    call chrfill

    ; --- (1) the accessors ---------------------------------------------------
    mov word [pushes], 2
    PUSHI 0xA5
    PUSHI 0x1234
    call disc_call
    dw _a2_wr
    PUSHI 0x5A
    PUSHI 0x2000
    call disc_call
    dw _a2_wr
    PUSHI 0xC3
    PUSHI 0x2001
    call disc_call
    dw _a2_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x1234], 0xA5
    jne .f1
    pop es
    mov word [pushes], 1
    PUSHI 0x1234
    call disc_call
    dw _a2_rd
    cmp ax, 0x00A5                  ; AH must be 0 (LESSONS.md 4)
    jne .f1b
    PUSHI 0x2000
    call disc_call
    dw _a2_rd16
    cmp ax, 0xC35A                  ; little-endian
    jne .f1b
    ; a2_rom_rd reads the OTHER claim: romfill put 0x80 + (off & 0x3F) there
    PUSHI 0x0005
    call disc_call
    dw _a2_rom_rd
    cmp ax, 0x0085
    jne .f1b
    mov al, '.'
    call putc
    jmp .fence
.f1:
    pop es
.f1b:
    mov al, '1'
    call putc
    inc bp

    ; --- (2) THE WRITE FENCE, and the scratch's zero deviations (3.3) --------
.fence:
    mov word [pushes], 2
    PUSHI 0x77
    PUSHI 0xC000                    ; the soft switches: DROPPED
    call disc_call
    dw _a2_wr
    PUSHI 0x77
    PUSHI A2_SCR_BASE + 0x10        ; the core's SCRATCH: DROPPED, which is
    call disc_call                  ;   what makes it safe with no stated
    dw _a2_wr                       ;   deviation at all
    PUSHI 0x77
    PUSHI 0xFFFF                    ; the reset vector, in ROM: DROPPED
    call disc_call
    dw _a2_wr
    PUSHI 0x77
    PUSHI 0xBFFF                    ; the last byte of the 48K: real RAM
    call disc_call
    dw _a2_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0xC000], 0x77
    je .f2e
    cmp byte [es:A2_SCR_BASE + 0x10], 0x77
    je .f2e
    cmp byte [es:0xFFFF], 0x77
    je .f2e
    cmp byte [es:0xBFFF], 0x77
    jne .f2e
    pop es
    mov al, '.'
    call putc
    jmp .dirty
.f2e:
    pop es
    mov al, '2'
    call putc
    inc bp

    ; --- (3) the dirty bit and the write window (7.5) ------------------------
.dirty:
    call ramclear
    mov word [pushes], 2
    PUSHI 0x11
    PUSHI 0x0512                    ; page 5 -> bitmap byte 0, bit 0x04.
    call disc_call                  ;   INSIDE the watch range
    dw _a2_wr
    PUSHI 0x22
    PUSHI 0x8A00                    ; page 0x8A -> byte 17, bit 0x20.
    call disc_call                  ;   OUTSIDE it: the bit is set, the
    dw _a2_wr                       ;   window must NOT move
    PUSHI 0x33
    PUSHI 0x0140                    ; the stack page - what a JSR writes, and
    call disc_call                  ;   the exact case the watch range exists
    dw _a2_wr                       ;   to keep out of the window
    PUSHI 0x44
    PUSHI 0x0400                    ; INSIDE, and below $0512: the low end
    call disc_call                  ;   moves for this one
    dw _a2_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    ; bitmap byte 0 covers pages 0-7, page p being bit 0x80 >> p: page 1
    ; ($0140) 0x40, page 4 ($0400) 0x08, page 5 ($0512) 0x04 = 0x4C. EVERY
    ; write sets its bit, whether or not it is in the watch range - the
    ; bitmap is what sees a page the display is about to be switched to.
    cmp byte [es:A2_SCR_BASE + 0], 0x4C
    jne .f3e
    cmp byte [es:A2_SCR_BASE + 17], 0x20    ; ...and page 0x8A, out of range
    jne .f3e
    cmp byte [es:A2_SCR_BASE + A2_SCR_ANY], 1
    jne .f3e
    cmp word [es:A2_SCR_BASE + A2_SCR_WLO], 0x0400  ; the lowest written IN
    jne .f3e                                        ;   RANGE
    cmp word [es:A2_SCR_BASE + A2_SCR_WHI], 0x0512  ; ...and the highest, and
    jne .f3e                                        ;   NOT $8A00 or $0140
    pop es
    mov al, '.'
    call putc
    jmp .take
.f3e:
    pop es
    mov al, '3'
    call putc
    inc bp

    ; --- (3b) a2_dirty_take: 36 bytes, a guard, and the reset ---------------
.take:
    mov word [pushes], 1
    mov ax, take
    push ax
    call disc_call
    dw _a2_dirty_take
    cmp byte [take + 0], 0x4C
    jne .f3be
    cmp byte [take + 17], 0x20
    jne .f3be
    cmp word [take + 32], 0x0400
    jne .f3be
    cmp word [take + 34], 0x0512
    jne .f3be
    cmp byte [take + 36], 0xA5      ; THE GUARD: it takes exactly 36 bytes
    jne .f3be
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:A2_SCR_BASE + 0], 0        ; ...and RESETS the scratch in the
    jne .f3bp                               ;   same pass
    cmp byte [es:A2_SCR_BASE + 17], 0
    jne .f3bp
    cmp word [es:A2_SCR_BASE + A2_SCR_WLO], 0xFFFF
    jne .f3bp
    cmp word [es:A2_SCR_BASE + A2_SCR_WHI], 0
    jne .f3bp
    cmp byte [es:A2_SCR_BASE + A2_SCR_ANY], 0
    jne .f3bp
    pop es
    ; --- (3c) ...and a2_wrote reads that byte, and reads 0 now --------------
    mov word [pushes], 0
    call disc_call
    dw _a2_wrote
    or ax, ax
    jnz .f3be
    mov word [pushes], 2
    PUSHI 0x55
    PUSHI 0x0500
    call disc_call
    dw _a2_wr
    mov word [pushes], 0
    call disc_call
    dw _a2_wrote
    cmp ax, 1
    jne .f3be
    mov al, '.'
    call putc
    jmp .movers
.f3bp:
    pop es
.f3be:
    mov al, '4'
    call putc
    inc bp

    ; --- (4) the movers ------------------------------------------------------
.movers:
    call ramclear
    mov si, src
    mov di, dst
    mov cx, 24
    xor al, al
.mf:
    mov [si], al
    mov byte [di], 0
    inc si
    inc di
    inc al
    loop .mf

    mov word [pushes], 3
    PUSHI 24
    mov ax, src
    push ax
    PUSHI 0x0900
    call disc_call
    dw _a2_zcopy_in
    mov ax, 0x0900
    call zcheck
    jc .f5e

    PUSHI 24
    PUSHI 0x0900
    mov ax, dst
    push ax
    call disc_call
    dw _a2_zcopy_out
    mov si, src
    mov di, dst
    mov cx, 24
.mc:
    mov al, [si]
    cmp al, [di]
    jne .f5e
    inc si
    inc di
    loop .mc
    cmp byte [dst + 24], 0xCC       ; the guard past the destination
    jne .f5e

    PUSHI 16
    PUSHI 0x5A
    PUSHI 0x0A00
    call disc_call
    dw _a2_zfill
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0A00], 0x5A
    jne .f5p
    cmp byte [es:0x0A0F], 0x5A
    jne .f5p
    cmp byte [es:0x0A10], 0
    jne .f5p
    pop es

    ; a2_chargen masks each byte to seven bits on the way out of the part
    mov word [pushes], 4
    PUSHI 8
    PUSHI 0
    PUSHI SEG_ROM
    mov ax, dst
    push ax
    call disc_call
    dw _a2_chargen
    mov si, dst
    mov cx, 8
    xor bx, bx
.mg:
    mov al, bl
    and al, 0x3F
    or al, 0x80                     ; what romfill wrote...
    and al, 0x7F                    ; ...and what a2_chargen must answer
    cmp [si], al
    jne .f5e
    inc si
    inc bx
    loop .mg
    mov al, '.'
    call putc
    jmp .band
.f5p:
    pop es
.f5e:
    mov al, '5'
    call putc
    inc bp

    ; --- (5a) THE COMPOSER, ACROSS THE SEGMENT BOUNDARY ---------------------
.band:
    call ramclear
    ; ONE GROUP of eight cells at $0400, one per branch of the per-cell mask:
    ;   cell 0  $C1  NORMAL    mask $00
    ;   cell 1  $01  INVERSE   mask $7F
    ;   cell 2  $41  FLASHING  mask = the phase's
    ;   cells 3-7 $A0 (a normal space)
    ; and _a2_chr is filled by chrfill so that glyph i, row r is
    ; ((i + r) & 0x7F) - a value that is different in every cell and every
    ; row, so a packing that shifted by the wrong amount cannot come out right
    ; by accident.
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0400], 0xC1
    mov byte [es:0x0401], 0x01
    mov byte [es:0x0402], 0x41
    mov bx, 0x0403
    mov cx, 5
.bs:
    mov byte [es:bx], 0xA0
    inc bx
    loop .bs
    pop es

    mov word [pushes], 6
    PUSHI 0                         ; fmask: the phase shows the NORMAL form
    PUSHI 0x0400                    ; moff
    PUSHI SEG_RAM                   ; mseg
    PUSHI 0                         ; g1
    PUSHI 0                         ; g0
    mov ax, banda
    push ax                         ; dst
    call disc_call
    dw _a2_band_text
    ; THE HAND-COMPUTED BYTE. Glyph index is `byte & 0x3F`, so the eight cells
    ; decode to chr[0x01], chr[0x01]^0x7F, chr[0x01], chr[0x20] x5; at pixel
    ; row 0 chrfill makes chr[i] = i & 0x7F, so the seven-bit values are
    ; 0x01, 0x7E, 0x01, 0x20, 0x20, 0x20, 0x20, 0x20 and the first packed byte
    ; is (g0 << 1) | (g1 >> 6) = 0x02 | 0x01 = 0x03.
    cmp byte [banda + A2_LBOX], 0x03
    jne .f6e
    ; ...and the second is (g1 << 2) | (g2 >> 5) = 0xF8 | 0x00 = 0xF8
    cmp byte [banda + A2_LBOX + 1], 0xF8
    jne .f6e
    ; THE LETTERBOX IS NEVER WRITTEN: bytes 0-1 and 37-39 of every band row
    cmp byte [banda + 0], 0
    jne .f6e
    cmp byte [banda + 1], 0
    jne .f6e
    ; ...and the FLASHING cell inverts when the phase does
    mov word [pushes], 6
    PUSHI 0x7F                      ; fmask: the swapped phase
    PUSHI 0x0400
    PUSHI SEG_RAM
    PUSHI 0
    PUSHI 0
    mov ax, bandb
    push ax
    call disc_call
    dw _a2_band_text
    mov si, banda
    mov di, bandb
    mov cx, 7
    xor dx, dx
.bd:
    mov al, [si + A2_LBOX]
    cmp al, [di + A2_LBOX]
    je .bd2
    inc dx
.bd2:
    inc si
    inc di
    loop .bd
    or dx, dx
    jz .f6e                         ; the phase changed NOTHING: the mask is
                                    ; not reaching the $40-$7F cell
    mov al, '.'
    call putc
    jmp .sig
.f6e:
    mov al, '6'
    call putc
    inc bp

    ; --- (5b) the signature and the flash scan ------------------------------
.sig:
    mov word [pushes], 3
    PUSHI 40
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _a2_rowsig
    mov [sig1], ax
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0405], 0xD5      ; one byte of the claim moves...
    pop es
    PUSHI 40
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _a2_rowsig
    cmp ax, [sig1]
    je .f7e                         ; ...and the signature must not agree
    ; a2_rowflash: the row holds $41, which is in $40-$7F
    PUSHI 40
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _a2_rowflash
    cmp ax, 1
    jne .f7e
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0402], 0xA0      ; ...take the flashing cell away
    pop es
    PUSHI 40
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _a2_rowflash
    or ax, ax
    jnz .f7e
    mov al, '.'
    call putc
    jmp .span
.f7e:
    mov al, '7'
    call putc
    inc bp

    ; --- (5c) the compare and the copy --------------------------------------
.span:
    mov si, banda
    mov di, bandb
    mov cx, 40
    mov al, 0x33
.sp1:
    mov [si], al
    mov [di], al
    inc si
    inc di
    loop .sp1
    mov word [pushes], 3
    PUSHI 40
    mov ax, bandb
    push ax
    mov ax, banda
    push ax
    call disc_call
    dw _a2_rowspan
    cmp ax, 0xFFFF                  ; equal rows: -1, and nothing is drawn
    jne .f8e
    mov byte [banda + 5], 0x99
    mov byte [banda + 30], 0x99
    PUSHI 40
    mov ax, bandb
    push ax
    mov ax, banda
    push ax
    call disc_call
    dw _a2_rowspan
    cmp ax, 0x051E                  ; (first << 8) | last = bytes 5 and 30
    jne .f8e
    PUSHI 40
    mov ax, banda
    push ax
    mov ax, bandb
    push ax
    call disc_call
    dw _a2_rowcopy
    PUSHI 40
    mov ax, bandb
    push ax
    mov ax, banda
    push ax
    call disc_call
    dw _a2_rowspan
    cmp ax, 0xFFFF                  ; ...and the copy made them equal again
    jne .f8e
    mov al, '.'
    call putc
    jmp .x2
.f8e:
    mov al, '8'
    call putc
    inc bp

    ; --- (8) the pixel doubler, which the C64 shipped WRONG IN TWO PLACES ----
.x2:
    mov word [pushes], 0
    call disc_call
    dw _a2_x2init
    ; every entry of the table is a doubled nibble: 0x00, 0x03, 0x0C, 0x0F,
    ; 0x30, ... and $FF doubles to $FFFF, $01 to $0003, $80 to $C000
    cmp word [a2_x2tab + 0xFF*2], 0xFFFF
    jne .f9e
    cmp word [a2_x2tab + 0x01*2], 0x0300    ; {high, low} in THAT order
    jne .f9e
    cmp word [a2_x2tab + 0x80*2], 0x00C0
    jne .f9e
    mov si, banda
    mov cx, 40
    mov al, 0x81
.x2f:
    mov [si], al
    inc si
    loop .x2f
    mov word [pushes], 3
    PUSHI 1
    mov ax, banda
    push ax
    mov ax, bandc
    push ax
    call disc_call
    dw _a2_band_x2
    cmp byte [bandc + 0], 0xC0      ; 0x81 -> 0xC003
    jne .f9e
    cmp byte [bandc + 1], 0x03
    jne .f9e
    cmp byte [bandc + 80], 0xC0     ; ...and the row is emitted TWICE
    jne .f9e
    mov al, '.'
    call putc
    jmp .neg
.f9e:
    mov al, '9'
    call putc
    inc bp

    ; --- (7) THE FOUR NEGATIVE CONTROLS -------------------------------------
.neg:
    mov word [disc_expect_bad], 1
    mov word [pushes], 0
    call disc_call
    dw bad_es
    call disc_call
    dw bad_df
    call disc_call
    dw bad_bp
    call disc_call
    dw bad_ds
    mov word [disc_expect_bad], 0

    ; --- the summary ---------------------------------------------------------
    mov si, msg_nl
    call puts
    mov ax, bp
    call putnum
    or bp, bp
    jnz .bad
    mov si, msg_ok
    call puts
    jmp halt
.bad:
    mov si, msg_bad
    call puts
    jmp halt

; -----------------------------------------------------------------------------
; disc_call - call the routine whose near address follows the call, then check
; that it kept the discipline: ES, DS, BP and SP as they were, DF clear.
;
; FROM THE `call` ONWARDS DS MAY BE ANYTHING. A routine that returned with DS
; on a claim would make every store below land in the claim instead of in this
; program - the checker's own bookkeeping corrupted by the very fault it
; exists to catch. CS = DS in this harness (both 0), so a CS override reaches
; the same bytes whatever DS now holds.
; -----------------------------------------------------------------------------
disc_call:
    pop bx
    mov ax, [bx]
    mov [disc_target], ax
    add bx, 2
    mov [disc_ret], bx
    mov [disc_sp], sp
    mov [disc_bp0], bp              ; BP AS IT IS **BEFORE** THE CALL
    call [disc_target]
    mov [cs:disc_ax], ax
    mov [cs:disc_sp2], sp
    mov [cs:disc_bp], bp
    mov [cs:disc_es], es
    mov [cs:disc_ds], ds
    pushf
    pop bx
    mov [cs:disc_fl], bx
    cld
    mov bx, SEG_KERNEL
    mov es, bx
    push cs
    pop ds                          ; ...and ours is ours again
    mov bx, [pushes]
    shl bx, 1
    add sp, bx
    mov bp, [disc_bp0]              ; the checker PUTS BACK the thing it just
                                    ; measured: BP is this program's failure
                                    ; counter
    mov bx, 0
    mov ax, [disc_bp0]
    cmp [disc_bp], ax
    jne .b
    cmp word [disc_es], SEG_KERNEL
    jne .b
    cmp word [disc_ds], 0
    jne .b
    mov ax, [disc_sp]
    cmp [disc_sp2], ax
    jne .b
    test word [disc_fl], 0x0400     ; DF
    jz .ok
.b: inc bx
.ok:
    cmp word [disc_expect_bad], 0
    jne .negc
    or bx, bx
    jz .pass
    mov al, 'X'
    call putc
    inc bp
    jmp .out
.pass:
    mov al, '.'
    call putc
    jmp .out
.negc:
    or bx, bx
    jz .slipped
    mov al, 'N'
    call putc
    jmp .out
.slipped:
    mov al, 'x'
    call putc
    inc bp
.out:
    mov ax, [disc_ax]
    jmp [disc_ret]

; THE NEGATIVE CONTROLS - four routines that break the discipline on purpose,
; one per thing disc_call claims to check.
bad_es:
    push bp
    mov bp, sp
    mov es, [_a2_m+AM_RAMSEG]
    pop bp
    ret
bad_df:
    push bp
    mov bp, sp
    std
    pop bp
    ret
bad_bp:
    push bp
    mov bp, sp
    pop bp
    inc bp                          ; ...and returns with BP one out
    ret
bad_ds:
    push bp
    mov bp, sp
    mov ax, SEG_RAM
    mov ds, ax                      ; ...and returns with DS on the claim
    pop bp
    ret

; ramclear - the RAM claim's low pages, the fence's targets and the scratch
ramclear:
    push es
    push cx
    push bx
    mov ax, SEG_RAM
    mov es, ax
    xor bx, bx
    mov cx, 0x0C00
.a: mov byte [es:bx], 0
    inc bx
    loop .a
    mov bx, A2_SCR_BASE
    mov cx, 0x100
.b: mov byte [es:bx], 0
    inc bx
    loop .b
    mov byte [es:0xC000], 0
    mov byte [es:0xBFFF], 0
    mov byte [es:0xFFFF], 0
    mov word [es:A2_SCR_BASE + A2_SCR_WLO], 0xFFFF  ; the window starts EMPTY
    mov word [es:A2_SCR_BASE + A2_SCR_WHI], 0
    mov word [es:A2_SCR_BASE + A2_SCR_WATLO], 0x0400  ; ...and the WATCH RANGE
    mov word [es:A2_SCR_BASE + A2_SCR_WATHI], 0x07FF  ;   is the display page
    pop bx
    pop cx
    pop es
    ret

; romfill - the ROM part gets 0x80 + (offset & 0x3F)
romfill:
    push es
    push cx
    push bx
    mov ax, SEG_ROM
    mov es, ax
    xor bx, bx
    mov cx, 0x400
.l: mov al, bl
    and al, 0x3F
    or al, 0x80
    mov [es:bx], al
    inc bx
    loop .l
    pop bx
    pop cx
    pop es
    ret

; chrfill - the decoded character generator: glyph i, row r = (i + r) & 0x7F,
; which is different in every cell and every row, so a packing that shifted by
; the wrong amount cannot come out right by accident
chrfill:
    push cx
    push bx
    push si
    mov si, _a2_chr
    xor bx, bx
.l: mov ax, bx
    mov cl, 3
    shr ax, cl                      ; the glyph index...
    mov cx, bx
    and cx, 7                       ; ...and the pixel row
    add ax, cx
    and al, 0x7F
    mov [si], al
    inc si
    inc bx
    cmp bx, 512
    jb .l
    pop si
    pop bx
    pop cx
    ret

; zcheck - the RAM claim at AX.. holds the 24-byte pattern and the bytes
; either side are 0. CF = 1 on a mismatch.
zcheck:
    push es
    push cx
    push bx
    push dx
    mov bx, ax
    mov dx, ax
    mov ax, SEG_RAM
    mov es, ax
    dec bx
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    add bx, 24
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    xor al, al
    mov cx, 24
.l: cmp [es:bx], al
    jne .bad
    inc bx
    inc al
    loop .l
    clc
    jmp short .out
.bad:
    stc
.out:
    pop dx
    pop bx
    pop cx
    pop es
    ret

; -----------------------------------------------------------------------------
; the routines under test - THE SHIPPING TEXT
; -----------------------------------------------------------------------------
%include "a2cpu.inc"                ; for _a2_m, which a2mem.inc reads the
                                    ; claims from, and for the scratch's
                                    ; layout constants
%include "a2mem.inc"
%include "a2band.inc"

section .text

; a2band.inc reads the decoded character generator out of the C's own storage
; (it is an ordinary global there, so nasm can see the label). Here it is the
; harness's, filled by chrfill.
section .data
_a2_chr: times 512 db 0
section .text

; ...and a2cpu.inc calls OUT to the compiled C for every $C000-$C0FF access in
; BOTH DIRECTIONS (APPLE2-SPEC section 3.2). This harness tests the MOVERS,
; not the core, so the two are stubs - but they have to EXIST, because a `call`
; to an undefined symbol assembles at a different size on the second pass and
; nasm then refuses the whole file with a page of `label changed during code
; generation`. The core's own gate is hosttest/a2cputest.asm, where these two
; answer values the test checks.
_a2_io_rd:
    mov ax, 0x00FF
    ret
_a2_io_wr:
    ret

putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret
puts:
    push ax
.l: mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e: pop ax
    ret
putnum:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.dv:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .dv
.pr:
    pop ax
    add al, '0'
    call putc
    loop .pr
    pop dx
    pop cx
    pop bx
    pop ax
    ret
halt:
    cli
    hlt
    jmp halt

msg_hello: db 'a2memtest: a2mem.inc + a2band.inc on a real x86, SS != DS', 13, 10, 0
msg_nl:    db 13, 10, 0
msg_ok:    db ' failures - a2mem OK', 13, 10, 0
msg_bad:   db ' FAILURES in a2mem', 13, 10, 0

pushes:          dw 0
disc_expect_bad: dw 0
disc_target: dw 0
disc_ret:    dw 0
disc_sp:     dw 0
disc_sp2:    dw 0
disc_bp:     dw 0
disc_bp0:    dw 0
disc_ax:     dw 0
disc_es:     dw 0
disc_ds:     dw 0
disc_fl:     dw 0
sig1:        dw 0
take:        times 36 db 0
             db 0xA5                    ; the guard a2_dirty_take must not
             times 3 db 0xCC            ;   reach: it takes exactly 36 bytes
src:         times 24 db 0
             times 4 db 0xCC
dst:         times 24 db 0
             times 4 db 0xCC
banda:       times 320 db 0
             times 4 db 0xCC
bandb:       times 320 db 0
             times 4 db 0xCC
bandc:       times 1280 db 0
             times 4 db 0xCC
