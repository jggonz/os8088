; =============================================================================
; os8088 - apps/infones/hosttest/nimemtest.asm
;
; A BOOT SECTOR THAT RUNS apps/infones/nimem.inc AND apps/infones/niband.inc ON
; A REAL x86 - the two files where this package loads ES and runs a string
; instruction (SPEC.md 91.14.4). The host harness cannot reach them (it
; substitutes C), tools/cc8086.py never sees hand-written assembly, and a
; routine that got a segment wrong would write into the KERNEL and not fault
; (LESSONS.md 4). So this runs the SHIPPING text - %included, never copied -
; under the one condition that makes this OS unlike every other target this
; code could be tested on: **SS != DS**.
;
; Here CS = DS = 0 stand in for the package's segment (nimem.inc reaches its
; statics through DS, exactly as it does in a package where CS = DS), SS =
; 0x1000 is the task stack, ES = 0x3000 stands in for KERNEL_SEG and MUST come
; back intact, 0x4000 is the machine claim, 0x5000 the CHR claim and 0x6000
; the tile cache.
;
; WHAT EACH CASE CHECKS, and none of it is the routine's own logic restated:
;
;   1. ni_move copies exactly n bytes between two claims and touches nothing
;      either side of them - a guard byte before and after, both checked;
;   2. ni_fill fills exactly n bytes with the byte it was given, guards again;
;   3. ni_pokew / ni_peekw round-trip a LITTLE-ENDIAN word in a claim, which
;      is what the core's PRG window table is made of;
;   4. ni_oam_move puts 256 bytes of the CPU RAM claim into OAM at the
;      documented offset, both halves inside the ONE machine claim - the case
;      where a mover's two segments are the same segment;
;   5. ni_chr_decode turns a 1KB CHR bank into 4,096 bytes of one byte a pixel,
;      checked against HAND-COMPUTED tiles at BOTH ENDS of the bank (tile 0
;      row 0 and tile 63 row 7, which is what catches a loop bound), plus a
;      guard at byte 4,096 - the decoder writing one byte past its claim is
;      exactly the defect no screendump shows;
;   6. after every call ES, DS, BP and SP are what they were AND DF IS CLEAR;
;   7. NEGATIVE CONTROLS: two deliberately broken routines - one that returns
;      with ES left on a claim, one that returns with DF set - go through the
;      same discipline check, which must FAIL both. A harness that cannot see
;      a broken routine has proved nothing (LESSONS.md 2).
;
; RUN IT:  apps/infones/hosttest/nimemtest.sh   (or `make nimemtest`)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": it must come back
SEG_MACH    equ 0x4000              ; the machine claim
SEG_CHR     equ 0x5000              ; the CHR claim
SEG_CACHE   equ 0x6000              ; the 32KB tile cache
SEG_FB      equ 0x8000              ; ...and a stand-in for mode 13h's
                                    ; framebuffer. 0x8000 is 512KB, above
                                    ; every claim above and below the 640KB
                                    ; line - a first draft put it at 0xE000,
                                    ; which is BIOS ROM: every store was
                                    ; DROPPED and the case failed with the
                                    ; guard bytes intact everywhere
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
    call rdsec
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
lba: dw 0

rdsec:
    push ax
    push cx
    push dx
    mov ax, [lba]
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
    pop dx
    pop cx
    pop ax
    ret
.err:
    mov al, 'D'
    call putc
    jmp halt

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
    mov ds, ax                      ; DS = CS = 0: "the package"
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld

    mov si, msg_hello
    call puts

    mov word [_ni_m + NIM_MACHSEG], SEG_MACH

; --- case 1: ni_move ---------------------------------------------------------
    call disc_arm
    ; fill the source with a walking pattern and put guards round the dest
    mov ax, SEG_CHR
    mov es, ax
    mov di, 0x0100
    mov cx, 300
    mov al, 1
.fill1:
    mov [es:di], al
    inc di
    inc al
    loop .fill1
    mov ax, SEG_CACHE
    mov es, ax
    mov byte [es:0x01FF], 0xAA      ; the guard BEFORE
    mov byte [es:0x0200 + 300], 0xBB ; ...and after
    mov ax, SEG_KERNEL
    mov es, ax
    PUSHI 300
    PUSHI 0x0100
    PUSHI SEG_CHR
    PUSHI 0x0200
    PUSHI SEG_CACHE
    call _ni_move
    add sp, 10
    call disc_check
    ; every byte, and both guards
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0x0200
    mov cx, 300
    mov al, 1
