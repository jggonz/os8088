; =============================================================================
; os8088 - apps/paccman/hosttest/pmcbandtest.asm
;
; A BOOT SECTOR THAT RUNS apps/paccman/pmcband.inc ON A REAL x86 (SPEC.md 91),
; under the one condition that makes this OS unlike every other target:
; SS != DS. The composer is the only hand-written assembly in PACCMAN;
; tools/cc8086.py never sees it, the host harness substitutes C for it, and a
; routine that got a segment wrong here would write into KERNEL_SEG and NOT
; FAULT (LESSONS.md 4). So the SHIPPING file is %included - not copied - and
; run.
;
; It is apps/c64/hosttest/c64memtest.asm's shape, and PACCMAN's version of it
; is simpler in one way and stricter in another. Simpler: pmcband.inc loads no
; segment register at all - `lodsb` reads DS:SI and every store is a
; DS-relative `mov [di], r8` - so the claim under test is that ES is NEVER
; TOUCHED, rather than that it is put back. Stricter: the answers are not
; restated here. They are the vectors apps/paccman/hosttest/pmcuitest.c wrote
; into build/pmcbandvec.inc with C twins that reach them a DIFFERENT WAY -
; through pmc_pal, the bit values and pmc_mono, where these loops go through
; pmc_pairs, pmc_planar and pmc_mono2 - so a byte agreeing here is two
; independent implementations agreeing, and the three generated lookup tables
; are under test with the loops.
;
; CS = DS = 0 stand in for the package's segment (where CS = DS, SPEC.md 1),
; SS = 0x1000 is the task stack, and ES = 0x3000 stands in for KERNEL_SEG and
; must come back untouched.
;
; WHAT EACH CASE CHECKS
;   1. _pmc_tile, rowstep 1: one 8x8 tile through its colour block into the
;      packed band, eight rows at PMC_BAND_ROW apart, against pv_t1_exp;
;   2. _pmc_tile, rowstep 2 with no table: the plain alternate-row SAMPLE,
;      four output rows, against pv_t2_exp. Sampling the wrong rows is a
;      defect that draws a plausible picture, so it has a vector of its own;
;  2b. _pmc_tile, rowstep 2 THROUGH pv_zmask: the shipping CGA arm's per-pixel
;      row MERGE (SPEC.md 91), against pv_t3_exp. A merge that reached for the
;      wrong row, or that OR-ed the two colour indices instead of taking the
;      odd row's pixel only where the even row's is 0, draws a plausible
;      picture too - and the twin that wrote pv_t3_exp asks the question as a
;      SENTENCE where these loops ask it as a table;
;   3. _pmc_pack_pl: a whole 8-row band into four bitplanes, against
;      pv_pl_exp. All four planes, because a plane-3 fault is invisible in
;      the low eight colours;
;   4. _pmc_pack_1: the same band into 1bpp through the class table and the
;      row-alternating checkerboard, against pv_m1_exp;
;   5. a SPAN: pack_pl and pack_1 over cols = 7 must write exactly the first 7
;      bytes of each row and NOT the eighth - the damage span is what makes a
;      frame cheap, and a packer that ignored `cols` would still look right;
;  5b. _pmc_sprite over a band that already holds filler: colour index 0 has
;      to leave the band's own nibble alone. Four vectors - even nibble
;      forwards, and the awkward one (odd nibble + flipx + a negative row
;      step) - each SAMPLED and MERGED, because the CGA row merge reaches the
;      sprite layer too (SPEC.md 91) and its +-4 to the dropped row changes
;      SIGN with flipy;
;   6. after every call ES, DS, SS, BP and SP are what they were and DF IS
;      CLEAR;
;   7. NEGATIVE CONTROLS, because a harness that cannot fail has proved
;      nothing: a routine that leaves ES on another segment and one that
;      returns with DF set must both be CAUGHT, and a deliberately flipped bit
;      in an expected vector must make the comparator say no.
;
; RUN IT:  apps/paccman/hosttest/pmcbandtest.sh
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000
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
    xor bp, bp                      ; the failure tally

    mov si, msg_hello
    call puts

    ; --- (1) _pmc_tile, every source row -------------------------------------
    mov al, 0xAA
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call fill
    mov word [pushes], 5
    PUSHI pv_zmask
    PUSHI 1
    PUSHI pt_band
    PUSHI pv_pairs
    PUSHI pv_tile_src
    call disc_call
    dw _pmc_tile
    mov si, pt_band
    mov di, pv_t1_exp
    mov cx, 8 * PMC_BAND_ROW
    call expect

    ; --- (2) _pmc_tile, alternate source rows (the CGA layout) ---------------
    mov al, 0xAA
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call fill
    mov word [pushes], 5
    PUSHI 0                         ; no table: the plain SAMPLE, the arm the
    PUSHI 2                         ; merge below replaced
    PUSHI pt_band
    PUSHI pv_pairs
    PUSHI pv_tile_src
    call disc_call
    dw _pmc_tile
    mov si, pt_band
    mov di, pv_t2_exp
    mov cx, 4 * PMC_BAND_ROW
    call expect

    ; --- (2b) _pmc_tile, alternate rows MERGED (SPEC.md 91) ------------------
    ; The shipping CGA arm: `merged = even | (odd & pmc_zmask[even])`, which is
    ; what keeps the arcade font's middle strokes on a 200-line screen. It is a
    ; vector of its own rather than a replacement for (2), because the sampled
    ; arm is still reachable (zmask = 0) and is what the band bench measures the
    ; merge's cost against.
    mov al, 0xAA
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call fill
    mov word [pushes], 5
    PUSHI pv_zmask
    PUSHI 2
    PUSHI pt_band
    PUSHI pv_pairs
    PUSHI pv_tile_src
    call disc_call
    dw _pmc_tile
    mov si, pt_band
    mov di, pv_t3_exp
    mov cx, 4 * PMC_BAND_ROW
    call expect

    ; --- (3) _pmc_pack_pl, the whole band into four planes -------------------
    xor al, al
    mov di, pt_planes
    mov cx, 4 * PMC_PL_STEP
    call fill
    mov word [pushes], 5
    PUSHI PMC_PL_STRIDE
    PUSHI pv_planar
    PUSHI 8
    PUSHI pt_planes
    PUSHI pv_band
    call disc_call
    dw _pmc_pack_pl
    mov si, pt_planes
    mov di, pv_pl_exp
    mov cx, 4 * PMC_PL_STEP
    call expect

    ; --- (4) _pmc_pack_1, the same band into 1bpp ----------------------------
    xor al, al
    mov di, pt_bits
    mov cx, 8 * PMC_PL_STRIDE
    call fill
    mov word [pushes], 6
    PUSHI PMC_PL_STRIDE
    PUSHI 0
    PUSHI pv_mono2
    PUSHI 8
    PUSHI pt_bits
    PUSHI pv_band
    call disc_call
    dw _pmc_pack_1
    mov si, pt_bits
    mov di, pv_m1_exp
    mov cx, 8 * PMC_PL_STRIDE
    call expect

    ; --- (5) A SPAN: cols = 7 writes seven bytes a row and not the eighth ----
    ; The damage span is what makes a frame cheap (SPEC.md 91), and a packer
    ; that ignored `cols` and wrote all 28 would still draw the right picture
    ; on the glass - so the byte AFTER the span is the whole of this case.
    mov al, 0x5A
    mov di, pt_planes
    mov cx, 4 * PMC_PL_STEP
    call fill
    mov word [pushes], 5
    PUSHI 7
    PUSHI pv_planar
    PUSHI 8
    PUSHI pt_planes
    PUSHI pv_band
    call disc_call
    dw _pmc_pack_pl
    ; row 0 of plane 0: bytes 0..6 are the answer, byte 7 is untouched filler
    mov si, pt_planes
    mov di, pv_pl_exp
    mov cx, 7
    call cmpn
    jnz .f5
    cmp byte [pt_planes + 7], 0x5A
    jne .f5
    ; ...and row 1 starts at PMC_PL_STRIDE, not at 7
    mov si, pt_planes + PMC_PL_STRIDE
    mov di, pv_pl_exp + PMC_PL_STRIDE
    mov cx, 7
    call cmpn
    jnz .f5
    ; plane 3's row 0 too - the plane step must be unaffected by the span
    mov si, pt_planes + 3 * PMC_PL_STEP
    mov di, pv_pl_exp + 3 * PMC_PL_STEP
    mov cx, 7
    call cmpn
    jnz .f5
    mov al, '.'
    call putc
    jmp .span1
