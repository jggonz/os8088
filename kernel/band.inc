; =============================================================================
; band.inc - the 1bpp BAND COMPOSER (SPEC.md 5.9)
;
; A band is a small 1bpp bitmap in a heap claim of its own that a caller draws
; a WHOLE picture into - ground, lines, boxes and text - and then hands to
; gfx_blit1 as one operation. Nothing reaches the framebuffer until band_emit,
; so every pixel the band covers is written EXACTLY ONCE and there is no
; interval in which the picture is half-drawn. That is the whole point: on a
; 4.77MHz 8088 the gap between an erase and the letters over it is visible
; (SPEC.md 6.6), and a title bar spends four passes over its caption band.
;
; IT IS A COMPOSER, NOT AN ADAPTER PATH. A band is 1bpp on every adapter and
; the two colours arrive at BLIT time from gfx_blit1_pen (SPEC.md 5.4.2.2), so
; there is one implementation of every op here and no planar code at all - a
; Color title bar's white-on-blue costs exactly what Bright's black-on-white
; does.
;
; CHUNKING IS THE CALLER'S, AND IT IS ONE LOOP. Every op takes SCREEN
; coordinates and clips itself to the band, so a picture wider than the buffer
; is drawn by opening the band at successive x and REPLAYING THE SAME OP LIST
; into each. Nothing has to be split by hand, and a glyph straddling two
; chunks is composed into both by the same clip that handles a glyph
; straddling the band's edge.
;
; WHY IT IS NOT font_run WITH A DESTINATION. font_run_x carries the display
; split (SPEC.md 39.14.6), GFXDENTERCD's nesting, the planar prologue
; (6.1.10), the unaligned phase (6.1.11) and the clip machinery, and threading
; a RAM destination through all of it would put the hottest primitive in the
; system at risk to save the small half of the work. A band is always 1bpp, so
; only the MONO composition is needed here. What is shared is the thing worth
; sharing: font_glyphs itself.
;
; THE BUFFER IS A HEAP CLAIM, NOT .bss (SPEC.md 5.9.2). It is 2KB, taken once
; at boot under MEM_K_BAND and never freed, and that is what makes the width
; cap a WHOLE SCREEN ROW rather than 128 pixels: the same 2KB out of
; KERN_BUDGET would have been two rungs of the kernel's own 64KB window, and
; PERFORMANCE.md Set 88 measured the composer's cost as per CHUNK - so the
; band being wide enough to hold the whole picture in one is the difference
; the memory buys. Every op therefore writes through ES, which the public
; entries set from [bnd_seg] and the internals inherit.
;
; The bodies are .cold (SPEC.md 2.6): a band is composed once per window
; operation, not per frame, and .text's rung had 90 bytes left when this
; arrived. What is resident is band_init alone - a claim has to be taken from
; somewhere that may call mem_claim near.
;
; IT IS A KNOB (SPEC.md 5.9.6), `make BAND=1`, and it was kern_big's default
; for one cycle in between. What it draws is the faster bar on both 1bpp
; adapters - 36.5 ms against the fifteen primitive calls' 40.8 on Hercules and
; 37.1 against 42.0 on CGA - and the slower one on VGA, 38.3 against 30.3,
; because a band is 1bpp on every card while the primitives get VGA's planar
; hardware. What it buys on all three is a bar that writes no pixel twice
; where the one it replaces writes the caption's own rows four times, and that
; is visible on the target machine (docs/TEXT-PLAN.md 1.1: flicker first,
; speed second). None of that moved when the default did; the BYTES are what
; moved it - 1,634 of them, 1,278 out of .cold. `make`
; against `make BAND=1` is the A/B, and the knob is now the only thing that
; keeps this file assembling at all: no shipped build compiles it, so the
; build matrix's `band` row (tests/unit/t_buildmatrix.py) is what stops it
; rotting.
; =============================================================================

