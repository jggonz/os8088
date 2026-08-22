; =============================================================================
; os8088 - apps/c64/hosttest/c64memtest.asm
;
; A BOOT SECTOR THAT TESTS apps/c64/c64mem.inc AND apps/c64/c64band.inc ON A
; REAL x86 - the two files where this package loads ES and runs a string
; instruction (C64-SPEC §3.6). The host harness cannot reach them (it
; substitutes C), tools/cc8086.py never sees hand-written assembly, and a
; routine that got a segment wrong would write into the kernel and NOT FAULT
; (LESSONS.md 4). So, as apps/runcpm/hosttest/rcmemtest.asm does for the Z80
; movers, this runs the SHIPPING text - %included, not copied - under the one
; condition that makes this OS unlike every other target: SS != DS.
;
; THE RULE IS WIDER THAN THE MOVERS, and that is why c64band.inc is here too:
; C64-SPEC §3.6 says every cross-segment routine is tested this way,
; not only the ones that move C64 memory. c64_rowspan's forward and REVERSE
; `repe cmpsb` set DF on purpose, and a reverse scan that forgot to clear it
; would leave every later string instruction in the task running backwards.
;
; Here CS = DS = 0 stand in for the package (c64mem.inc reads the claims
; through DS, exactly as it does in a package where CS = DS), SS = 0x1000 is
; the task stack, ES = 0x3000 stands in for KERNEL_SEG and must come back
; intact, 0x4000 is the C64's RAM claim and 0x5000 its ROM claim.
;
; WHAT EACH CASE CHECKS, and none of it is the routine's own logic restated:
;
;   1. bytes written through c64_wr read back through c64_rd and through a
;      direct far read of the claim, little-endian through c64_rd16, and
;      c64_rom_rd reads the OTHER claim;
;   2. THE TWO STATED DEVIATIONS OF C64-SPEC §3.5: a write to
;      $FFC0-$FFF9 is dropped, and $FFFA-$FFFF are still real RAM - the
;      vectors a program that banks the KERNAL out puts there;
;   3. the DIRTY BIT and the WRITE WINDOW a write leaves behind (9.2): the
;      right bit of the right byte, the lowest/highest address, and - the row
;      that matters once there is a core - that a write OUTSIDE THE WATCH
;      RANGE sets the dirty bit and the "something was written" byte and does
;      NOT widen the window. Without that, one slice of a running machine
;      (a JSR writes $01xx, a BASIC statement zero page and $0800+) leaves the
;      window spanning the whole matrix and the flush recomposes forty cells
;      of every dirty row, forever;
;   3b. c64_dirty_take, the ONE call a flush now makes for all of that: the
;      32-byte bitmap and the window's two words land in the PACKAGE's memory
;      in the documented slots (dst[0..31], dst[32..35]), the 37th byte is a
;      guard it must not reach, and the scratch is RESET in the same pass -
;      bitmap zeroed, window re-opened to $FFFF/0, ANY cleared. It replaced
;      ~50 near thunk calls a flush, ~1.8 ms on the target (9.2, 9.7);
;   4. every mover leaves the destination equal to the source byte for byte,
;      including a block laid across the 64KB wrap, and touches nothing
;      outside it (a guard byte each side);
;   5a. c64band.inc's CROSS-SEGMENT pair, which C64-SPEC §3.6's rule is actually about:
;      c64_band1 composing three cells out of a matrix row in the RAM claim
;      and a character generator in the ROM claim - one cell per branch of the
;      luminance mask, including the uniform one an XOR cannot express - and
;      c64_rowsig answering a different signature when one byte of the claim,
;      or one nibble of the near colour row, moves;
;   5. c64band.inc's compare and copy: c64_rowspan answers "same" on equal
;      rows and the exact first/last differing CELL on unequal ones - over all
;      eight pixel rows, which is the case a single-row test would miss - and
;      c64_rowcopy makes them equal again;
;   6. after every call ES, DS, BP and SP are what they were and DF IS CLEAR;
;   7. NEGATIVE CONTROLS: two deliberately broken routines - one that leaves
;      ES on the claim, one that returns with DF set - go through the same
;      discipline check, which must FAIL them. A harness that cannot see a
;      broken routine has proved nothing.
;
; RUN IT:  apps/c64/hosttest/c64memtest.sh
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
    mov word [_c64_m+CM_RAMSEG], SEG_RAM
    mov word [_c64_m+CM_ROMSEG], SEG_ROM

    mov si, msg_hello
    call puts

    call ramclear
    call romfill

    ; --- (1) the accessors ---------------------------------------------------
    mov word [pushes], 2
    PUSHI 0xA5
    PUSHI 0x1234
    call disc_call
    dw _c64_wr
    PUSHI 0x5A
    PUSHI 0x2000
    call disc_call
    dw _c64_wr
    PUSHI 0xC3
    PUSHI 0x2001
    call disc_call
    dw _c64_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x1234], 0xA5
    jne .f1
    pop es
    mov word [pushes], 1
    PUSHI 0x1234
    call disc_call
    dw _c64_rd
    cmp ax, 0x00A5                  ; AH must be 0 (LESSONS.md 4)
    jne .f1b
    PUSHI 0x2000
    call disc_call
    dw _c64_rd16
    cmp ax, 0xC35A                  ; little-endian
    jne .f1b
    ; c64_rom_rd reads the OTHER claim: romfill put 0x80 + (off & 0x3F) there
    PUSHI 0x0005
    call disc_call
    dw _c64_rom_rd
    cmp ax, 0x0085
    jne .f1b
    mov al, '.'
    call putc
    jmp .scratch