.f5:
    mov al, '5'
    call putc
    inc bp
.span1:
    mov al, 0x5A
    mov di, pt_bits
    mov cx, 8 * PMC_PL_STRIDE
    call fill
    mov word [pushes], 6
    PUSHI 7
    PUSHI 0
    PUSHI pv_mono2
    PUSHI 8
    PUSHI pt_bits
    PUSHI pv_band
    call disc_call
    dw _pmc_pack_1
    mov si, pt_bits
    mov di, pv_m1_exp
    mov cx, 7
    call cmpn
    jnz .f5b
    cmp byte [pt_bits + 7], 0x5A
    jne .f5b
    mov si, pt_bits + PMC_PL_STRIDE
    mov di, pv_m1_exp + PMC_PL_STRIDE
    mov cx, 7
    call cmpn
    jnz .f5b
    mov al, '.'
    call putc
    jmp .spr
.f5b:
    mov al, '6'
    call putc
    inc bp

.spr:
    ; --- (7) _pmc_sprite, MERGED OVER a band that already holds something ----
    ; A sprite is composed over the tiles, not instead of them: colour index 0
    ; has to leave the band's own nibble alone. A vector taken over an EMPTY
    ; band would pass whether that were true or not, so pv_spr_band is filler
    ; and the answer is the merge of the two.
    mov si, pv_spr_band
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call copyn
    mov word [pushes], 8
    PUSHI 0                         ; zmask: the plain alternate-row sample
    PUSHI pv_brev
    PUSHI 0                         ; flags: high nibble first, no flipx
    PUSHI 8                         ; rows
    PUSHI 4                         ; sinc: every source row, forwards
    PUSHI pt_band + 8
    PUSHI pv_spr_pal
    PUSHI pv_spr_src
    call disc_call
    dw _pmc_sprite
    mov si, pt_band
    mov di, pv_s1_exp
    mov cx, 8 * PMC_BAND_ROW
    call expect

    ; --- (8) ...ODD NIBBLE, FLIPX AND A NEGATIVE ROW STEP, ALL AT ONCE -------
    ; The three awkward cases are one case, because they co-occur: Pac-Man
    ; running left on the CGA layout at an odd pixel is all three. flipy is a
    ; NEGATIVE sinc and nothing else, which is why the routine has no test for
    ; it (pmcband.inc).
    mov si, pv_spr_band
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call copyn
    mov word [pushes], 8
    PUSHI 0                         ; zmask: the plain alternate-row sample
    PUSHI pv_brev
    PUSHI 3                         ; flags: LOW nibble first, and flipx
    PUSHI 4                         ; rows
    PUSHI -8                        ; sinc: back two source rows a band row
    PUSHI pt_band + 9
    PUSHI pv_spr_pal
    PUSHI pv_spr_src + 15 * 4       ; ...starting at the sprite's LAST row
    call disc_call
    dw _pmc_sprite
    mov si, pt_band
    mov di, pv_s2_exp
    mov cx, 8 * PMC_BAND_ROW
    call expect

    ; --- (8b) ...AND BOTH OF THOSE WITH THE CGA ROW MERGE ON -----------------
    ; The shipping CGA sprite layer (SPEC.md 91): a drawn row takes the DROPPED
    ; row's pixel wherever its own is index 0, which is `_pmc_tile`'s sentence
    ; one layer up and keeps Pac-Man's caps and the ghosts' fringes on a
    ; 200-line screen. Two vectors because the merge's +-4 to the partner row
    ; is `sinc >> 1` and flipy makes it NEGATIVE - the sign is the case a
    ; forwards-only vector would not reach.
    mov si, pv_spr_band
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call copyn
    mov word [pushes], 8
    PUSHI pv_zmask                  ; ...the merge, and this is the arm that
    PUSHI pv_brev                   ;    ships on CGA
    PUSHI 0                         ; flags: high nibble first, no flipx
    PUSHI 4                         ; rows
    PUSHI 8                         ; sinc: the CGA layout's alternate rows
    PUSHI pt_band + 8
    PUSHI pv_spr_pal
    PUSHI pv_spr_src
    call disc_call
    dw _pmc_sprite
    mov si, pt_band
    mov di, pv_s3_exp
    mov cx, 8 * PMC_BAND_ROW
    call expect

    mov si, pv_spr_band
    mov di, pt_band
    mov cx, 8 * PMC_BAND_ROW
    call copyn
    mov word [pushes], 8
    PUSHI pv_zmask
    PUSHI pv_brev
    PUSHI 3                         ; flags: LOW nibble first, and flipx
    PUSHI 4                         ; rows
    PUSHI -8                        ; ...and flipy: the partner is BEHIND
    PUSHI pt_band + 9
    PUSHI pv_spr_pal
    PUSHI pv_spr_src + 15 * 4
    call disc_call
    dw _pmc_sprite
    mov si, pt_band
    mov di, pv_s4_exp
    mov cx, 8 * PMC_BAND_ROW
    call expect

    ; --- (9) THE NEGATIVE CONTROLS -------------------------------------------
