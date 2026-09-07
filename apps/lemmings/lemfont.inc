; =============================================================================
; os8088 - apps/lemmings/lemfont.inc      TEXT IN A FOREIGN MODE (SPEC.md 85.7)
;
; %included by apps/lemmings/lemmings.asm AFTER lemblit.inc, whose lb_rpix()
; and lb_kind it uses. CLAUDE.md's section discipline and the cdecl contract in
; lemmask.inc's header apply here word for word.
;
; NO KERNEL FONT SLOT IS LEGAL INSIDE THE BRACKET. os88_font_run(),
; os88_font_char() and every other drawing slot are REFUSED there (SPEC.md
; 53.1, apps/cc/os88.h), so the status line is lettered by this file out of
; MAIN.DAT's own glyphs - which is also what makes it look like Lemmings and
; not like the desktop.
;
; THE STATUS FONT is MAIN.DAT section 6 offset 0x1900: 38 glyphs, 8x16, THREE
; planes, in the order `%` `0`-`9` `-` `A`-`Z`
; (lemmings_3ds/doc/data/lemmings_main_dat_file_format.txt sections 5 and 9,
; which print the set literally). There is no lowercase and no space glyph, and
; that is why the index map below is a FOUR-ARM CASE with a fourth arm that
; draws an 8x16 BLACK CELL: Lemmix's Game.SkillPanel.pas DrawNewStr does
; exactly that for the space, for the template's own '.' and for anything else,
; after UpCase'ing both the old and the new character - so the whole line is
; uppercase on the glass whatever case it is stored in.
;
; THE PURPLE 16x16 FONT of the preview and postview screens is WAVE 5's, with
; those two screens (SPEC.md 92.4.3). It is a second index map over the same
; per-pixel plotter and nothing here changes to accept it.
;
; IT DOES NOT PLOT PER PIXEL, AND THE FIRST VERSION DID. That version's header
; estimated a plot at "~50 cycles on an 8088, so a whole 40-character line is
; ~5,120 plots = ~54 ms" and a five-cell field at "~7 ms". THE BENCH SAID
; OTHERWISE: SPEC.md 92.7.1 conclusion 4 and PERFORMANCE.md Set 116 measured a
; five-cell field at 393 ms on VGA and 349 on CGA - 614 us a plot, ~2,900 cycles,
; FIFTY-EIGHT TIMES the estimate - which makes the whole line 3.1 s and puts the
; per-tick OUT / IN / clock update seven ticks behind the frame it belongs to.
; The estimate was a guess and the guess was wrong by nearly two orders of
; magnitude, which is PERFORMANCE.md rule 4 in one paragraph.
;
; SO THE CELL IS WRITTEN AS BYTES, NOT AS PIXELS, and the glyph's own storage is
; what makes that nearly free:
;
;   VGA        the three glyph planes ARE VGA planes 0..2 of the cell (the
;              colour's bit p is source plane p - the order the format
;              document gives and the order the deleted per-pixel decoder built),
;              and a cell column is col*8 - byte aligned by construction. So a
;              plane pass is `map mask 1<<p` and SIXTEEN byte stores, and plane
;              3 is sixteen zeros. 64 stores and 4 `out` pairs a cell, against
;              128 read-modify-writes and 1,280 `out`s.
;   CGA/Herc   the three planes OR to one 8-bit "ink" mask a row - the class map
;              is SOLID for 1..7 and there is no other colour in this font - and
;              lb_dbl doubles a nibble to a byte, which is exactly the 2-bits-a
;              -pixel CGA cell AND the doubled Hercules one. Two byte stores a
;              row, 32 a cell.
;
; AND THAT IS WHAT WAS MEASURED, not what is hoped for: the same five-cell
; field is 8.6 ms on VGA and 16.1 ms on CGA (PERFORMANCE.md Set 116's last row,
; SPEC.md 92.7.1's table and its conclusion 4, both of which carry the first
; pass beside it). 46x on VGA, 22x on CGA, and the per-tick update is a fifth
; of a tick rather than seven of them.
;
; THE LINE IS STILL DELTA-DRAWN - Lemmix's own rule, and the five field write
; offsets in SPEC.md 92's authority table are what make it cheap - so an
; ordinary tick letters the OUT and IN counts and the clock, about five cells.
; Cross-referenced BOTH WAYS with SPEC.md 92.7.1 conclusion 4 so the estimate
; and the measurement cannot drift apart again.
;
; IT IS SAFE INSIDE A lem_r_batch() BRACKET, and it was not: the VGA arm sets
; write mode 0 on the way in and used to leave the card in it, with the Map
; Mask at plane 3 alone, while [lb_batch] still said a bracket was open - so
; every later lb_rpix in that bracket wrote raw bit-plane data. See _lem_f_cell
; .out, which restores what it found.
;
; ATTRIBUTION. The glyphs are the original's, read off MAIN.DAT by
; tools/os88lem.py and never committed; the index map is the rule Lemmix's
; DrawNewStr implements. Lemmings is (C) 1991 DMA Design / Psygnosis and
; README.TXT carries the full attribution.
; =============================================================================