.f1:
    pop es
.f1b:
    mov al, '1'
    call putc
    inc bp

    ; --- (2) the two stated deviations of 3.5 --------------------------------
.scratch:
    mov word [pushes], 2
    PUSHI 0x77
    PUSHI 0xFFC0                    ; the scratch: DROPPED
    call disc_call
    dw _c64_wr
    PUSHI 0x77
    PUSHI 0xFFF9                    ; the last scratch byte: DROPPED
    call disc_call
    dw _c64_wr
    PUSHI 0x77
    PUSHI 0xFFFA                    ; the NMI vector: real RAM
    call disc_call
    dw _c64_wr
    PUSHI 0x77
    PUSHI 0xFFBF                    ; one below the scratch: real RAM
    call disc_call
    dw _c64_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0xFFC0], 0x77
    je .f2e
    cmp byte [es:0xFFF9], 0x77
    je .f2e
    cmp byte [es:0xFFFA], 0x77
    jne .f2e
    cmp byte [es:0xFFBF], 0x77
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

    ; --- (3) the dirty bit and the write window (9.2) ------------------------
.dirty:
    call ramclear
    mov word [pushes], 2
    PUSHI 0x11
    PUSHI 0x0512                    ; page 5 -> bitmap byte 0, bit 0x04.
    call disc_call                  ;   INSIDE the watch range
    dw _c64_wr
    PUSHI 0x22
    PUSHI 0x8A00                    ; page 0x8A -> byte 17, bit 0x20.
    call disc_call                  ;   OUTSIDE it: the bit is set, the
    dw _c64_wr                      ;   window must NOT move
    PUSHI 0x33
    PUSHI 0x0140                    ; the stack page - what a JSR writes, and
    call disc_call                  ;   the exact case that used to drag the
    dw _c64_wr                      ;   window's low end down to $0140
    PUSHI 0x44
    PUSHI 0x0400                    ; INSIDE, and below $0512: the low end
    call disc_call                  ;   moves for this one
    dw _c64_wr
    push es
    mov ax, SEG_RAM
    mov es, ax
    ; bitmap byte 0 covers pages 0-7, page p being bit 0x80 >> p: page 1
    ; ($0140) 0x40, page 4 ($0400) 0x08, page 5 ($0512) 0x04 = 0x4C. EVERY
    ; write sets its bit, whether or not it is in the watch range - the
    ; bitmap is what sees a RAM charset and wave 4's bitmap and sprite data.
    cmp byte [es:0xFFC0 + 0], 0x4C
    jne .f3e
    cmp byte [es:0xFFC0 + 17], 0x20         ; ...and page 0x8A, out of range
    jne .f3e
    cmp byte [es:0xFFC0 + 0x30], 1          ; "something was written", for a
    jne .f3e                                ;   write outside the range too
    cmp word [es:0xFFC0 + 0x2C], 0x0400     ; the lowest written IN RANGE
    jne .f3e
    cmp word [es:0xFFC0 + 0x2E], 0x0512     ; ...and the highest IN RANGE, and
    jne .f3e                                ;   NOT $8A00 or $0140
    pop es
    mov al, '.'
    call putc
    jmp .take