BAND_KB     equ 2               ; the buffer, in KB: mem_claim's unit, and the
BAND_BYTES  equ BAND_KB * 1024  ; smallest claim that holds a whole screen row
BAND_MAXW   equ 720             ; band width cap, PIXELS - a multiple of 8, and
BAND_MAXB   equ BAND_MAXW / 8   ; deliberately a WHOLE SCREEN ROW ON THE WIDEST
                                ; adapter SPEC.md 39 has: HERCULES is 720x348
                                ; and the other two are 640, so a title bar is
                                ; one chunk on all three and Set 88's per-chunk
                                ; cost is paid once. It read 640 - VGA's and
                                ; CGA's width - and claimed no adapter was
                                ; wider, which vid_tab's Hercules row has
                                ; always contradicted. Nothing clipped wrong,
                                ; because every op clips itself and the chunk
                                ; loop replays; what it cost was a SECOND
                                ; chunk - another band_open_x clear, another
                                ; replay, another gfx_blit1 - for any window
                                ; wider than 640 on Hercules, which is every
                                ; ZOOMED one (wm_zoom takes [vid_pw] = 720)
%if gfx_b1pap - gfx_b1ink != 1
  %error "gfx_b1ink/gfx_b1pap must stay adjacent: band_emit_x sets both in one word store"
%endif
BAND_MAXH   equ 16              ; band height cap, rows. TITLE_H is 18 and the
                                ; bar's interior is 16 of them; the separator
                                ; under it spans the ragged edges the band
                                ; cannot reach and stays a gfx_hline
%if BAND_MAXB * BAND_MAXH > BAND_BYTES
  %error "band.inc: BAND_MAXW x BAND_MAXH no longer fits BAND_KB"
%endif
                                ; THE ONLY size guard, and it is at assembly
                                ; time: band_open_x's two cap checks are then
                                ; the whole of the runtime one, with no product
                                ; to compute and no refusal path to unwind

%ifdef BANDCOMP
section .cold

; -----------------------------------------------------------------------------
; band_pen_x - the two colours this band will be BLITTED with (SPEC.md 5.9.3)
; in:  AL = ink (what an ink op draws), AH = paper (the ground)
; out: nothing; every register preserved
;
; CALL IT BEFORE band_open_x. It is not an argument to the emit because of
; what it decides: on a 1bpp adapter gfx_blit1 IGNORES the pen - a set band bit
; is a LIT pixel and that is the whole of the mapping (SPEC.md 5.4.2.2) - so a
; band composed ink-as-set-bits comes out INVERTED whenever the ink is not the
; lit colour, which on Bright is every title bar in the system. The fix is a
; POLARITY, decided once here and folded into the ops' own constants: the clear
; band_open_x lays down, the bit band_ink_x stores, and - since a glyph's bits
; lie entirely inside its own mask - one XOR against that mask in bnd_cell.
; Nothing per pixel, and the VGA path is untouched because there the pen really
; does the mapping.
;
; WHAT IT DECIDES IS NOW SPEED AND NOT THE PICTURE (SPEC.md 5.9.3.1). This
; reads [vid_mono], which is a fact about the display that happens to be
; CURRENT - and a band on an extended desktop is emitted across two displays
; that never have the same depth. So band_emit_x DECLARES the polarity to
; gfx_blit1 instead, and the emit turns the band over per display when it has
; to. The guess here is still worth making: the display it is right about takes
; the `rep movsw` and the other one the complemented hand loop at 34 clocks a
; word against 25 (SPEC.md 5.4.2.2), so being wrong costs 36% of an emit rather
; than an inverted bar.
; -----------------------------------------------------------------------------
band_pen_x:
    mov [bnd_pen], ax
    mov byte [bnd_pol], 0       ; VGA: gfx_blit1_pen answers for the colours
    cmp byte [vid_mono], 0
    je .out
    push bx
    mov bl, al                  ; gfx_inktab (SPEC.md 39.4): FFh = this colour
    xor bh, bh                  ; lights the pixel, 00h = black, 01h = the 50%
    and bl, 15                  ; dither class, which a band cannot express and
    cmp byte [bx + gfx_inktab], 0FFh    ; is therefore treated as unlit
    pop bx
    je .out
    mov byte [bnd_pol], 1       ; the ink is DARK, so the band is stored with