section .text

LEM_FGLYPHS equ 38                  ; '%' 0-9 '-' A-Z
LEM_FPLANE  equ 16                  ; bytes per plane of one 8x16 glyph
LEM_FCELL   equ 48                  ; ...and three planes of them

; -----------------------------------------------------------------------------
; int lem_f_index(int ch) - the glyph index of a character, or -1 for "an 8x16
; black cell". THE FOUR ARMS ARE Lemmix Game.SkillPanel.pas DrawNewStr's, in
; its own order: '%' -> 0, '0'..'9' -> ord - ord('0') + 1, '-' -> 11,
; 'A'..'Z' -> ord - ord('A') + 12, giving 0..37; anything else - the space and
; the template's own '.' included - falls to the black cell.
;
; The character is UPPERCASED first, because the font has no lowercase glyph
; and Lemmix's mixed-case strings are Lemmix's presentation, undone by its own
; UpCase at draw time.
; -----------------------------------------------------------------------------
_lem_f_index:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    and ax, 0xFF
    cmp al, 'a'
    jb .nolc
    cmp al, 'z'
    ja .nolc
    sub al, 32
.nolc:
    cmp al, '%'
    jne .dig
    xor ax, ax
    jmp .out
.dig:
    cmp al, '0'
    jb .dash
    cmp al, '9'
    ja .dash
    sub al, '0' - 1
    xor ah, ah
    jmp .out
.dash:
    cmp al, '-'
    jne .alpha
    mov ax, 11
    jmp .out
.alpha:
    cmp al, 'A'
    jb .none
    cmp al, 'Z'
    ja .none
    sub al, 'A' - 12
    xor ah, ah
    jmp .out
.none:
    mov ax, -1
.out:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_f_cell(unsigned seg, unsigned off, int col, int row, int ch)
;
; ONE 8x16 CELL of the status line, at PANEL character column `col` and pixel
; row `row`. `off` is the first byte of the 38-glyph bank inside LEMMAIN.
;
; A ZERO PIXEL IS WRITTEN TOO, and deliberately: that is what erases the
; character that was there and what draws the black cell of lem_f_index's fourth
; arm. Nothing is written twice - each plane byte, or each shadow byte, is
; stored exactly once.
;
; Locals: [bp-2] glyph index, [bp-4] the glyph's first byte, [bp-6] plane,
; [bp-8] the destination offset of row 0.
; -----------------------------------------------------------------------------
_lem_f_cell:
    push bp
    mov bp, sp
    sub sp, 8
    push si
    push di
    push es
    push ds
    ; the cell has to be inside the 320x40 panel, and it is byte aligned in
    ; every backend by construction (x = col*8)
    mov ax, [bp+8]                  ; col
    cmp ax, 0
    jl .out
    cmp ax, 40
    jge .out
    mov ax, [bp+10]                 ; row
    cmp ax, 0
    jl .out
    add ax, 16
    cmp ax, 40
    jg .out

    push word [bp+12]
    call _lem_f_index
    add sp, 2
    mov [bp-2], ax
    ; the glyph's first byte inside the bank, or -1 for "the black cell"
    cmp ax, 0
    jl .noglyph
    mov bx, LEM_FCELL
    mul bx
    add ax, [bp+6]
    mov [bp-4], ax
    jmp .have