.f3e:
    pop es
    mov al, '3'
    call putc
    inc bp

    ; --- (3b) c64_dirty_take: the whole flush read, in ONE call --------------
    ; The scratch state case 3 just left behind is the fixture: bitmap byte 0
    ; = 0x4C, byte 17 = 0x20, the window $0400..$0512, ANY = 1. The routine
    ; must bring all of that into the PACKAGE's memory - which is a different
    ; segment from the claim, which is the whole reason it is in this file -
    ; and RESET the scratch in the same pass, or the next flush recomposes
    ; what this one already drew.
.take:
    mov word [pushes], 1
    PUSHI take
    call disc_call
    dw _c64_dirty_take
    cmp byte [take + 0], 0x4C               ; the bitmap came across...
    jne .f3be
    cmp byte [take + 17], 0x20
    jne .f3be
    cmp word [take + 32], 0x0400            ; ...and the window's two words,
    jne .f3be                               ;   in the documented slots
    cmp word [take + 34], 0x0512
    jne .f3be
    cmp byte [take + 36], 0xA5              ; ...and NOT one byte past 36
    jne .f3be
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0xFFC0 + 0], 0             ; the scratch is RESET by the same
    jne .f3bp                               ;   pass: bitmap zeroed...
    cmp byte [es:0xFFC0 + 17], 0
    jne .f3bp
    cmp word [es:0xFFC0 + 0x2C], 0xFFFF     ; ...the window re-opened...
    jne .f3bp
    cmp word [es:0xFFC0 + 0x2E], 0
    jne .f3bp
    cmp byte [es:0xFFC0 + 0x30], 0          ; ...and ANY cleared, because this
    jne .f3bp                               ;   IS the read of it
    pop es
    mov al, '.'
    call putc
    jmp .movers
.f3bp:
    pop es
.f3be:
    mov al, 'K'                     ; 'T' is the totals marker below
    call putc
    inc bp

    ; --- (4) the movers ------------------------------------------------------
.movers:
    mov cx, 24
    mov bx, src
    mov al, 0x11
.fill:
    mov [bx], al
    add al, 0x37
    inc bx
    loop .fill

    call ramclear
    mov word [pushes], 3
    PUSHI 24
    PUSHI src
    PUSHI 0x0100
    call disc_call
    dw _c64_zcopy_in
    mov ax, 0x0100
    call zcheck
    jc .f4
    call dclear
    PUSHI 24
    PUSHI 0x0100
    PUSHI dst
    call disc_call
    dw _c64_zcopy_out
    call dcheck
    jc .f4
    ; claim to claim: the ROM claim's 0x0100.. into the RAM claim at 0x0300
    mov word [pushes], 4
    PUSHI 24
    PUSHI 0x0100
    PUSHI SEG_ROM
    PUSHI 0x0300
    call disc_call
    dw _c64_zzcopy_in
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0300], 0x80              ; romfill's pattern at 0x0100
    jne .f4e
    cmp byte [es:0x0317], 0x97
    jne .f4e
    cmp byte [es:0x0318], 0                 ; one past: untouched
    jne .f4e
    pop es
    ; c64_zfill, and n = 0 moving nothing
    mov word [pushes], 3
    PUSHI 24
    PUSHI 0xEE
    PUSHI 0x0400
    call disc_call
    dw _c64_zfill
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0400], 0xEE
    jne .f4e
    cmp byte [es:0x0417], 0xEE
    jne .f4e
    cmp byte [es:0x0418], 0
    jne .f4e
    pop es
    PUSHI 0
    PUSHI src
    PUSHI 0x0500
    call disc_call
    dw _c64_zcopy_in
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0500], 0
    jne .f4e
    pop es
    ; --- (4b) c64_copy_row: Edit > Copy's per-BYTE loop (C64-SPEC 7.7) ------
    ; The table index with the reverse-video bit masked off (charset.c:204),
    ; last_non_whitespace (clipboard.c:67), the trim (:81-85) and one '\n' -
    ; all of it in assembly, because a per-byte loop in C is what
    ; docs/C-TOOLCHAIN.md refuses. The row is composed into ANOTHER SEGMENT,
    ; so this exercises the ES load and its restoration too, and the answer is
    ; a LENGTH: the bytes past it are scratch, because the trim winds DI back
    ; over the trailing spaces rather than erasing them - which is exactly
    ; what clipboard.c:81-85 does with its index.
    mov byte [_c64_sctab+0], ' '            ; screen code 0 is a space...
    mov byte [_c64_sctab+1], 'X'
    mov byte [_c64_sctab+2], 'Y'
    mov cx, 40                              ; a row of spaces...
    mov bx, _c64_scrow