.negc:
    mov word [disc_expect_bad], 1
    mov word [pushes], 0
    call disc_call
    dw bad_es                       ; leaves ES on another segment
    mov word [pushes], 0
    call disc_call
    dw bad_df                       ; returns with DF set
    mov word [disc_expect_bad], 0

    ; ...and the comparator itself: a flipped bit must be seen. pv_m1_exp is
    ; in .data and is writable, so the flip is real and is put back.
    xor byte [pv_m1_exp], 0x01
    mov si, pv_m1_exp
    mov di, pt_bits
    mov cx, 1
    call cmpn
    jz .fneg                        ; equal after a flip: the comparator lies
    mov al, 'N'
    call putc
    jmp .flipok
.fneg:
    mov al, 'F'
    call putc
    inc bp
.flipok:
    xor byte [pv_m1_exp], 0x01

    ; --- the report -----------------------------------------------------------
    mov si, msg_res
    call puts
    mov ax, bp
    call putdec
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
; disc_call - call the routine whose address follows the CALL, then check that
; it left the machine as it found it. `pushes` says how many argument words
; the caller stacked, because cdecl makes the CALLER clean.
;
; FROM THE `call` TO THE RESTORE, DS MAY BE ANYTHING. A routine that returned
; with DS on another segment would make every store below land there - the
; checker's own bookkeeping corrupted by the very fault it exists to catch -
; so every access is CS-overridden until DS has been recorded and put back.
; CS = DS = 0 here, so a CS override reaches the same bytes whatever DS holds.
; -----------------------------------------------------------------------------
disc_call:
    pop bx
    mov ax, [bx]
    mov [disc_target], ax
    add bx, 2
    mov [disc_ret], bx              ; the real return address, in a VARIABLE -
                                    ; pushing it would put a word between the
                                    ; callee's return address and the cdecl
                                    ; arguments, so every [bp+N] would be off
                                    ; by two and the whole gate would compare
                                    ; garbage against the vectors
    mov [disc_sp], sp
    mov [disc_bp0], bp              ; BP **BEFORE** the call: recording it
                                    ; after would make the test `bp == bp`
    call [disc_target]
    mov [cs:disc_sp2], sp
    mov [cs:disc_bp], bp
    mov [cs:disc_es], es
    mov [cs:disc_ds], ds
    mov [cs:disc_ss], ss
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
    add sp, bx                      ; cdecl: the caller cleans
    mov bp, [disc_bp0]              ; put back the thing just measured - BP is
                                    ; this program's failure tally, so a
                                    ; routine that clobbered it would corrupt
                                    ; the count that catches it

    mov bx, 0
    mov ax, [disc_bp0]
    cmp [disc_bp], ax
    jne .b
    cmp word [disc_es], SEG_KERNEL  ; NEVER TOUCHED, not merely restored
    jne .b
    cmp word [disc_ds], 0
    jne .b
    cmp word [disc_ss], SEG_STACK
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
    jmp .out