.out:                           ; every bit the other way up
    ret

; -----------------------------------------------------------------------------
; band_open_x - begin a band at screen (AX, BX), CX pixels wide, DX rows
; in:  AX = x (a multiple of 8), BX = y, CX = width (a multiple of 8), DX = rows
; out: CF = 0 begun and cleared to PAPER, CF = 1 refused and no band exists
; clobbers: nothing but flags
;
; The refusals are gfx_blit1's own (SPEC.md 5.4.2: an x or a width off the byte
; grid is a caller error, not a shape to clip), the buffer's caps, and NO
; BUFFER AT ALL - band_init's claim can be refused on a full heap, and a band
; with nowhere to compose is exactly the same answer to the caller as a band
; too small. A caller that can be refused must have a path that does not use a
; band; kern_small has no gfx_blit1 at all, so it has one anyway.
; -----------------------------------------------------------------------------
band_open_x:
    test ax, 7
    jnz .no
    test cx, 7
    jnz .no
    or cx, cx
    jz .no
    cmp cx, BAND_MAXW
    ja .no
    or dx, dx
    jz .no
    cmp dx, BAND_MAXH
    ja .no
    cmp word [bnd_seg], 0
    je .no                      ; boot asked and the heap said no
    push ax
    push cx
    push dx                     ; the `mul` below writes DX:AX
    push di
    push es
    mov [bnd_x], ax
    mov [bnd_y], bx
    mov [bnd_w], cx
    mov [bnd_h], dx
    mov ax, cx
    mov cl, 3
    shr ax, cl
    mov [bnd_b], ax             ; bytes a row
    mul dx                      ; AX = bytes in the whole band (<= BAND_BYTES,
    mov cx, ax                  ; which the %if above is what guarantees)
    mov es, [bnd_seg]
    xor di, di
    mov al, [bnd_pol]           ; PAPER, in the storage polarity band_pen_x
    neg al                      ; chose: 00h or FFh
    cld
    rep stosb
    mov al, 1                   ; ...and the first op draws in INK, which is
    xor al, [bnd_pol]           ; the other one
    mov [bnd_ink], al
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; band_ink_x - AL = 1 the following ops draw in INK, 0 in PAPER
; -----------------------------------------------------------------------------
band_ink_x:
    xor al, [bnd_pol]           ; ...which is a SET bit or a clear one depending
    mov [bnd_ink], al           ; on band_pen_x's polarity; AL is spent, and
    ret                         ; every caller passes a constant

; -----------------------------------------------------------------------------
; bnd_clip - a screen span AX..CX against the band's own columns
; in:  AX = x1, CX = x2 (SCREEN, inclusive)
; out: CF = 0 with AX/CX clipped and made BAND-relative; CF = 1 nothing of it
;      is in the band. BX/DX preserved.
; -----------------------------------------------------------------------------
bnd_clip:
    sub ax, [bnd_x]
    sub cx, [bnd_x]
    cmp cx, ax
    jl .no                      ; SIGNED: either end can be left of the band
    or ax, ax
    jns .l_ok
    xor ax, ax
.l_ok:
    push bx
    mov bx, [bnd_w]
    dec bx
    cmp cx, bx
    jle .r_ok
    mov cx, bx