.crfill:
    mov byte [bx], 0
    inc bx
    loop .crfill
    mov byte [_c64_scrow+0], 1              ; ...with X Y X at the front and
    mov byte [_c64_scrow+1], 2              ; 37 trailing spaces to trim. The
    mov byte [_c64_scrow+2], 0x81           ; third cell carries the REVERSE
                                            ; bit and must still read as 'X'
    call ramclear
    mov word [pushes], 3
    PUSHI 40
    PUSHI 0x0500
    PUSHI SEG_RAM
    call disc_call
    dw _c64_copy_row
    cmp word [disc_ax], 4                   ; XYX + the line ending
    jne .f4b
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0500], 'X'
    jne .f4be
    cmp byte [es:0x0501], 'Y'
    jne .f4be
    cmp byte [es:0x0502], 'X'
    jne .f4be
    cmp byte [es:0x0503], 0x0A
    jne .f4be
    cmp byte [es:0x0504], ' '               ; ...and the scratch past the
    jne .f4be                               ; answer is the trimmed spaces
    pop es
    ; ...and a row whose LAST cell is not a space keeps all 40
    mov byte [_c64_scrow+39], 2
    call ramclear
    mov word [pushes], 3
    PUSHI 40
    PUSHI 0x0500
    PUSHI SEG_RAM
    call disc_call
    dw _c64_copy_row
    cmp word [disc_ax], 41
    jne .f4b
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0500+38], ' '            ; nothing was trimmed
    jne .f4be
    cmp byte [es:0x0500+39], 'Y'
    jne .f4be
    cmp byte [es:0x0500+40], 0x0A
    jne .f4be
    pop es
    ; ...and a row that is ALL spaces is one line ending and nothing else
    mov byte [_c64_scrow+0], 0
    mov byte [_c64_scrow+1], 0
    mov byte [_c64_scrow+2], 0
    mov byte [_c64_scrow+39], 0
    call ramclear
    mov word [pushes], 3
    PUSHI 40
    PUSHI 0x0500
    PUSHI SEG_RAM
    call disc_call
    dw _c64_copy_row
    cmp word [disc_ax], 1
    jne .f4b
    push es
    mov ax, SEG_RAM
    mov es, ax
    cmp byte [es:0x0500], 0x0A
    jne .f4be
    pop es
    ; ...and n = 0 writes the line ending and nothing before it
    call ramclear
    mov word [pushes], 3
    PUSHI 0
    PUSHI 0x0500
    PUSHI SEG_RAM
    call disc_call
    dw _c64_copy_row
    cmp word [disc_ax], 1
    jne .f4b
    mov al, '.'
    call putc
    jmp .band
.f4be:
    pop es
.f4b:
    mov al, 'C'
    call putc
    inc bp
    jmp .band
.f4e:
    pop es
.f4:
    mov al, '4'
    call putc
    inc bp

    ; --- (5) c64band.inc's compare and copy ----------------------------------