.negc:
    or bx, bx
    jz .slipped
    mov al, 'N'
    call putc
    jmp .out
.slipped:
    mov al, 'S'                     ; a broken routine went through undetected
    call putc
    inc bp
.out:
    jmp [disc_ret]

; --- the two broken routines the discipline check has to catch ---------------
bad_es:
    push ax
    mov ax, SEG_STACK
    mov es, ax                      ; and never put back
    pop ax
    ret
bad_df:
    std
    ret

; -----------------------------------------------------------------------------
; expect - DS:SI against DS:DI for CX bytes; prints '.' or 'X' and counts.
; -----------------------------------------------------------------------------
expect:
    call cmpn
    jnz .bad
    mov al, '.'
    call putc
    ret
.bad:
    mov al, 'X'
    call putc
    inc bp
    ret

; cmpn - CX bytes at DS:SI against DS:DI. ZF=1 equal. ES is not used, so this
; is `repe cmpsb`'s job done by hand: cmpsb would read ES:DI, which here is the
; kernel sentinel.
cmpn:
    push si
    push di
    push cx
    push ax
.l:
    jcxz .same
    mov al, [si]
    cmp al, [di]
    jne .diff
    inc si
    inc di
    dec cx
    jmp .l
.same:
    xor al, al
    cmp al, al                      ; ZF = 1
    jmp .out