.cmp1:
    cmp [si], al
    jne .bad1
    inc si
    inc al
    loop .cmp1
    cmp byte [0x01FF], 0xAA
    jne .bad1
    cmp byte [0x0200 + 300], 0xBB
    jne .bad1
    pop ds
    call ok
    jmp c2
.bad1:
    pop ds
    mov si, msg_move
    call bad

; --- case 2: ni_fill ---------------------------------------------------------
c2:
    call disc_arm
    mov ax, SEG_CACHE
    mov es, ax
    mov byte [es:0x0FFF], 0xAA
    mov byte [es:0x1000 + 64], 0xBB
    mov ax, SEG_KERNEL
    mov es, ax
    PUSHI 64
    PUSHI 0x5A
    PUSHI 0x1000
    PUSHI SEG_CACHE
    call _ni_fill
    add sp, 8
    call disc_check
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0x1000
    mov cx, 64
.cmp2:
    cmp byte [si], 0x5A
    jne .bad2
    inc si
    loop .cmp2
    cmp byte [0x0FFF], 0xAA
    jne .bad2
    cmp byte [0x1000 + 64], 0xBB
    jne .bad2
    pop ds
    call ok
    jmp c3
.bad2:
    pop ds
    mov si, msg_fill
    call bad

; --- case 3: ni_pokew / ni_peekw --------------------------------------------
c3:
    call disc_arm
    PUSHI 0x1234
    PUSHI 0x2000
    PUSHI SEG_MACH
    call _ni_pokew
    add sp, 6
    call disc_check
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    cmp byte [0x2000], 0x34         ; LITTLE-ENDIAN, and the byte order is the
    jne .bad3                       ; whole of what this case is about
    cmp byte [0x2001], 0x12
    jne .bad3
    pop ds
    call disc_arm
    PUSHI 0x2000
    PUSHI SEG_MACH
    call _ni_peekw
    add sp, 4
    cmp ax, 0x1234
    jne .bad3b
    call disc_check
    call ok
    jmp c4
.bad3:
    pop ds
.bad3b:
    mov si, msg_word
    call bad

; --- case 4: ni_oam_move, whose two segments are ONE segment -----------------
c4:
    call disc_arm
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov si, 0x0300                  ; a source inside the 2KB CPU RAM
    mov cx, 256
    mov al, 0x40
.f4:
    mov [si], al
    inc si
    inc al
    loop .f4
    mov byte [0x30FF], 0            ; wipe OAM's first and last...
    mov byte [0x3000], 0
    mov byte [0x3100], 0xCC         ; ...and guard the byte after it
    pop ds
    PUSHI 0x0300
    call _ni_oam_move
    add sp, 2
    call disc_check
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    cmp byte [0x3000], 0x40
    jne .bad4
    cmp byte [0x30FF], 0x3F         ; 0x40 + 255, wrapped in a byte
    jne .bad4
    cmp byte [0x3100], 0xCC
    jne .bad4
    pop ds
    call ok
    jmp c5
.bad4:
    pop ds
    mov si, msg_oam
    call bad

; --- case 5: ni_chr_decode, against HAND-COMPUTED tiles ----------------------
; tile 0, row 0:  plane0 = 10110001, plane1 = 11000011
;                 -> 3, 2, 1, 1, 0, 0, 2, 3
; tile 63, row 7: plane0 = 11111111, plane1 = 00001111
;                 -> 1, 1, 1, 1, 3, 3, 3, 3
c5:
    call disc_arm
    push ds
    mov ax, SEG_CHR
    mov ds, ax
    xor si, si                      ; the bank starts clean
    mov cx, 1024
    xor al, al