.band:
    call bandfill                   ; banda == bandb, 8 rows of 40
    mov word [pushes], 3
    PUSHI 40
    PUSHI bandb
    PUSHI banda
    call disc_call
    dw _c64_rowspan
    cmp ax, -1                      ; identical rows answer "same"
    jne .f5
    ; a difference at cell 5 of pixel row 3 and at cell 30 of pixel row 6:
    ; the span is the union over ALL EIGHT rows, which is the case a
    ; single-row compare would get wrong
    mov byte [banda + 3*40 + 5], 0x99
    mov byte [banda + 6*40 + 30], 0x99
    PUSHI 40
    PUSHI bandb
    PUSHI banda
    call disc_call
    dw _c64_rowspan
    cmp ax, 0x051E                  ; (first 5 << 8) | last 30
    jne .f5
    ; ...and rowcopy makes them equal again, touching nothing outside the span
    mov byte [bandb - 1], 0xCC
    mov word [pushes], 3
    PUSHI 26
    mov ax, banda + 5
    push ax
    mov ax, bandb + 5
    push ax
    call disc_call
    dw _c64_rowcopy
    cmp byte [bandb - 1], 0xCC
    jne .f5
    PUSHI 40
    PUSHI bandb
    PUSHI banda
    call disc_call
    dw _c64_rowspan
    cmp ax, -1
    jne .f5
    mov al, '.'
    call putc
    jmp .band1
.f5:
    mov al, '5'
    call putc
    inc bp

    ; --- (6) c64band.inc's CROSS-SEGMENT pair: the composer and the
    ;         signature. C64-SPEC §3.6 says every cross-segment entry point is
    ;         exercised here with SS != DS and an ES sentinel, and these two
    ;         are the ones that read a CLAIM: c64_band1 takes the matrix
    ;         through mseg:moff and the character generator through gseg:goff,
    ;         c64_rowsig takes the matrix the same way. Neither was called at
    ;         all until this case existed - the gate tested only the two
    ;         near-only routines beside them.
.band1:
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0400], 1         ; three screen codes in the RAM claim
    mov byte [es:0x0401], 2
    mov byte [es:0x0402], 3
    pop es
    mov byte [colrow+0], 0          ; ...and three colours, near, one per
    mov byte [colrow+1], 1          ; branch of the luminance mask
    mov byte [colrow+2], 2
    mov word [_c64_cmask+0], 0x00FF ; {and 0xFF, xor 0x00} - the glyph as it is
    mov word [_c64_cmask+2], 0xFFFF ; {and 0xFF, xor 0xFF} - inverted
    mov word [_c64_cmask+4], 0xFF00 ; {and 0x00, xor 0xFF} - a uniform cell
    mov cx, 320                     ; the band, cleared, with a guard cell
    mov di, banda
    xor al, al
.b1z:
    mov [di], al
    inc di
    loop .b1z
    mov byte [banda+3], 0xCC
    mov word [pushes], 10
    PUSHI 0                         ; bg
    PUSHI 0                         ; mode - standard text
    PUSHI 0                         ; goff - the ROM claim's own base, which
    PUSHI SEG_ROM                   ; gseg    romfill made 0x80 + (off & 0x3F)
    PUSHI colrow                    ; col
    PUSHI 0x0400                    ; moff
    PUSHI SEG_RAM                   ; mseg
    PUSHI 2                         ; last
    PUSHI 0                         ; first
    PUSHI banda                     ; dst
    call disc_call
    dw _c64_band1
    ; glyph(code c, line k) is rom[c * 8 + k] = 0x80 + ((c * 8 + k) & 0x3F),
    ; so code 1 gives 0x88..0x8F down the cell and code 2 gives 0x90..0x97
    mov cx, 8
    xor di, di
    mov bl, 0x88
.b1c:
    mov al, [banda+di]              ; cell 0: the glyph, as it stands
    cmp al, bl
    jne .f6
    mov ah, bl                      ; cell 1: the same glyph eight codes on,
    add ah, 8                       ;         INVERTED
    not ah
    mov al, [banda+di+1]
    cmp al, ah
    jne .f6
    cmp byte [banda+di+2], 0xFF     ; cell 2: uniform, which an XOR alone
    jne .f6                         ;         cannot say
    inc bl
    add di, 40
    loop .b1c
    cmp byte [banda+3], 0xCC        ; ...and nothing outside the span
    jne .f6

    ; c64_rowsig, over the same matrix row in the same claim
    mov word [pushes], 4
    PUSHI 40
    PUSHI colrow
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _c64_rowsig
    mov [sig1], ax
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0405], 0x5A      ; one byte of the CLAIM moves...
    pop es
    PUSHI 40
    PUSHI colrow
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _c64_rowsig
    cmp ax, [sig1]
    je .f6                          ; ...so the signature must move with it -
                                    ; if it does not, it is not reading the
                                    ; claim at all
    push es
    mov ax, SEG_RAM
    mov es, ax
    mov byte [es:0x0405], 0
    pop es
    mov byte [colrow+5], 3          ; ...and the COLOUR row is in it too
    PUSHI 40
    PUSHI colrow
    PUSHI 0x0400
    PUSHI SEG_RAM
    call disc_call
    dw _c64_rowsig
    cmp ax, [sig1]
    je .f6
    mov byte [colrow+5], 0
    mov al, '.'
    call putc
    jmp .x2