.r_ok:
    pop bx
    cmp ax, cx
    jg .no
    or cx, cx
    js .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; band_fill_x - a solid rect, SCREEN coordinates, clipped to the band
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; out: nothing; every register preserved
;
; A FILL'S COLUMNS DO NOT CHANGE DOWN ITS ROWS, and this used to recompute them
; for every one: `bnd_span` took two shifts by CL, two mask builds, an ink test
; and six pushes to draw what is often two bytes, and `band_fill_x` called it
; once a row. 5,368 cycles a call, which was 20.25 ms of a composed title bar's
; 47.7 (PERFORMANCE.md Set 91). It is bnd_cell's defect one level up
; (SPEC.md 5.9.4) and it gets the same answer: both byte indices, both coverage
; masks, the direction and the middle run's length are decided ONCE, and then
; one of three loops runs with nothing in it but the writes.
;
; THE DIRECTION IS FOLDED INTO THE MASKS, which is what keeps it to three loops
; instead of six. Drawing in ink is `byte | m` and in paper `byte & ~m`, so an
; (AND, OR) pair says both: (FFh, m) for ink and (~m, 0) for paper. The whole
; -byte run between the edges needs neither - its mask is FFh, so ORing it in
; gives FFh and ANDing it out gives 0, and either way it is a plain `rep stosb`.
; -----------------------------------------------------------------------------
band_fill_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [bnd_seg]           ; the band's segment, for everything below
    call bnd_clip
    jc .out
    push ax                     ; the clipped columns, while the rows are done
    push cx                     ; - the clip below wants BX, DX and AX at once

    ; --- the ROWS: a first one and a count ---------------------------------
    sub bx, [bnd_y]
    sub dx, [bnd_y]
    or bx, bx
    jns .t_ok
    xor bx, bx
.t_ok:
    mov ax, [bnd_h]
    dec ax
    cmp dx, ax
    jle .b_ok
    mov dx, ax
.b_ok:
    cmp bx, dx
    jg .pop2
    or dx, dx
    js .pop2
    sub dx, bx
    inc dx
    push dx                     ; rows to fill, 1.. - banked because the
    mov ax, bx                  ; multiply below writes DX:AX
    mul word [bnd_b]            ; ONE multiply: a band is 2KB, so the product
    mov si, ax                  ; cannot need DX
    pop bp                      ; BP = rows to fill
    pop cx                      ; x2...
    pop ax                      ; ...and x1

    ; --- the COLUMNS, once: two byte indices and two coverage masks --------
    mov di, ax
    mov ah, cl                  ; x2's low byte, banked past the shift count
    mov bx, cx
    mov cl, 3
    shr di, cl                  ; DI = the span's first band byte...
    shr bx, cl                  ; ...and BX its last
    add si, di                  ; SI = where row 0's span starts
    sub bx, di                  ; BX = how many bytes apart the two ends are
    mov cl, al                  ; the LEFT byte's coverage, from x1's phase
    and cl, 7
    mov dh, 0FFh
    shr dh, cl
    mov cl, ah                  ; ...and the RIGHT byte's, from x2's
    and cl, 7
    xor cl, 7
    mov dl, 0FFh
    shl dl, cl
    mov ax, bx                  ; AX = the distance; BX is a mask pair below
    or ax, ax
    jnz .dir
    and dh, dl                  ; ONE byte: both ends fold into one mask

    ; --- and the DIRECTION, folded into an (AND, OR) pair per edge ---------
.dir:
    mov bl, dl                  ; the right coverage, out of DX
    mov bh, 0FFh
    cmp byte [bnd_ink], 0
    je .paper
    mov dl, dh                  ; INK: or = m, and = FFh
    mov dh, bh
    mov cl, 0FFh                ; ...and a whole byte between them is set
    jmp short .dirok
.paper:
    not dh                      ; PAPER: and = ~m, or = 0
    mov dl, 0
    mov bh, bl
    not bh
    mov bl, 0
    mov cl, 0                   ; ...and a whole byte between them is clear
.dirok:
    or ax, ax
    jz .one
    dec ax                      ; AX = the WHOLE bytes between the two edges,
    mov ah, al                  ; and a band row is at most 80 of them
    mov al, cl                  ; AL = what each of them becomes
    or ah, ah
    jz .two
    cld