.diff:
    mov al, 1
    or al, al                       ; ZF = 0
.out:
    pop ax
    pop cx
    pop di
    pop si
    ret

; fill - CX bytes of AL at DS:DI. `stosb` would write ES:DI (SPEC.md 73.5.1),
; and ES is the kernel sentinel this program must not touch.
fill:
    push di
    push cx
.l:
    jcxz .out
    mov [di], al
    inc di
    dec cx
    jmp .l
.out:
    pop cx
    pop di
    ret

; copyn - CX bytes from DS:SI to DS:DI. `movsb` would write ES:DI, and ES is
; the kernel sentinel this program exists to prove nothing touches.
copyn:
    push si
    push di
    push cx
    push ax
.l:
    jcxz .out
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .l
.out:
    pop ax
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; the serial port, which is where the answer comes out
; -----------------------------------------------------------------------------
putc:
    push ax
    push dx
    mov ah, 0x01
    xor dx, dx
    int 0x14
    pop dx
    pop ax
    ret

puts:
    push ax
    push si
.l:
    mov al, [si]
    inc si
    or al, al
    jz .out
    call putc
    jmp .l
.out:
    pop si
    pop ax
    ret

putdec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d: xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p: pop ax
    add al, '0'
    call putc
    loop .p
    pop dx
    pop cx
    pop bx
    pop ax
    ret

halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; THE SHIPPING COMPOSER - %included, never copied. An edit to it re-runs this
; gate through the Makefile's prerequisite on apps/paccman/pmcband.inc.
; -----------------------------------------------------------------------------
%include "paccman/pmcband.inc"

section .rodata
msg_hello: db 'pmcbandtest: pmcband.inc on a real x86, SS != DS', 13, 10, 0
msg_res:   db 13, 10, 'pmcband: ', 0
msg_ok:    db ' failures - pmcband OK', 13, 10, 0
msg_bad:   db ' FAILURES in pmcband', 13, 10, 0

section .data
; The vectors pmcuitest.c wrote, including the answers its independent C twins
; give. In .data and not .rodata because the comparator's own negative control
; flips a bit of pv_m1_exp and puts it back.
%include "pmcbandvec.inc"

section .bss
pt_band:   resb 8 * PMC_BAND_ROW
pt_planes: resb 4 * PMC_PL_STEP
pt_bits:   resb 8 * PMC_PL_STRIDE

disc_target:      resw 1
disc_ret:         resw 1
disc_sp:          resw 1
disc_sp2:         resw 1
disc_bp:          resw 1
disc_bp0:         resw 1
disc_es:          resw 1
disc_ds:          resw 1
disc_ss:          resw 1
disc_fl:          resw 1
disc_expect_bad:  resw 1
pushes:           resw 1

section .text