.f6:
    mov al, '6'
    call putc
    inc bp

    ; --- (6b) c64band.inc's PIXEL DOUBLER (C64-SPEC 9.8's 2x) ----------------
    ; NOTHING CALLED EITHER OF THESE UNTIL FULLSCREEN 2x SHIPPED, and both had
    ; been in the tree for two waves: c64_x2init answered a table of 256 ZEROS
    ; (`shl bx, 1` reads bit 15, and the byte was in the LOW half; and the pair
    ; was set BETWEEN the two result shifts, so it walked out of the sixteen
    ; bits). On the glass that is a fullscreen picture that is entirely black,
    ; and the host harness could not see it because it models the doubler in C
    ; rather than running this - LESSONS.md 7's trap. So it is run here.
.x2:
    mov word [pushes], 0
    call disc_call
    dw _c64_x2init
    ; four entries by hand: 0 doubles to nothing, $FF to everything, and the
    ; two single-bit ends prove which way round the bits go
    cmp word [c64_x2tab+0*2], 0x0000
    jne .f6b
    cmp byte [c64_x2tab+0xFF*2], 0xFF
    jne .f6b
    cmp byte [c64_x2tab+0xFF*2+1], 0xFF
    jne .f6b
    cmp byte [c64_x2tab+0x80*2], 0xC0       ; the LEFTMOST source bit becomes
    jne .f6b                                ; the leftmost PAIR
    cmp byte [c64_x2tab+0x80*2+1], 0x00
    jne .f6b
    cmp byte [c64_x2tab+0x01*2], 0x00       ; ...and the rightmost the
    jne .f6b                                ; rightmost pair
    cmp byte [c64_x2tab+0x01*2+1], 0x03
    jne .f6b
    cmp byte [c64_x2tab+0x55*2], 0x33       ; an alternating byte, both halves
    jne .f6b
    cmp byte [c64_x2tab+0x55*2+1], 0x33
    jne .f6b
    ; ...AND EVERY BYTE OF THE TABLE IS A DOUBLED NIBBLE. There are only 16
    ; values a doubled nibble can take and every one has its bit pairs equal,
    ; so `(b & 0xAA) >> 1 == (b & 0x55)` holds for all 512 bytes and for
    ; nothing a wrong loop is likely to produce. A single hand-checked entry
    ; would not have caught the all-zero table either, because 0 passes it.
    mov si, c64_x2tab
    mov cx, 512
.x2chk:
    mov al, [si]
    mov ah, al
    and al, 0xAA
    shr al, 1
    and ah, 0x55
    cmp al, ah
    jne .f6b
    inc si
    loop .x2chk

    ; c64_band_x2: one source row of C64_BSTRIDE bytes becomes TWO rows of
    ; C64_X2STRIDE, and the second is a copy of the first - which is what lets
    ; the X-ONLY tier read the same buffer at twice the stride (9.8).
    mov cx, 320                     ; a source band with a known first row
    mov di, banda
    xor al, al
.x2fill:
    mov [di], al
    inc di
    loop .x2fill
    mov byte [banda+0], 0x80
    mov byte [banda+1], 0x01
    mov byte [banda+2], 0x55
    mov cx, 240                     ; ...and the destination, cleared: THREE
    mov di, bandb                   ; doubled rows, so that "rows = 1 wrote no
                                    ; third" is a real question and the clear
                                    ; itself stays inside bandb's 320 bytes
    xor al, al