.noglyph:
    mov word [bp-4], -1
.have:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .shadow

    ; --- VGA: three planes copied straight in, the fourth zeroed -----------
    ; write mode 0 with the whole bit mask, which is the ambient state every
    ; other writer in lemblit.inc leaves behind - stated rather than assumed,
    ; because a caller could be inside lb_batch's write mode 2. .out restores
    ; that bracket if there was one, so this is safe in both ambients.
    mov ah, 0
    mov al, 5
    call lb_gc
    mov ah, 0xFF
    mov al, 8
    call lb_gc
    mov ax, [bp+10]
    call lem_rowoff                 ; DI = row * 200, the VGA panel stride
    add di, [bp+8]                  ; + the byte column
    mov [bp-8], di
    mov ax, [lb_fb]                 ; ...before DS moves: see lb_pplane
    mov es, ax
    mov word [bp-6], 0
.vplane:
    mov ax, 1
    mov cx, [bp-6]
    shl ax, cl
    mov ah, al
    mov al, 2                       ; Map Mask: this plane only
    call lb_seq
    ; plane 3 of a three-plane glyph, and every plane of a black cell, is 16
    ; zero bytes; the rest is a straight copy of the glyph's own plane.
    cmp word [bp-2], 0
    jl .vzero                       ; the black cell: every plane is zeros
    cmp word [bp-6], 3
    je .vzero                       ; a three-plane glyph leaves plane 3 clear
    mov si, [bp-4]
    mov ax, [bp-6]
    mov cl, 4
    shl ax, cl                      ; plane * LEM_FPLANE (16)
    add si, ax
    mov ax, [bp+4]
    mov ds, ax
    mov di, [bp-8]
    mov cx, 16
.vcopy:
    mov al, [si]
    mov [es:di], al
    inc si
    add di, LEM_VSTRIDE
    loop .vcopy
    push cs
    pop ds
    jmp .vnext
.vzero:
    mov di, [bp-8]
    mov cx, 16
    xor al, al
.vclr:
    mov [es:di], al
    add di, LEM_VSTRIDE
    loop .vclr
.vnext:
    inc word [bp-6]
    cmp word [bp-6], 4
    jb .vplane
    jmp .out

    ; --- CGA and Hercules: one ink mask a row, doubled into two bytes ------
    ; The class map has one arm for this font: every non-zero colour of a
    ; 3-plane glyph is 1..7, which lb_spix draws SOLID, so the three planes OR
    ; to the row's ink and lb_dbl turns a nibble into the byte both backends
    ; want (CGA two bits a pixel, Hercules two doubled pixels).
.shadow:
    cmp word [bp-2], 0
    jl .sblank
    mov si, [bp-4]
    mov ax, [bp+4]
    mov es, ax                      ; the CLAIM in ES, so DS stays ours and
    xor di, di                      ; lf_mask below is reachable
.smrow:
    mov al, [es:si]
    or al, [es:si+LEM_FPLANE]
    or al, [es:si+2*LEM_FPLANE]
    mov [lf_mask+di], al
    inc si
    inc di
    cmp di, 16
    jb .smrow
    jmp .sdraw
.sblank:
    xor di, di
.sbrow:
    mov byte [lf_mask+di], 0
    inc di
    cmp di, 16
    jb .sbrow