.wide:
    mov di, si
    and [es:di], dh
    or [es:di], dl
    inc di
    mov cl, ah
    xor ch, ch
    rep stosb                   ; the run, which is a STORE and not a masked
    and [es:di], bh             ; read-modify-write
    or [es:di], bl
    add si, [bnd_b]
    dec bp
    jnz .wide
    jmp short .out

.two:                           ; two bytes with nothing between them
    and [es:si], dh
    or [es:si], dl
    and [es:si+1], bh
    or [es:si+1], bl
    add si, [bnd_b]
    dec bp
    jnz .two
    jmp short .out

.one:                           ; the whole span inside a single byte
    and [es:si], dh
    or [es:si], dl
    add si, [bnd_b]
    dec bp
    jnz .one
    jmp short .out

.pop2:
    pop cx
    pop ax
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; band_hline_x / band_vline_x / band_frame_x - gfx_hline's and gfx_frame's
; argument shapes exactly, so a caller converting a picture does not have to
; re-read them (SPEC.md 5)
; band_hline_x: AX = x1, BX = x2, DX = y
; band_vline_x: AX = x,  BX = y1, DX = y2
; band_frame_x: AX = x1, BX = y1, CX = x2, DX = y2
; -----------------------------------------------------------------------------
band_hline_x:
    push cx
    push bx
    mov cx, bx
    mov bx, dx
    call band_fill_x            ; a one-row rect
    pop bx
    pop cx
    ret

band_vline_x:
    push cx
    mov cx, ax
    call band_fill_x            ; a one-column rect
    pop cx
    ret