.x2clr:
    mov [di], al
    inc di
    loop .x2clr
    mov word [pushes], 3
    PUSHI 1                         ; rows = 1
    PUSHI banda
    PUSHI bandb
    call disc_call
    dw _c64_band_x2
    cmp byte [bandb+0], 0xC0
    jne .f6b
    cmp byte [bandb+1], 0x00
    jne .f6b
    cmp byte [bandb+2], 0x00
    jne .f6b
    cmp byte [bandb+3], 0x03
    jne .f6b
    cmp byte [bandb+4], 0x33
    jne .f6b
    cmp byte [bandb+5], 0x33
    jne .f6b
    cmp byte [bandb+C64_X2STRIDE+0], 0xC0   ; the duplicated scan line
    jne .f6b
    cmp byte [bandb+C64_X2STRIDE+5], 0x33
    jne .f6b
    cmp byte [bandb+C64_X2STRIDE*2], 0      ; ...and rows = 1 wrote no third
    jne .f6b
    mov al, '.'
    call putc
    jmp .neg
.f6b:
    mov al, 'D'
    call putc
    inc bp

    ; --- (7) the negative controls -------------------------------------------
.neg:
    mov word [pushes], 1
    PUSHI 0
    mov word [disc_expect_bad], 1
    call disc_call
    dw bad_es
    PUSHI 0
    call disc_call
    dw bad_df
    PUSHI 0
    call disc_call                  ; ...and the two the checker could not see
    dw bad_bp                       ; at all until it recorded BP BEFORE the
    PUSHI 0                         ; call and stopped doing its own
    call disc_call                  ; bookkeeping through a DS the routine
    dw bad_ds                       ; under test had just moved
    mov word [disc_expect_bad], 0

    mov al, 'T'
    call putc
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
; disc_call - call the routine whose address follows the call, with the
; arguments already pushed ([pushes] words), and check the discipline after:
; ES, DS, BP, SP back and DF CLEAR. The arguments are popped here and the
; routine's AX handed back. '.' on a pass, 'X' on a failure; for a negative
; control, 'N' when it is caught and 'x' when it slipped through.
; -----------------------------------------------------------------------------
disc_call:
    pop bx
    mov ax, [bx]
    mov [disc_target], ax
    add bx, 2
    mov [disc_ret], bx
    mov [disc_sp], sp
    mov [disc_bp0], bp              ; BP AS IT IS **BEFORE** THE CALL, which is
                                    ; the whole point of the check and what the
                                    ; first draft did not record: it stored BP
                                    ; AFTER the call and then compared that
                                    ; word with the same live BP, so the test
                                    ; was `bp == bp` and no routine could ever
                                    ; fail it
    call [disc_target]
    ; --- FROM HERE DS MAY BE ANYTHING. A routine that returned with DS on a
    ;     claim would make every store below land in the claim instead of in
    ;     this program - the checker's own bookkeeping corrupted by the very
    ;     fault it exists to catch, and a `.` printed for it. CS = DS in this
    ;     harness (both 0), so a CS override reaches the same bytes whatever DS
    ;     now holds. Every access is overridden until DS has been recorded,
    ;     checked and put back.
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
    mov bp, [disc_bp0]              ; ...and the checker PUTS BACK the thing it
                                    ; just measured: BP is this program's own
                                    ; failure counter, so a routine that
                                    ; returns with BP wrong (bad_bp) would
                                    ; otherwise corrupt the tally it is being
                                    ; caught by
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
; one per thing disc_call claims to check. A harness that cannot see a broken
; routine has proved nothing, and bad_bp and bad_ds are here because the two
; checks they break were both inoperative: BP was compared with itself, and
; the checker's own stores went through whatever DS the routine left behind.
bad_es:
    push bp
    mov bp, sp
    mov es, [_c64_m+CM_RAMSEG]
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

; ramclear - the RAM claim's first 0x600 bytes and its scratch to zero
ramclear:
    push es
    push cx
    push bx
    mov ax, SEG_RAM
    mov es, ax
    xor bx, bx
    mov cx, 0x600
.a: mov byte [es:bx], 0
    inc bx
    loop .a
    mov bx, 0xFF00
    mov cx, 0x100
.b: mov byte [es:bx], 0
    inc bx
    loop .b
    mov word [es:0xFFC0 + 0x2C], 0xFFFF     ; the write window starts EMPTY
    mov word [es:0xFFC0 + 0x2E], 0
    mov word [es:0xFFC0 + 0x32], 0x0400     ; ...and the WATCH RANGE is the
    mov word [es:0xFFC0 + 0x34], 0x07E7     ;   screen matrix (9.2)
    pop bx
    pop cx
    pop es
    ret