.sdraw:
    mov ax, [lb_shadow]
    mov es, ax
    mov word [bp-6], 0
.srow:
    mov ax, [bp+10]
    add ax, [bp-6]
    add ax, LEM_VIEWH               ; the panel occupies shadow rows 160..199
    ; the row is marked dirty WHOLE, for lb_spix's own reason: two byte stores
    ; against a per-cell span merge, and the panel's forty rows are redrawn as
    ; a block or not at all
    mov bx, ax
    mov byte [lb_dlo+bx], 0
    mov byte [lb_dhi+bx], LEM_SSTRIDE - 1
    call lb_rowoff8                 ; DI = row * 80
    mov ax, [bp+8]
    shl ax, 1                       ; a cell is TWO shadow bytes on both cards
    add di, ax
    mov bx, [bp-6]
    mov al, [lf_mask+bx]
    mov ah, al
    and ah, 0x0F                    ; the RIGHT four pixels
    mov cl, 4
    shr al, cl                      ; ...and the left four
    push ax
    xor bx, bx
    mov bl, al
    mov dl, [lb_dbl+bx]             ; doubled: the first byte
    pop ax
    xor bx, bx
    mov bl, ah
    mov dh, [lb_dbl+bx]             ; ...and the second
    mov [es:di], dl
    mov [es:di+1], dh
    inc word [bp-6]
    cmp word [bp-6], 16
    jb .srow
.out:
    pop ds
    ; THE CALLER'S BRACKET, PUT BACK. The VGA arm above set write mode 0 and a
    ; whole Bit Mask on the way in and leaves the Map Mask at 0x08 (the last
    ; .vplane pass) on the way out - which is fatal INSIDE a lem_r_batch()
    ; bracket, because lb_rpix skips lb_batch_on whenever [lb_batch] is
    ; non-zero: every later plot in that bracket would write raw bit-plane
    ; data into plane 3 alone. No caller does it today (lem_frame_panel and
    ; lem_frame_first letter outside any bracket), so this is a trap being
    ; disarmed rather than a defect being fixed - but wave 3 redraws the status
    ; line every tick beside the minimap and the view rectangle, which is
    ; exactly the call order that lands it. lb_batch_on is the two GC/Seq
    ; writes lb_batch_in would have made, and it returns at its first compare
    ; on the two shadow backends. DS FIRST: lb_batch_on reads [lb_kind].
    cmp word [lb_batch], 0
    je .nobatch
    call lb_batch_on
.nobatch:
    pop es
    pop di
    pop si
    add sp, 8                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

section .bss
lf_mask:    resb 16                 ; one cell's ink, a byte a row (the shadow
                                    ; backends only)
section .text

; -----------------------------------------------------------------------------
; void lem_f_run(unsigned seg, unsigned off, int col, int row,
;                const char *s, int n)
;
; `n` cells from `s`, left to right. It exists so that a delta-drawn field is
; ONE call rather than five - the same reason os88_font_run() exists on the
; desktop side (SPEC.md 6.1) - and the caller may pass a shorter string than
; `n`, in which case the rest are the fourth arm's black cells, which is what
; Lemmix's PadRight does.
; -----------------------------------------------------------------------------
_lem_f_run:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+12]
    xor di, di
.cell:
    cmp di, [bp+14]
    jge .out
    xor ax, ax
    cmp si, 0
    je .blank
    mov al, [si]
    cmp al, 0
    jne .have
    mov si, 0                       ; the string ended: pad with black cells
    xor ax, ax
.blank:
.have:
    push ax
    mov ax, [bp+10]
    push ax
    mov ax, [bp+8]
    add ax, di
    push ax
    push word [bp+6]
    push word [bp+4]
    call _lem_f_cell
    add sp, 10
    cmp si, 0
    je .next
    inc si
.next:
    inc di
    jmp .cell
.out:
    pop di
    pop si
    pop bp
    ret

section .text