band_frame_x:
    push ax
    push bx
    push cx
    push dx
    push bp
    mov bp, dx
    mov dx, bx
    call band_fill_x            ; top edge
    mov dx, bp
    push bx
    mov bx, bp
    call band_fill_x            ; bottom edge
    pop bx
    push cx
    mov cx, ax
    mov dx, bp
    call band_fill_x            ; left edge
    pop cx
    push ax
    mov ax, cx
    mov dx, bp
    call band_fill_x            ; right edge
    pop ax
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; band_run_x - a NUL string as OPAQUE cells, SCREEN coordinates
; in:  CX = x, DX = y, ES:SI = the string (the caller's segment, so a package's
;      title reads through ES exactly as font_str_x's does)
; out: nothing; every register preserved
;
; Opaque: each cell's 8x8 is cleared to PAPER and then the glyph's bits are set,
; which is font_run's decision-per-cell (SPEC.md 6.1) done in RAM. A cell is
; two band bytes when the pen is off the byte grid and one when it is not, and
; the 16-bit shift below produces both without a branch - `shr ax, cl` with the
; glyph in AH and zero in AL gives the high byte in AH and the low byte in AL,
; and at cl = 0 that is the glyph and nothing.
;
; THIS OP HAS TWO SEGMENTS AND ONE REGISTER TO HOLD THEM: the string is the
; caller's and the band is its own, so ES is swapped ONCE PER CELL - two `mov
; es, mem`, about 28 cycles, against a cell's thousand. The alternative was a
; string segment threaded through bnd_cell and its four row loops, which is
; four more arguments to save the two instructions.
; -----------------------------------------------------------------------------
band_run_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [bnd_sseg], es          ; the caller's, banked for the fetch below
    sub dx, [bnd_y]             ; DX = the cell's top row, band-relative
    sub cx, [bnd_x]             ; CX = the pen, band-relative (SIGNED)
.cell:
    mov es, [bnd_sseg]
    mov al, [es:si]
    mov es, [bnd_seg]           ; ...and back to the band for the composition
    or al, al
    jz .out
    inc si
    call bnd_cell
    add cx, 8
    mov ax, [bnd_w]
    cmp cx, ax
    jl .cell                    ; ...and once the pen has left the band there
                                ; is nothing further this chunk can hold, so
                                ; the loop simply falls into the epilogue
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bnd_cell - one opaque cell: AL = the character, CX = its band x (signed),
;            DX = its band y (signed); ES = the BAND. Preserves everything.
;
; ONE CELL IS ONE LOOP, and every decision in it is made before the loop
; starts. The first version made sixteen near calls to place a character -
; `bnd_cput` twice a glyph row and `bnd_cbyte` twice inside that, each one
; re-testing the same band bounds against the same byte index - and cost
; ~10,500 cycles a cell against font_run's 1,243 for the identical opaque 8x8
; decision (PERFORMANCE.md Set 89). Nothing about composing was slow; this
; routine was.
;
; What is hoisted, in order:
;
;   - the VERTICAL clip, to a first glyph row and a count. The old loop ran all
;     eight rows and tested each against the band's top and bottom edge.
;   - the ONE multiply, to the first visible row's address. Every row after it
;     is one stride further on, which the old code did too - through a flag
;     byte and a call.
;   - the two mask bytes AND their complements, into DX/BX rather than four
;     .bss bytes re-read per byte written.
;   - SPEC.md 5.9.3's polarity, into an XOR pair. A glyph's bits lie entirely
;     inside its own mask, so `~g & m` is exactly `g ^ m`, and m is constant
;     for the cell: the inverted case costs ONE `xor ax, bx` a row instead of
;     a compare, a `not` and two `and`s a byte.
;   - and the horizontal case, into WHICH LOOP RUNS. There are no bounds tests
;     left inside any of them, because the entry checks already prove which of
;     the cell's two bytes are in the band and that cannot change per row.
;
; The aligned loop is the one worth reading: at a shift of 0 the mask is FFh,
; so the read-modify-write collapses to a plain STORE and a row is four
; instructions.
; -----------------------------------------------------------------------------
bnd_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, cx
    add bx, 7
    js .out                     ; wholly left of the band
    cmp cx, [bnd_w]
    jge .out                    ; ...or wholly right of it

    cmp al, FONT_FIRST          ; the glyph, while AL is still the character
    jb .blank
    cmp al, FONT_LAST
    ja .blank
    sub al, FONT_FIRST
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, font_glyphs         ; .lowbss, so the rows are read through SS
    jmp short .have
.blank:
    mov ax, font_zero           ; an unprintable cell is still PAINTED
.have:
    mov si, ax                  ; SI = the glyph's 8 rows

    ; --- the VERTICAL clip, once: BX = first visible glyph row, BP = count --
    xor bx, bx
    or dx, dx
    jns .t_ok
    mov bx, dx                  ; the cell starts above the band: skip that
    neg bx                      ; many of its rows
    cmp bx, 8
    jge .out                    ; ...all of them
.t_ok:
    mov ax, [bnd_h]
    dec ax
    sub ax, dx                  ; AX = the LAST visible glyph row
    js .out                     ; the cell starts below the band
    cmp ax, 7
    jle .b_ok
    mov ax, 7
.b_ok:
    sub ax, bx
    js .out
    inc ax
    mov bp, ax                  ; BP = rows to draw, 1..8
    add si, bx                  ; ...from this glyph row
    add dx, bx                  ; ...at this band row, which is now >= 0

    ; --- the ONE multiply, and the cell's first band byte -------------------
    mov ax, dx
    mul word [bnd_b]            ; a band is 2KB, so the product cannot need DX
    mov di, ax
    mov ax, cx
    and ax, 7                   ; the pen's phase - AND clears AH, so a
    mov dx, ax                  ; NEGATIVE x still gives 0..7 here
    mov ax, cx
    mov cl, 3
    sar ax, cl                  ; AX = the first band byte, SIGNED: floor, so
    mov cx, dx                  ; a pen at -1 is byte -1 phase 7
    add di, ax                  ; DI = where this cell's first row starts

    ; --- the masks, their complements, and the polarity as an XOR -----------
    mov dx, 0FF00h
    shr dx, cl                  ; DH = the high mask, DL = the low one
    mov bx, dx                  ; BX = them, which IS the XOR pair when the
    not dx                      ; band is stored inverted (SPEC.md 5.9.3)...
    cmp byte [bnd_pol], 0
    jne .polok
    xor bx, bx                  ; ...and nothing to flip when it is not
.polok:

    ; --- and the horizontal case, as a choice of LOOP -----------------------
    or cl, cl
    jz .lalign                  ; on the byte grid: one STORE a row
    or ax, ax
    js .lleft                   ; the cell's first byte is left of the band -
    inc ax                      ; and can only be byte -1, since 7 pixels of
    cmp ax, [bnd_b]             ; it are inside
    jl .lboth
    jmp short .lright           ; ...or its second byte is right of the band,
                                ; which likewise makes the first the last one

.lalign:
    mov al, [ss:si]             ; the glyph row (font_glyphs is LOW_SEG)
    inc si
    xor al, bh                  ; the mask is FFh, so the polarity is a plain
    mov [es:di], al             ; complement and the cell IS the byte
    add di, [bnd_b]
    dec bp
    jnz .lalign
    jmp short .out

.lboth:
    mov ah, [ss:si]
    inc si
    xor al, al
    shr ax, cl                  ; AH = the high byte, AL = the low one
    xor ax, bx
    and [es:di], dh
    or [es:di], ah
    inc di
    and [es:di], dl
    or [es:di], al
    dec di
    add di, [bnd_b]
    dec bp
    jnz .lboth
    jmp short .out

.lleft:                         ; only the cell's SECOND byte is in the band
    inc di                      ; ...at band byte 0, since DI is one short
.lleftl:
    mov ah, [ss:si]
    inc si
    xor al, al
    shr ax, cl
    xor ax, bx
    and [es:di], dl
    or [es:di], al
    add di, [bnd_b]
    dec bp
    jnz .lleftl
    jmp short .out

.lright:                        ; ...and only the FIRST byte is in the band
    mov ah, [ss:si]
    inc si
    xor al, al
    shr ax, cl
    xor ax, bx
    and [es:di], dh
    or [es:di], ah
    add di, [bnd_b]
    dec bp
    jnz .lright
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; band_emit_x - hand the band to gfx_blit1 and spend it
; in:  nothing - the colours are band_pen_x's, taken before the band opened
; out: CF = gfx_blit1's own (0 drawn or clipped exactly, 1 refused)
; clobbers: nothing but flags
; -----------------------------------------------------------------------------
band_emit_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    ; THE PEN IS SET HERE AND NOT THROUGH gfx_blit1_pen, because this body is
    ; .cold and that routine is .text: a near call between two segments is
    ; SPEC.md 5.4.2.1's defect, and tools/os88ovlchk.py caught exactly that on
    ; the first build of this file. The two bytes are declared adjacent for
    ; this - one word store, in gfx_blit1_pen's own register order - and the
    ; assertion below is what keeps the second writer honest.
    mov ax, [bnd_pen]
    mov [gfx_b1ink], ax         ; AL = ink, AH = paper (SPEC.md 5.4.2.2)
    mov al, [bnd_pol]           ; ...AND THE WAY UP THIS BAND IS STORED (SPEC.md
    inc al                      ; 5.9.3.1): 1 = a set bit is the INK, 2 = a set
    mov [gfx_b1adp], al         ; bit is the PAPER, and 0 - every other caller
                                ; of gfx_blit1 - means the band is already in
                                ; SCREEN polarity and there is nothing to adapt.
                                ; A band crosses a seam on an extended desktop
                                ; and the two displays there are never the same
                                ; depth (39.19), so no ONE storage can be right
                                ; for both: what the storage IS gets declared,
                                ; and the emit turns the band over per display
                                ; when that display wants the other way up.
                                ; The flag dies with the pen, so 5.4.2.2's "on
                                ; 1bpp the pen is not read" still holds for a
                                ; package
    mov es, [bnd_seg]
    xor si, si
    mov bp, [bnd_b]             ; the stride
    mov ax, [bnd_x]
    mov bx, [bnd_y]
    mov cx, [bnd_w]
    mov dx, [bnd_h]
    call gfx_blit1_x
    mov word [gfx_b1ink], (CBLACK << 8) | CWHITE
                                ; ...AND BACK TO REST, before the lock hold
    mov byte [gfx_b1adp], 0     ; ends: the pen and the polarity are one-shots
                                ; with gfx_unlock's lifetime (SPEC.md 5.4.2.2,
                                ; 5.9.3.1), and a band left declared upside
                                ; down turns the NEXT gfx_blit1 of the hold -
                                ; an icon, a package's blit - over. The two
                                ; literals rather than gfx_blit1_pen for the
                                ; reason above, and `mov` writes no flags, so
                                ; the CF this routine documents survives them
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss
; The band, and it is KERNEL_SEG's .bss rather than COLD_SEG's for
; gfx_blit1_x's reason (SPEC.md 5.4.2.1): these bodies run with DS still the
; kernel's, and a .cold module that reached for its own segment's data would
; be addressing whatever the caller left in DS.
bnd_x       resw 1              ; the band's screen origin - x is a multiple
bnd_y       resw 1              ;   of 8, which gfx_blit1 requires
bnd_w       resw 1              ; its width in pixels...
bnd_b       resw 1              ;   ...and in bytes, which is the blit stride
bnd_h       resw 1              ; its rows
bnd_ink     resb 1              ; 1 = ops SET bits, 0 = they CLEAR them
                                ; (bnd_cell's five scratch words went with the
                                ; sixteen calls a cell it used to make: every
                                ; one of them is a REGISTER now, hoisted out of
                                ; the row loop - SPEC.md 5.9.4)
bnd_pen     resw 1              ; band_pen_x's pair: AL = ink, AH = paper
bnd_pol     resb 1              ; ...and the STORAGE polarity it chose: 1 = the
                                ; band holds every bit the other way up, which
                                ; is what a 1bpp adapter's blit wants when the
                                ; ink is the dark colour (SPEC.md 5.9.3).
                                ; band_emit_x DECLARES it to gfx_blit1, which
                                ; is what makes a band legible on a display
                                ; this was not chosen for (SPEC.md 5.9.3.1)
bnd_sseg    resw 1              ; band_run_x scratch: the CALLER's segment, so
                                ; ES can carry the band and the string in turn
bnd_seg     resw 1              ; THE BAND ITSELF - the base segment of
                                ; band_init's claim, or 0 if the heap refused
                                ; it. Every op writes through ES loaded from
                                ; here; this word is KERNEL_SEG's .bss like the
                                ; rest, because these bodies run with DS still
                                ; ours (SPEC.md 5.4.2.1)

; -----------------------------------------------------------------------------
section .text
; -----------------------------------------------------------------------------
; band_init - take the composer's buffer, once, at boot
; out: nothing; a refusal leaves [bnd_seg] at 0 and band_open_x then refuses
;      every band, which every caller already has to handle
; clobbers: nothing
;
; RESIDENT, and the only byte of this file that is: mem_claim is .text, so a
; .cold body calling it near is SPEC.md 5.4.2.1's defect. Taken at boot rather
; than on first use because a claim during a paint is a claim while the gfx
; lock is held, and because a composer that works until the heap fills is a
; composer whose fallback nobody has ever seen run.
; -----------------------------------------------------------------------------
band_init:
    push ax
    push bx
    push dx
    mov word [bnd_seg], 0       ; -f bin: .bss is not zeroed, and a band opened
    mov byte [bnd_pol], 0       ; before any band_pen_x must still be legible
    mov ax, BAND_KB
    mov bx, MEM_K_BAND
    call mem_claim
    jc .no
    mov [bnd_seg], dx
.no:
    pop dx
    pop bx
    pop ax
    ret
%endif                          ; BANDCOMP