; romfill - the ROM claim gets 0x80 + (offset & 0x3F)
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

; zcheck - the RAM claim at AX.. holds the 24-byte pattern and the bytes
; either side are 0. CF = 1 on a mismatch.
zcheck:
    push es
    push cx
    push bx
    push dx
    mov dx, ax
    mov ax, SEG_RAM
    mov es, ax
    mov bx, dx
    dec bx
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    add bx, 24
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    mov cx, 24
    mov al, 0x11
.l: cmp [es:bx], al
    jne .bad
    add al, 0x37
    inc bx
    loop .l
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop bx
    pop cx
    pop es
    ret

dclear:
    push cx
    push bx
    mov bx, dst - 4
    mov cx, 32
.l: mov byte [bx], 0xCC
    inc bx
    loop .l
    pop bx
    pop cx
    ret

dcheck:
    push cx
    push bx
    mov bx, dst
    mov cx, 24
    mov al, 0x11
.l: cmp [bx], al
    jne .bad
    add al, 0x37
    inc bx
    loop .l
    cmp byte [dst - 1], 0xCC
    jne .bad
    cmp byte [dst + 24], 0xCC
    jne .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop bx
    pop cx
    ret

; bandfill - two identical 8 x 40 bands, with a guard below the second
bandfill:
    push cx
    push bx
    mov bx, 0
    mov cx, 320
    mov al, 0x40
.l: mov [banda + bx], al
    mov [bandb + bx], al
    add al, 0x1D
    inc bx
    loop .l
    mov byte [bandb - 1], 0xCC
    pop bx
    pop cx
    ret

; -----------------------------------------------------------------------------
; the routines under test - THE SHIPPING TEXT
; -----------------------------------------------------------------------------
%include "c64cpu.inc"               ; for _c64_m, which c64mem.inc reads the
                                    ; claims from
%include "c64mem.inc"
%include "c64band.inc"

section .text

; c64band.inc reads these two out of the C's own storage (they are ordinary
; globals there, so nasm can see the label). Here they stand in for it.
section .data
_c64_cmask: times 32 db 0xFF
_c64_bgfill: db 0
; Edit > Copy's two package globals (c64kbd.c bss on the target). The real
; c64_sctab is built by ovl_conv_init from VICE's three transcribed
; conversions; here it is whatever the case below writes, because what is
; under test is c64_copy_row's LOOP - the mask, the index, the store into
; somebody else's segment, last_non_whitespace and the trim.
_c64_scrow: times 40 db 0
            times 4 db 0xCC
_c64_sctab: times 128 db 0
section .text

; ...and c64cpu.inc calls OUT to the compiled C for every $D000-$DFFF access
; and for $0000/$0001 (C64-SPEC §3.4). This harness tests the MOVERS,
; not the core, so the two are stubs - but they have to EXIST, because a
; `call` to an undefined symbol assembles at a different size on the second
; pass and nasm then refuses the whole file. The core's own gate is
; hosttest/c64cputest.asm, where these two answer values the test checks.
_c64_io_rd:
    mov ax, 0x00FF
    ret
_c64_io_wr:
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

msg_hello: db 'c64memtest: c64mem.inc + c64band.inc on a real x86, SS != DS', 13, 10, 0
msg_ok:    db ' failures - c64mem OK', 13, 10, 0
msg_bad:   db ' FAILURES in c64mem', 13, 10, 0

pushes:          dw 0
disc_expect_bad: dw 0
disc_target: dw 0
disc_ret:    dw 0
disc_sp:     dw 0
disc_sp2:    dw 0
disc_bp:     dw 0
disc_bp0:    dw 0
sig1:        dw 0
colrow:      times 40 db 0
             times 4 db 0xCC
disc_ax:     dw 0
disc_es:     dw 0
disc_ds:     dw 0
disc_fl:     dw 0
take:        times 36 db 0
             db 0xA5                    ; the guard c64_dirty_take must not
             times 3 db 0xCC            ;   reach: it takes exactly 36 bytes
src:         times 24 db 0
             times 4 db 0xCC
dst:         times 24 db 0
             times 4 db 0xCC
             times 4 db 0xCC
banda:       times 320 db 0
             times 4 db 0xCC
bandb:       times 320 db 0
             times 4 db 0xCC