.z5:
    mov [si], al
    inc si
    loop .z5
    mov byte [0x0000], 0xB1         ; tile 0 plane 0 row 0
    mov byte [0x0008], 0xC3         ; tile 0 plane 1 row 0
    mov byte [1008 + 7], 0xFF       ; tile 63 plane 0 row 7
    mov byte [1008 + 15], 0x0F      ; tile 63 plane 1 row 7
    mov ax, SEG_CACHE
    mov ds, ax
    mov byte [4096], 0xE5           ; THE GUARD one byte past the output
    pop ds
    PUSHI 0
    PUSHI SEG_CHR
    PUSHI 0
    PUSHI SEG_CACHE
    call _ni_chr_decode
    add sp, 8
    call disc_check
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0
    mov di, t0row0
    mov cx, 8
.k5a:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad5
    inc si
    inc di
    loop .k5a
    mov si, 63 * 64 + 7 * 8
    mov di, t63row7
    mov cx, 8
.k5b:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad5
    inc si
    inc di
    loop .k5b
    cmp byte [4096], 0xE5           ; ...and it wrote nothing past the end
    jne .bad5
    pop ds
    call ok
    jmp c8
.bad5:
    pop ds
    mov si, msg_chr
    call bad

; --- case 8: ni_bg_line, against HAND-COMPUTED pixels -------------------------
; v = 0 (coarse X 0, coarse Y 0, nametable 0, fine Y 0), fine X 0, vertical
; mirroring so nametable 0 folds onto physical A, the background on with its
; left column shown.
;
;   nametable[0] = tile 1, whose CHR row 0 is plane0 10110001 / plane1 11000011
;                  -> pattern values 3, 2, 1, 1, 0, 0, 2, 3 (case 5's tile);
;   nametable[1] = tile 0, which is all zeros -> eight transparent pixels;
;   the attribute byte is 0, so palette 0 and the LUT index is the value.
;
; With ni_bgpal = 8F 11 22 33 ... the first sixteen visible pixels are
;   33 22 11 11 8F 8F 22 33   8F 8F 8F 8F 8F 8F 8F 8F
; and ni_line[0..7] - which a fine X of 0 never reaches - must be untouched.
c8:
    call disc_arm
    call nifix_zero
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov byte [NI_O_VRAM + 0], 1     ; tile 1 at column 0
    mov byte [NI_O_VRAM + 1], 0     ; ...and tile 0 at column 1
    mov ax, SEG_CHR
    mov ds, ax
    mov byte [16 + 0], 0xB1         ; tile 1, plane 0, row 0
    mov byte [16 + 8], 0xC3         ; tile 1, plane 1, row 0
    pop ds
    PUSHI 0
    PUSHI SEG_CHR
    PUSHI 0
    PUSHI SEG_CACHE
    call _ni_chr_decode             ; bank 0 only: tiles 0 and 1 are in it
    add sp, 8
    mov byte [_ni_bgpal + 0], 0x8F
    mov byte [_ni_bgpal + 1], 0x11
    mov byte [_ni_bgpal + 2], 0x22
    mov byte [_ni_bgpal + 3], 0x33
    mov word [_ni_cs + NIC_CACHESEG], SEG_CACHE
    mov word [_ni_cs + NIC_MACHSEG], SEG_MACH
    mov word [_ni_cs + NIC_V], 0
    mov word [_ni_cs + NIC_BGTILE], 0
    mov word [_ni_cs + NIC_FX], 0
    mov word [_ni_cs + NIC_MASK], 0x0A
    mov word [_ni_cs + NIC_MIRROR], 1
    mov word [_ni_cs + NIC_LINE], 0
    mov si, _ni_line                ; THE GUARD: a fine X of 0 must leave
    mov cx, 8                       ; ni_line[0..7] alone. Stored through DS
    mov al, 0xE5                    ; and not with `rep stosb`: ES is this
.p8:                                ; harness's stand-in for KERNEL_SEG and
    mov [si], al                    ; the discipline check is armed
    inc si
    loop .p8
    call _ni_bg_line
    call disc_check
    mov si, _ni_line
    mov cx, 8
.g8:
    cmp byte [si], 0xE5
    jne .bad8
    inc si
    loop .g8
    mov si, _ni_line + 8
    mov di, bgexp
    mov cx, 16
.k8:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad8
    inc si
    inc di
    loop .k8
    call ok
    jmp c9
.bad8:
    mov si, msg_bg
    call bad

; --- case 9: ni_spr_line - first writer wins, priority, and the strike -------
; The background is tile 2, whose every pixel is value 1 and therefore OPAQUE,
; so the merge has something to lose to. Three sprites are on line 10:
;
;   sprite 0  y 9, tile 1, attr 0x00 (palette 0, in FRONT), x 64
;   sprite 1  y 9, tile 1, attr 0x01 (palette ONE, in front), x 64  <- loses
;   sprite 2  y 9, tile 2, attr 0x20 (BEHIND),                x 80  <- loses
;
; Sprite 1 is at exactly sprite 0's pixels with a different palette, so if the
; scratch were painted back to front - or written over - the palette 1 colours
; would be there instead. Sprite 2 is opaque and behind an opaque background,
; so the background keeps every one of its eight pixels.
;
; With ni_sppal = .. 41 42 43 (palette 0) and 51 52 53 (palette 1), x 64..71 is
;   43 42 41 41 11 11 42 43
; - the two 0x11 being tile 1's transparent pattern values, where the
; background shows through - x 80..87 is eight of 0x11, and the answer is the
; SPRITE-0 STRIKE at x = 64: the first pixel where sprite 0 is opaque over an
; opaque background.
c9:
    call disc_arm
    call nifix_zero
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov si, NI_O_VRAM               ; every tile is 2, so the whole background
                                    ; line is OPAQUE and the merge has
                                    ; something to lose to. Stored one byte at
                                    ; a time and not with `rep stosb`: ES is
                                    ; the harness's stand-in for KERNEL_SEG
                                    ; and must stay it (the discipline check
                                    ; is armed around this case).
                                    ;
                                    ; 960 BYTES AND NOT 1,024: the last 64 are
                                    ; the ATTRIBUTE TABLE, and filling them
                                    ; with 2 as well puts the line on palette
                                    ; 2 - whose LUT entries this case never
                                    ; sets, so every background pixel came out
                                    ; 0 and the merge had nothing to lose to
    mov cx, 960
.f9:
    mov byte [si], 2
    inc si
    loop .f9
    ; the three sprites, and everything else off the screen
    mov si, NI_O_OAM
    mov cx, 256
.o9:
    mov byte [si], 0xFF
    inc si
    loop .o9
    mov byte [NI_O_OAM + 0], 9      ; sprite 0: y, tile, attr, x
    mov byte [NI_O_OAM + 1], 1
    mov byte [NI_O_OAM + 2], 0x00
    mov byte [NI_O_OAM + 3], 64
    mov byte [NI_O_OAM + 4], 9      ; sprite 1, the one that must LOSE
    mov byte [NI_O_OAM + 5], 1
    mov byte [NI_O_OAM + 6], 0x01
    mov byte [NI_O_OAM + 7], 64
    mov byte [NI_O_OAM + 8], 9      ; sprite 2, BEHIND an opaque background
    mov byte [NI_O_OAM + 9], 2
    mov byte [NI_O_OAM + 10], 0x20
    mov byte [NI_O_OAM + 11], 80
    mov ax, SEG_CHR
    mov ds, ax
    mov byte [16 + 0], 0xB1         ; tile 1 row 0, as case 8
    mov byte [16 + 8], 0xC3
    mov si, 32                      ; tile 2: every row plane 0 = 0xFF and
    mov cx, 8                       ; plane 1 = 0, so every pixel is value 1
.t9:
    mov byte [si], 0xFF
    mov byte [si + 8], 0x00
    inc si
    loop .t9
    pop ds
    PUSHI 0
    PUSHI SEG_CHR
    PUSHI 0
    PUSHI SEG_CACHE
    call _ni_chr_decode
    add sp, 8
    mov byte [_ni_bgpal + 0], 0x8F
    mov byte [_ni_bgpal + 1], 0x11
    mov byte [_ni_bgpal + 2], 0x22
    mov byte [_ni_bgpal + 3], 0x33
    mov byte [_ni_sppal + 1], 0x41
    mov byte [_ni_sppal + 2], 0x42
    mov byte [_ni_sppal + 3], 0x43
    mov byte [_ni_sppal + 5], 0x51
    mov byte [_ni_sppal + 6], 0x52
    mov byte [_ni_sppal + 7], 0x53
    mov word [_ni_cs + NIC_V], 0
    mov word [_ni_cs + NIC_FX], 0
    mov word [_ni_cs + NIC_MASK], 0x1E  ; both halves on, both left columns on
    mov word [_ni_cs + NIC_SPH], 8
    mov word [_ni_cs + NIC_SPTILE], 0
    mov word [_ni_cs + NIC_LINE], 10
    call _ni_oam_grab
    call _ni_bg_line
    call _ni_spr_line
    mov [strike], ax
    call disc_check
    cmp word [strike], 64
    jne .bad9
    mov si, _ni_line + 8 + 64
    mov di, sprexp
    mov cx, 8
.k9:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad9
    inc si
    inc di
    loop .k9
    mov si, _ni_line + 8 + 80       ; the BEHIND sprite lost every pixel
    mov cx, 8
.b9:
    cmp byte [si], 0x11
    jne .bad9
    inc si
    loop .b9
    call ok
    jmp c10
.bad9:
    mov si, msg_spr
    call bad

; --- case 10: ni_present13's bound, which is the whole of what it can get
; wrong on a machine with no screen (SPEC.md 91.6.2) ------------------------
; Row 0 and row 199 - the two ends of the drop table's range - each land at
; row * 320 + 32 and are 256 bytes long, so the last byte written is 63,967 of
; the 64,000-byte screen. The guard bytes either side of each run, and eight
; more past the end of the framebuffer, must all survive.
c10:
    call disc_arm
    mov word [_ni_fbseg], SEG_FB
    push ds
    mov ax, SEG_FB
    mov ds, ax
    xor si, si
    mov cx, 32004                   ; 64,008 bytes as words: the screen and
    mov ax, 0xE5E5                  ; eight guard bytes past it
.z10:
    mov [si], ax
    inc si
    inc si
    loop .z10
    pop ds
    mov si, _ni_line + 8            ; a ramp, so a byte landing at the wrong
    xor al, al                      ; offset is visible rather than plausible
    mov cx, 256
.r10:
    mov [si], al
    inc si
    inc al
    loop .r10
    PUSHI 0
    call _ni_present13
    add sp, 2
    PUSHI 199
    call _ni_present13
    add sp, 2
    call disc_check
    push ds
    mov ax, SEG_FB
    mov ds, ax
    xor si, si                      ; the border before row 0's run
    mov cx, 32
.g10a:
    cmp byte [si], 0xE5
    jne .bad10
    inc si
    loop .g10a
    xor al, al                      ; ...the run itself
    mov cx, 256
.g10b:
    cmp [si], al
    jne .bad10
    inc si
    inc al
    loop .g10b
    mov cx, 32                      ; ...and the border after it
.g10c:
    cmp byte [si], 0xE5
    jne .bad10
    inc si
    loop .g10c
    mov si, 199 * 320 + 32          ; row 199's run
    xor al, al
    mov cx, 256
.g10d:
    cmp [si], al
    jne .bad10
    inc si
    inc al
    loop .g10d
    mov cx, 40                      ; ...the 32 border bytes after it AND the
.g10e:                              ; eight past the framebuffer's end
    cmp byte [si], 0xE5
    jne .bad10
    inc si
    loop .g10e
    pop ds
    call ok
    jmp c11
.bad10:
    pop ds
    mov si, msg_pres
    call bad

; --- case 11: THE FIXTURE FRAME, out over COM2 for tools/niref.py -----------
; SPEC.md 91.14.4 names two dumps and this is the SHIPPING ASSEMBLY's: 240
; lines composed by the routines above and written out a byte a pixel, which
; niref.py - an independent compositor written from the NESdev documentation -
; then renders for itself and compares bit for bit. The C model's frame is
; niuitest's and is checked the same way; running the reference against one of
; them alone would be C64-SPEC 9.8's cautionary case one level up.
;
; The fixture is tools/niref.py's synth() and niuitest.c's fixture(), written
; a THIRD time, which is what makes the agreement mean something.
c11:
    call nifix_build
    call com2_init
    xor bp, bp                      ; the line
.line11:
    mov [_ni_cs + NIC_LINE], bp
    mov ax, bp
    call nifix_v
    mov [_ni_cs + NIC_V], ax
    mov word [_ni_cs + NIC_OVF], 0
    call _ni_bg_line
    call _ni_spr_line
    mov si, _ni_line + 8
    mov cx, 256
.px11:
    lodsb
    call com2_putc
    loop .px11
    inc bp
    cmp bp, 240
    jb .line11
    call ok

; --- case 7: THE NEGATIVE CONTROLS ------------------------------------------
; A harness that cannot see a broken routine has proved nothing, so two are
; written here on purpose and the discipline check must catch BOTH.
c7:
    call disc_arm
    call neg_es
    call disc_check_expect_fail
    call disc_arm
    call neg_df
    call disc_check_expect_fail
    cld                             ; ...neg_df left it set

; --- the summary -------------------------------------------------------------
    mov si, msg_sum
    call puts
    mov ax, [nfail]
    call putnum
    mov si, msg_sum2
    call puts
    cmp word [nfail], 0
    jne .f
    mov si, msg_ok
    call puts
    jmp halt
.f:
    mov si, msg_bad
    call puts
halt:
    mov si, msg_nl
    call puts
    cli
.h: hlt
    jmp .h

; -----------------------------------------------------------------------------
; THE FIXTURE (SPEC.md 91.14.4)
;
; tools/niref.py's synth() and hosttest/niuitest.c's fixture(), written a THIRD
; time. Three independent spellings of one formula is what makes the three
; frames agreeing mean something; a fixture written once and shared would make
; two of them agree about a state neither had checked.
; -----------------------------------------------------------------------------
NIFIX_CTRL   equ 0x20               ; bg pattern table $0000, 8x16 SPRITES
NIFIX_MASK   equ 0x1A               ; bg on + bg left on, sprites on, sprite
                                    ;   LEFT COLUMN OFF
NIFIX_FX     equ 3
NIFIX_MIRROR equ 1                  ; vertical

; nifix_zero - the claims back to a known state before a focused case
nifix_zero:
    push ax
    push cx
    push si
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov si, NI_O_VRAM
    mov cx, 2048
    xor al, al
.v:
    mov [si], al
    inc si
    loop .v
    mov si, NI_O_OAM
    mov cx, 256
    mov al, 0xFF                    ; every sprite off the bottom of the
.o:                                 ; screen
    mov [si], al
    inc si
    loop .o
    mov ax, SEG_CHR
    mov ds, ax
    xor si, si
    mov cx, 8192
    xor al, al
.c:
    mov [si], al
    inc si
    loop .c
    pop ds
    pop si
    pop cx
    pop ax
    mov word [_ni_cs + NIC_CACHESEG], SEG_CACHE
    mov word [_ni_cs + NIC_MACHSEG], SEG_MACH
    mov word [_ni_cs + NIC_BGTILE], 0
    mov word [_ni_cs + NIC_SPTILE], 0
    mov word [_ni_cs + NIC_SPH], 8
    mov word [_ni_cs + NIC_MIRROR], 1
    mov word [_ni_cs + NIC_OVF], 0
    ret

; nifix_v - the fixture's loopy address for line AX. Coarse Y walks with the
; line and coarse X steps at line 100: a mid-frame $2005 write, which is the
; case a single snapshot cannot express and a scanline model exists for.
nifix_v:
    push bx
    push cx
    push dx
    mov bx, ax
    mov cl, 3
    shr bx, cl
    and bx, 0x1F                    ; coarse Y
    mov cl, 5
    shl bx, cl                      ; ...in its field
    mov dx, ax
    and dx, 7
    mov cl, 12
    shl dx, cl                      ; fine Y in its field
    or  bx, dx
    cmp ax, 100
    jb  .low
    or  bx, 5                       ; coarse X 5...
    or  bx, 1 << 10                 ; ...and the other nametable
.low:
    mov ax, bx
    pop dx
    pop cx
    pop bx
    ret

; nifix_build - the whole fixture: nametables, palette, OAM, CHR, the decoded
; cache and the two palette lookups.
nifix_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    xor bx, bx                      ; the nametables: (i * 7) & 0xFF
.nt:
    mov ax, bx
    mov cx, 7
    mul cx
    mov si, bx
    add si, NI_O_VRAM
    mov [si], al
    inc bx
    cmp bx, 2048
    jb .nt
    xor bx, bx                      ; the palette: (i * 3 + 1) & 0x3F
.pal:
    mov ax, bx
    mov cx, 3
    mul cx
    inc ax
    and al, 0x3F
    mov si, bx
    add si, NI_O_PAL
    mov [si], al
    inc bx
    cmp bx, 32
    jb .pal
    xor bx, bx                      ; OAM, four bytes a sprite
.oam:
    mov si, bx
    mov cl, 2
    shl si, cl
    add si, NI_O_OAM
    mov ax, bx
    mov cx, 17
    mul cx
    mov [si], al                    ; Y (the sprite's top, less one)
    mov ax, bx
    mov cx, 5
    mul cx
    mov [si + 1], al                ; tile
    mov al, bl
    and al, 0xE3
    mov [si + 2], al                ; palette, both flips, the behind bit
    mov ax, bx
    mov cx, 13
    mul cx
    mov [si + 3], al                ; X
    inc bx
    cmp bx, 64
    jb .oam
    mov ax, SEG_CHR
    mov ds, ax
    xor bx, bx                      ; CHR: (i * 13 + (i >> 4)) & 0xFF
.chr:
    mov ax, bx
    mov cx, 13
    mul cx
    mov dx, bx
    mov cl, 4
    shr dx, cl
    add ax, dx
    mov si, bx
    mov [si], al
    inc bx
    cmp bx, 8192
    jb .chr
    pop ds
    ; the eight 1KB banks into the 32KB cache
    xor bx, bx
.bank:
    mov ax, bx
    mov cl, 10
    shl ax, cl
    push ax                         ; soff = bank * 1024
    PUSHI SEG_CHR                   ; sseg
    mov ax, bx
    mov cl, 12
    shl ax, cl
    push ax                         ; doff = bank * 4096
    PUSHI SEG_CACHE                 ; dseg
    call _ni_chr_decode
    add sp, 8
    inc bx
    cmp bx, 8
    jb .bank
    ; the two palette lookups, ni_pal_build's own arithmetic
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov al, [NI_O_PAL]
    and al, 0x3F
    or  al, 0x80                    ; THE BACKDROP, TAGGED - which is what
    mov dl, al                      ; makes the background's inner loop
    pop ds                          ; branchless (SPEC.md 91.5.2)
    xor bx, bx                      ; bx = p * 4
.lut:
    mov si, bx
    mov [_ni_bgpal + si], dl
    mov byte [_ni_sppal + si], 0
    mov cx, 3
.lutv:
    inc si
    push ds
    push si
    mov ax, SEG_MACH
    mov ds, ax
    mov ax, si
    add ax, NI_O_PAL
    mov si, ax
    mov al, [si]
    and al, 0x3F
    mov ah, [si + 0x10]
    and ah, 0x3F
    pop si
    pop ds
    mov [_ni_bgpal + si], al
    mov [_ni_sppal + si], ah
    loop .lutv
    add bx, 4
    cmp bx, 16
    jb .lut
    mov word [_ni_cs + NIC_CACHESEG], SEG_CACHE
    mov word [_ni_cs + NIC_MACHSEG], SEG_MACH
    mov word [_ni_cs + NIC_BGTILE], 0
    mov word [_ni_cs + NIC_SPTILE], 0
    mov word [_ni_cs + NIC_SPH], 16     ; NIFIX_CTRL bit 5: 8x16 sprites
    mov word [_ni_cs + NIC_FX], NIFIX_FX
    mov word [_ni_cs + NIC_MASK], NIFIX_MASK
    mov word [_ni_cs + NIC_MIRROR], NIFIX_MIRROR
    call _ni_oam_grab
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE DISCIPLINE CHECK (case 6, applied to every case)
; -----------------------------------------------------------------------------
disc_arm:
    mov [d_es], es
    mov [d_ds], ds
    mov [d_bp], bp
    mov [d_sp], sp
    ret

disc_check:
    pushf
    push ax
    mov ax, es
    cmp ax, [d_es]
    jne .bad
    mov ax, ds
    cmp ax, [d_ds]
    jne .bad
    cmp bp, [d_bp]
    jne .bad
    mov ax, sp
    add ax, 4                       ; our own pushf and push ax
    cmp ax, [d_sp]
    jne .bad
    pushf
    pop ax
    test ah, 0x04                   ; DF is bit 10, which is bit 2 of AH
    jnz .bad
    pop ax
    popf
    ret
.bad:
    pop ax
    popf
    mov si, msg_disc
    call bad
    ret

; ...and the same check INVERTED, for the two deliberately broken routines
disc_check_expect_fail:
    push ax
    mov ax, es
    cmp ax, [d_es]
    jne .caught
    pushf
    pop ax
    test ah, 0x04
    jnz .caught
    pop ax
    mov si, msg_neg
    call bad
    ret
.caught:
    pop ax
    mov al, 'N'
    call putc
    ret

; the two broken routines. They are HERE and not in the shipping text.
neg_es:
    mov ax, SEG_CACHE
    mov es, ax                      ; ...and never puts it back
    ret
neg_df:
    std                             ; ...and never clears it
    ret

; -----------------------------------------------------------------------------
; REPORTING
; -----------------------------------------------------------------------------
ok:
    mov al, '.'
    call putc
    ret

bad:
    inc word [nfail]
    push ax
    mov al, '!'
    call putc
    call puts
    mov si, msg_nl
    call puts
    pop ax
    ret

; --- COM2, for the FRAME DUMP -----------------------------------------------
; A second port, because the frame is 61,440 raw bytes and the case reports
; are text: interleaving them on one line would need an escape and a
; de-escaper, and a second UART is free. tools/niref.py reads what comes out
; of it.
com2_init:
    push ax
    push dx
    mov dx, 0x2FB
    mov al, 0x80                    ; DLAB
    out dx, al
    mov dx, 0x2F8
    mov al, 1                       ; divisor 1: 115,200 baud
    out dx, al
    mov dx, 0x2F9
    xor al, al
    out dx, al
    mov dx, 0x2FB
    mov al, 0x03                    ; 8N1, DLAB off
    out dx, al
    pop dx
    pop ax
    ret

com2_putc:
    push ax
    push dx
.wait:
    mov dx, 0x2FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x2F8
    out dx, al
    pop dx
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
    push si
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e:
    pop si
    pop ax
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

section .rodata
msg_hello: db 13, 10, "nimemtest: nimem.inc and niband.inc, SS != DS", 13, 10, 0
msg_move:  db "ni_move", 0
msg_fill:  db "ni_fill", 0
msg_word:  db "ni_pokew/ni_peekw", 0
msg_oam:   db "ni_oam_move", 0
msg_chr:   db "ni_chr_decode", 0
msg_bg:    db "ni_bg_line", 0
msg_spr:   db "ni_spr_line", 0
msg_pres:  db "ni_present13", 0
msg_disc:  db "ES/DS/BP/SP/DF discipline", 0
msg_neg:   db "a NEGATIVE CONTROL passed the discipline check", 0
msg_sum:   db 13, 10, "nimemtest: ", 0
msg_sum2:  db " failures - ", 0
msg_ok:    db "nimem OK", 0
msg_bad:   db "FAILURES in nimem", 0
msg_nl:    db 13, 10, 0

t0row0:    db 3, 2, 1, 1, 0, 0, 2, 3
t63row7:   db 1, 1, 1, 1, 3, 3, 3, 3

; case 8: tile 1's row 0 through ni_bgpal, then eight of tile 0's transparent
bgexp:     db 0x33, 0x22, 0x11, 0x11, 0x8F, 0x8F, 0x22, 0x33
           db 0x8F, 0x8F, 0x8F, 0x8F, 0x8F, 0x8F, 0x8F, 0x8F
; case 9: sprite 0's row 0 through ni_sppal, with the background showing at
; the two pixels its pattern leaves transparent
sprexp:    db 0x43, 0x42, 0x41, 0x41, 0x11, 0x11, 0x42, 0x43

section .bss
strike: resw 1
nfail: resw 1
d_es:  resw 1
d_ds:  resw 1
d_bp:  resw 1
d_sp:  resw 1

section .text

; =============================================================================
; THE SHIPPING TEXT, %included and never copied
;
; nicpu.inc is here for its .bss register file alone: nimem.inc reads the
; machine claim's segment out of _ni_m, exactly as it does in the package.
; Its two calls out to the C are stubbed below, because this harness has no C
; in it - and stubbing them is honest here, since no case below reaches one.
; =============================================================================
_ni_io_rd:
    xor ax, ax
    ret
_ni_io_wr:
    xor ax, ax
    ret

%include "nicpu.inc"
%include "nimem.inc"
%include "niband.inc"
