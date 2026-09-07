; =============================================================================
; os8088 - apps/infones/niband.inc     the tile-cache decoder and the composer
;
; Part of INFONES (SPEC.md 91.5). Derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
; InfoNES fe3295c0, restructured for 8086 real mode.
;
; WHAT THIS FILE FOLLOWS: the tile cache itself - decode a 1KB CHR bank into
; 64 bytes per tile, ONE BYTE PER PIXEL, so that the composer's inner loop is
; a byte load rather than two plane bytes and a shift pair - is InfoNES's
; InfoNES_SetupChr (src/InfoNES.cpp:1103-1191), and the invalidation is its
; `ChrBufUpdate |= 1 << (addr >> 10)` (src/K6502_rw.h:325). The BYTE-per-pixel
; line buffer with tag bits above the palette index is the GBA renderer's
; structural model (gba/src/InfoNES_Advance/InfoNES.cpp:682-940), not the
; desktop renderer's, which writes WORD RGB into a 122,880-byte frame buffer
; that does not exist here.
;
; %included by apps/infones/infones.asm AFTER the compiled C, and by
; hosttest/nimemtest.asm - the boot-sector harness that runs THIS text on a
; real x86 under SS != DS with an ES sentinel and four discipline negative
; controls, so what is gated is the shipping text (SPEC.md 91.14.4).
;
; -----------------------------------------------------------------------------
; WAVE 1 SCOPE: THE DECODER. WAVE 2 ADDS THE COMPOSER ABOVE IT
; -----------------------------------------------------------------------------
; The decoder is here now because it is what the cache IS, because nimemtest
; needs something of this file to gate from wave 1, and because it is the one
; piece whose cost decides whether MMC3 is affordable at all (SPEC.md 91.5.1's
; storm, and `make nibandbench`'s job in wave 3).
;
; Wave 2 adds, into this file: the eight-way-unrolled background run over the
; cache with its two partial end blocks for the fine-X phase and the nametable
; flip at the wrap; the sprite pass into the 272-byte line buffer with the
; transparency test, both flips and the priority/strike tag bits; and the
; serial frame dump nimemtest reads out (SPEC.md 91.14.4).
;
; THE LINE BUFFER IS 272 BYTES WITH THE VISIBLE WINDOW AT +8, and that is not
; slack: the background run writes up to 7 pixels BEFORE the visible line and
; 8 after it, because a non-zero fine X puts a run's two partial end blocks
; across both edges. Bounding it at 256 is a silent write past a bss array on
; every line the screen is scrolled (SPEC.md 91.5.2).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], ...; the answer in AX; BP,
; DS, SS:SP and DF preserved.
; =============================================================================

section .bss

; THE UNPACK TABLE, 2,048 bytes, built ONCE on the first decode.
;
; Entry b holds the eight bytes of the bit pattern of b, MOST SIGNIFICANT
; FIRST, each 0 or 1. A CHR row is two plane bytes, and with this table the
; row is FOUR WORD OPERATIONS rather than eight shift-and-merge pixels:
;
;     out_word = table[p1] * 2   OR   table[p0]
;
; and `* 2` is one `add ax,ax` that cannot carry between the two bytes,
; because every byte in the table is 0 or 1. Two bits per pixel, assembled
; two pixels at a time, with no per-pixel branch at all.
;
; IT IS BSS AND BUILT, NOT .data AND SHIPPED: an initialised 2KB array is
; paid for TWICE, once on the floppy and once in the region, and the loader
; zeroes bss for free (SPEC.md 73.9).
ni_unp:      resb 2048
ni_unp_ok:   resb 1

section .text

; -----------------------------------------------------------------------------
; ni_unp_build - fill the table, once. INTERNAL, near, and it must be called
; with DS = the PACKAGE (which is where its caller stands before it swaps DS
; to a claim): that is what keeps the table's stores off a `cs:` override, and
; a write through `cs:` into the package's own code page is LESSONS.md 13's
; TCG slow path besides.
; -----------------------------------------------------------------------------
ni_unp_build:
    cmp byte [ni_unp_ok], 0
    jne .done
    mov byte [ni_unp_ok], 1
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx                      ; the byte value, 0..255
.byte:
    mov si, bx
    shl si, 1
    shl si, 1
    shl si, 1                       ; SI = b * 8, the entry's base
    mov al, bl
    mov cx, 8
.bit:
    xor dl, dl
    shl al, 1                       ; the MOST significant bit first, which is
    adc dl, 0                       ; the order a CHR row is drawn in
    mov [ni_unp + si], dl
    inc si
    loop .bit
    inc bx
    cmp bx, 256
    jb .byte
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.done:
    ret

; -----------------------------------------------------------------------------
; void ni_chr_decode(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff)
;
; ONE 1KB CHR BANK - 64 tiles of 16 bytes - into 4,096 bytes of the tile
; cache, one byte per pixel, values 0..3.
;
; A tile is eight rows of plane 0 followed by eight rows of plane 1, so row r
; is [si+r] and [si+r+8] and the tile advances by 16.
;
; BP IS USED AS THE SECOND TABLE INDEX and that is deliberate. The 8086's
; memory operands take a base of BX, BP, SI or DI and no other register: SI is
; the source, DI the destination, BX the first index, so BP is the only one
; left - and `[cs:bp+ni_unp]` is legal, the override being what stops the
; default SS from applying (CLAUDE.md's hard rule that `[bp+disp]` addresses
; SS). The caller's BP is on the stack from the entry `push` and comes back at
; the exit `pop`, so the frame is read out ONCE, before BP is spent.
; -----------------------------------------------------------------------------
_ni_chr_decode:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    push bx
    push cx
    push dx
    call ni_unp_build               ; DS is still the package here
    mov ax, [bp+4]                  ; dseg
    mov di, [bp+6]                  ; doff
    mov cx, [bp+8]                  ; sseg
    mov si, [bp+10]                 ; soff
    mov es, ax                      ; cc8086:allow ES is the cache claim,
                                    ;   restored below
    mov ds, cx                      ; ...and DS the CHR claim; the table is
                                    ;   reached through `cs:` from here on
    mov dl, 64                      ; tiles in a 1KB bank
.tile:
    mov dh, 8                       ; rows in a tile
.row:
    mov bl, [si+8]                  ; plane 1
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov bp, bx                      ; BP = table index for plane 1
    mov bl, [si]                    ; plane 0
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [cs:bp+ni_unp]
    add ax, ax                      ; plane 1 is worth 2, and no byte can
    or  ax, [cs:bx+ni_unp]          ; carry into its neighbour because every
    mov [es:di], ax                 ; table byte is 0 or 1
    mov ax, [cs:bp+ni_unp+2]
    add ax, ax
    or  ax, [cs:bx+ni_unp+2]
    mov [es:di+2], ax
    mov ax, [cs:bp+ni_unp+4]
    add ax, ax
    or  ax, [cs:bx+ni_unp+4]
    mov [es:di+4], ax
    mov ax, [cs:bp+ni_unp+6]
    add ax, ax
    or  ax, [cs:bx+ni_unp+6]
    mov [es:di+6], ax
    add di, 8
    inc si
    dec dh
    jnz .row
    add si, 8                       ; past the tile's plane-1 half
    dec dl
    jnz .tile
    pop dx
    pop cx
    pop bx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; WAVE 2 - THE SCANLINE COMPOSER (SPEC.md 91.5)
;
; Three routines and one struct. The struct is the whole argument list: these
; are called 240 times a frame and pushing seven arguments each would be
; seven `push`es and an `add sp` per line for values that change one at a
; time. `ni_cs` is the same idea as `ni_m` one file along - the C's `struct
; ni_comp` IS these offsets, and the two cannot drift without the composer
; reading the wrong word.
;
; THE PIXEL FORMAT (SPEC.md 91.5.2), and it is what makes the merge one
; compare rather than a mask, a shift and a branch:
;
;   ni_line[]  a composed pixel. Bits 0-5 are the NES palette index the DAC is
;              programmed with; BIT 7 is InfoNES's own BACKDROP tag, set when
;              the background pattern value was 0. The tag rides all the way
;              to the framebuffer and costs the present nothing, because the
;              DAC carries the 64 colours FOUR TIMES (SPEC.md 91.5.3).
;
;   ni_spr[]   a sprite scratch pixel, and 0 means NOTHING IS HERE. Bits 0-3
;              are `palette * 4 + value`, which is never 0 because a sprite
;              pattern value of 0 is transparent and is not written at all;
;              bit 4 is the SPRITE-0 tag and bit 5 the BEHIND-BACKGROUND tag.
;              The merge reads the colour through ni_sppal[b & 15].
;
; THE LINE BUFFER IS 272 BYTES WITH THE VISIBLE WINDOW AT +8 and that is not
; slack (SPEC.md 91.5.2): the background lays 33 tiles - 264 pixels - starting
; at `8 - fine X`, so a non-zero fine X puts the first partial block before
; the visible line and the last one after it. A sprite at x = 255 writes
; through offset 8 + 255 + 7 = 270. Bounding either at 256 is a silent write
; past a bss array on every scrolled line.
;
; All cdecl (SPEC.md 73.3), and NONE OF THE THREE TAKES AN ARGUMENT, so BP is
; free for the whole of each of them: the caller's BP is pushed at entry and
; popped at exit and nothing in between reads a frame.
; =============================================================================

section .bss

; --- the composer's inputs, the C's `struct ni_comp ni_cs` -------------------
; EVERY FIELD IS A WORD, deliberately. A struct of words has no packing
; question in either language, and the composer reads a byte field with
; `mov al, [_ni_cs + NIC_FX]` at no cost at all.
_ni_cs:      resw 13
NIC_CACHESEG equ 0                  ; the 32KB tile cache claim
NIC_MACHSEG  equ 2                  ; the machine claim (nametables, palette)
NIC_V        equ 4                  ; the loopy address THIS LINE starts with
NIC_BGTILE   equ 6                  ; the background pattern table, in TILES
NIC_SPTILE   equ 8                  ; ...and the sprite one (8x8 only)
NIC_LINE     equ 10                 ; the visible scanline, 0..239
NIC_HIT      equ 12                 ; -1, or the x the sprite-0 strike was at
NIC_FX       equ 14                 ; fine X, 0..7
NIC_MASK     equ 16                 ; $2001 as it stands
NIC_MIRROR   equ 18                 ; 0 horizontal, 1 vertical
NIC_SPH      equ 20                 ; sprite height, 8 or 16
NIC_OVF      equ 22                 ; a 9th sprite was on this line
NIC_NSPR     equ 24                 ; how many were painted

_ni_line:    resb 272               ; the composed line, visible window at +8
_ni_spr:     resb 272               ; the sprite scratch, same window
_ni_bgpal:   resb 16                ; the C's background LUT: entry p*4+v, and
                                    ;   every p*4+0 is the BACKDROP with bit 7
_ni_sppal:   resb 16                ; ...and the sprite one, entry p*4+v
_ni_oamc:    resb 256               ; OAM, copied into the package once a frame
_ni_fbseg:   resw 1                 ; the framebuffer segment FSI_SEG gave us

; the per-line column table pass 1 writes and pass 2 reads: PAIRS of words,
; the tile's byte offset in the cache and its `palette * 4`
ni_tt:       resw 33 * 2

; pass 1's own scratch. It runs with DS on the MACHINE claim, so everything
; here is reached through the ES override - which is what an `es:` prefix
; costs and why these are words rather than a second struct.
ni_p_row:    resw 1                 ; nametable base + coarse Y * 32
ni_p_atr:    resw 1                 ; attribute base + (coarse Y >> 2) * 8
ni_p_sh:     resw 1                 ; (coarse Y & 2) << 1
ni_p_fy8:    resw 1                 ; fine Y * 8
ni_p_bgt:    resw 1                 ; the background table's byte base
ni_p_nt:     resw 1                 ; the logical nametable, 0..3
ni_p_cx:     resw 1                 ; coarse X, 0..31

; the sprite list pass A builds and pass B paints: four words a sprite
NI_SL_OFF    equ 0                  ; the row's byte offset in the cache
NI_SL_X      equ 2
NI_SL_TAG    equ 4                  ; palette*4 | 0x10 sprite-0 | 0x20 behind
NI_SL_FLIP   equ 6                  ; non-zero: flipped horizontally
NI_SL_STRIDE equ 8
ni_sl:       resw 8 * 4
ni_s_lo:     resw 1                 ; the leftmost and rightmost pixels any
ni_s_hi:     resw 1                 ;   sprite touched, so the merge is a span

section .text

; -----------------------------------------------------------------------------
; void ni_bg_line(void) - the background of ONE line, out of ni_cs.
;
; TWO PASSES, because three segments are wanted at once and there are two.
; Pass 1 stands on the MACHINE claim and reads the nametable and the attribute
; table; pass 2 stands on the TILE CACHE and lays the pixels. The 33-entry
; table between them is what makes that split cost one extra walk of 33 rather
; than a segment reload per pixel.
;
; NESdev's decomposition of `v`, which agnes.c:1071-1082 also uses: coarse X
; is bits 0-4, coarse Y bits 5-9, the nametable bits 10-11 and fine Y bits
; 12-14. The attribute byte for a tile is at 0x23C0 | (nt << 10) |
; ((coarse Y >> 2) << 3) | (coarse X >> 2), two of its bits chosen by
; (coarse Y & 2) and (coarse X & 2).
; -----------------------------------------------------------------------------
_ni_bg_line:
    push bp
    push si
    push di
    push ds
    push es
    push bx
    push cx
    push dx
    cld
    push ds
    pop es                          ; cc8086:allow ES is the PACKAGE's own
                                    ;   segment for both passes - the tables
                                    ;   and the line buffer are ours - and it
                                    ;   is restored below

    ; --- is the background on at all? -------------------------------------
    test byte [_ni_cs + NIC_MASK], 0x08
    jnz .on
    mov al, [_ni_bgpal]             ; entry 0 IS the tagged backdrop
    mov di, _ni_line + 8
    mov cx, 256
    rep stosb                       ; cc8086:allow ES is ours, set above
    jmp .done
.on:
    ; --- unpack v ---------------------------------------------------------
    mov dx, [_ni_cs + NIC_V]
    mov ax, dx
    and ax, 0x1F
    mov [ni_p_cx], ax               ; coarse X
    mov ax, dx
    mov cl, 5
    shr ax, cl
    and ax, 0x1F                    ; coarse Y
    mov bx, ax
    and bx, 2
    shl bx, 1
    mov [ni_p_sh], bx               ; (coarse Y & 2) << 1
    mov bx, ax
    mov cl, 2
    shr bx, cl
    mov cl, 3
    shl bx, cl                      ; (coarse Y >> 2) * 8
    mov [ni_p_atr], bx
    mov cl, 5
    shl ax, cl                      ; coarse Y * 32
    mov [ni_p_row], ax
    mov ax, dx
    mov cl, 10
    shr ax, cl
    and ax, 3
    mov [ni_p_nt], ax               ; the logical nametable
    mov ax, dx
    mov cl, 12
    shr ax, cl
    and ax, 7
    mov cl, 3
    shl ax, cl
    mov [ni_p_fy8], ax              ; fine Y * 8
    mov ax, [_ni_cs + NIC_BGTILE]
    mov cl, 6
    shl ax, cl                      ; the pattern table, in cache BYTES
    mov [ni_p_bgt], ax

    ; --- pass 1: 33 columns of nametable and attribute --------------------
    mov ax, [_ni_cs + NIC_MACHSEG]
    mov ds, ax                      ; cc8086:allow DS is the machine claim for
                                    ;   this pass; the package is reached
                                    ;   through ES and DS is restored below
    mov di, ni_tt
    xor bp, bp                      ; the column, 0..32
.col1:
    call ni_ntbase                  ; -> BX = this column's nametable base,
                                    ;    SI = coarse X within it
    ; the tile number, and its byte offset in the cache
    push bx
    add bx, [es:ni_p_row]
    mov al, [bx+si]
    pop bx
    xor ah, ah
    mov cl, 6
    shl ax, cl                      ; tile * 64 - one tile is 64 cache bytes
    add ax, [es:ni_p_fy8]
    add ax, [es:ni_p_bgt]
    stosw                           ; cc8086:allow ES is ours
    ; the attribute byte, and the two bits of it this tile wants
    add bx, 0x03C0
    add bx, [es:ni_p_atr]
    mov ax, si
    mov cl, 2
    shr ax, cl
    add bx, ax
    mov al, [bx]
    mov cl, [es:ni_p_sh]
    mov dx, si
    and dl, 2
    add cl, dl
    shr al, cl
    and ax, 3
    shl ax, 1
    shl ax, 1                       ; palette * 4, the ni_bgpal index base
    stosw                           ; cc8086:allow
    inc bp
    cmp bp, 33
    jb .col1

    ; --- pass 2: the pixels ------------------------------------------------
    ; `es:` AND IT IS NOT DECORATION: pass 1 left DS on the MACHINE claim, so
    ; a bare [_ni_cs + NIC_CACHESEG] reads the nametable region at that offset
    ; and loads DS with whatever byte pair is there. It assembles, it runs,
    ; and what it draws is 256 pixels of somebody else's memory a line - the
    ; picture is recognisably the right SHAPE, because pass 1 read the right
    ; nametable, and every pixel inside it is noise.
    mov ax, [es:_ni_cs + NIC_CACHESEG]
    mov ds, ax                      ; cc8086:allow DS is the tile cache for
                                    ;   this pass, restored below
    mov di, _ni_line + 8
    sub di, [es:_ni_cs + NIC_FX]    ; the fine-X phase: the first block starts
                                    ;   before the visible window
    mov bp, ni_tt
    xor bh, bh                      ; BH stays 0 for the whole pass: only BL
    mov cx, 33                      ;   is ever written, so the LUT index is
.col2:                              ;   one byte move
    mov si, [es:bp]
    mov dx, [es:bp+2]               ; DL = palette * 4
    add bp, 4
%rep 8
    lodsb                           ; cc8086:allow DS is the cache, set above
    add al, dl
    mov bl, al
    mov al, [es:bx + _ni_bgpal]
    stosb                           ; cc8086:allow ES is ours
%endrep
    dec cx
    jnz .col2

    ; --- the left-column mask ($2001 bit 1) --------------------------------
    test byte [es:_ni_cs + NIC_MASK], 0x02
    jnz .done
    push es
    pop ds                          ; cc8086:allow back to the package for the
                                    ;   eight-byte fill; restored below anyway
    mov al, [_ni_bgpal]
    mov di, _ni_line + 8
    mov cx, 8
    rep stosb                       ; cc8086:allow
.done:
    pop dx
    pop cx
    pop bx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; ni_ntbase - INTERNAL. Column BP of this line: which physical nametable it is
; in and where in it.
;
; in:  BP = the column 0..32, DS = the machine claim, ES = the package
; out: BX = NI_O_VRAM + physical * 1024, SI = coarse X within it (0..31)
; clobbers AX
;
; THE HORIZONTAL WRAP FLIPS NAMETABLE BIT 0 and is what makes a scrolled line
; run off one table onto the next; the two logical tables then fold onto the
; two physical ones by the cartridge's mirroring.
; -----------------------------------------------------------------------------
ni_ntbase:
    mov si, [es:ni_p_cx]
    add si, bp
    mov ax, [es:ni_p_nt]
    cmp si, 32
    jb .in
    xor ax, 1
    and si, 0x1F
.in:
    cmp byte [es:_ni_cs + NIC_MIRROR], 0
    je .horiz
    and ax, 1                       ; vertical: A, B, A, B
    jmp short .fold
.horiz:
    shr ax, 1                       ; horizontal: A, A, B, B
    and ax, 1
.fold:
    mov bx, ax
    mov cl, 10
    shl bx, cl
    add bx, NI_O_VRAM
    ret

; -----------------------------------------------------------------------------
; int ni_spr_line(void) - the sprites of ONE line: evaluate, paint, merge.
;
; out: AX = the x a sprite-0 strike was found at, or -1
;      ni_cs.ovf is set when a NINTH sprite was on the line
;      ni_cs.nspr is how many were painted
;
; THE ORDER IS THE PORT PLAN'S R7 AND IT IS NOT INFONES'S BACK-TO-FRONT PAINT.
; The up-to-eight sprites go into the 272-byte scratch in ASCENDING OAM order
; with FIRST WRITER WINS, so the lowest index owns a pixel exactly as the
; hardware's priority says it does; then ONE merge pass against the untouched
; background resolves front/behind per pixel. Painting back to front would get
; the same picture for opaque sprites and the wrong one the moment a
; behind-tagged sprite is under a front one, and it could not find the
; sprite-0 strike at all, because by then the background it must be compared
; against has been written over.
;
; SPRITE-0 IS FOUND IN THE MERGE: the first pixel where sprite 0 is opaque AND
; the background is opaque, with both left masks respected and x = 255
; excluded (the hardware cannot strike there). What is done with the x is the
; frame loop's - it splits the NEXT frame's budget on the same line
; (SPEC.md 91.5.2), which is the one-frame latency this port states.
; -----------------------------------------------------------------------------
_ni_spr_line:
    push bp
    push si
    push di
    push ds
    push es
    push bx
    push cx
    push dx
    cld
    push ds
    pop es                          ; cc8086:allow the package, for the whole
                                    ;   routine; restored below
    mov word [_ni_cs + NIC_NSPR], 0
    mov word [ni_s_lo], 0xFFFF
    mov word [ni_s_hi], 0
    mov word [_ni_cs + NIC_HIT], -1
    test byte [_ni_cs + NIC_MASK], 0x10
    jz .out                         ; sprites are off: nothing to evaluate,
                                    ;   and nothing was painted last line
                                    ;   either - the merge always clears

    ; --- pass A: evaluate, into ni_sl -------------------------------------
    mov di, ni_sl
    xor bp, bp                      ; the OAM index * 4
    xor dx, dx                      ; how many are on the line
.eval:
    mov al, [ds:bp + _ni_oamc]         ; Y, which OAM stores as top - 1
    xor ah, ah
    mov bx, [_ni_cs + NIC_LINE]
    sub bx, ax
    dec bx                          ; the row within the sprite
    cmp bx, [_ni_cs + NIC_SPH]
    jae .next                       ; unsigned: a negative row is huge
    cmp dx, 8
    jb .room
    mov word [_ni_cs + NIC_OVF], 1  ; THE NINTH. InfoNES's own cap, and the
    jmp .painted                    ;   flag with it
.room:
    mov al, [ds:bp + _ni_oamc + 2]     ; the attribute byte
    mov ah, al
    test al, 0x80                   ; flipped vertically?
    jz .noflipv
    mov cx, [_ni_cs + NIC_SPH]
    dec cx
    sub cx, bx
    mov bx, cx
.noflipv:
    ; the tile, and the row's byte offset in the cache
    push ax                         ; the attribute byte, in AH
    mov al, [ds:bp + _ni_oamc + 1]
    xor ah, ah
    cmp word [_ni_cs + NIC_SPH], 8
    je .h8
    ; 8x16: BIT 0 OF THE TILE PICKS THE PATTERN TABLE and the rest is the
    ; even tile of a vertical pair, the odd one being its bottom half
    mov cx, ax
    and cx, 1
    mov si, cx
    mov cl, 8
    shl si, cl                      ; 0 or 256 tiles
    and ax, 0xFE
    add ax, si
    mov cx, bx
    mov si, cx
    mov cl, 3
    shr si, cl                      ; the bottom half?
    add ax, si
    and bx, 7
    jmp short .toff
.h8:
    add ax, [_ni_cs + NIC_SPTILE]
.toff:
    mov cl, 6
    shl ax, cl                      ; tile * 64
    mov cl, 3
    shl bx, cl                      ; row * 8
    add ax, bx
    mov [di + NI_SL_OFF], ax
    pop ax                          ; the attribute byte back, in AH
    ; the tag: palette * 4, the sprite-0 bit and the behind bit
    mov bl, ah
    and bl, 3
    shl bl, 1
    shl bl, 1
    test ah, 0x20
    jz .front
    or  bl, 0x20
.front:
    or  bp, bp
    jne .nots0
    or  bl, 0x10                    ; SPRITE 0, and only OAM index 0 is
.nots0:                             ;   ever it
    xor bh, bh
    mov [di + NI_SL_TAG], bx
    xor bx, bx
    test ah, 0x40
    jz .noflip
    mov bx, 1
.noflip:
    mov [di + NI_SL_FLIP], bx
    mov al, [ds:bp + _ni_oamc + 3]     ; X
    xor ah, ah
    mov [di + NI_SL_X], ax
    ; the merge's span
    cmp ax, [ni_s_lo]
    jae .nolo
    mov [ni_s_lo], ax
.nolo:
    add ax, 7
    cmp ax, [ni_s_hi]
    jbe .nohi
    mov [ni_s_hi], ax
.nohi:
    add di, NI_SL_STRIDE
    inc dx
.next:
    add bp, 4
    cmp bp, 256
    jb .eval
.painted:
    mov [_ni_cs + NIC_NSPR], dx
    or  dx, dx
    jz .out

    ; --- pass B: paint, ascending, first writer wins ------------------------
    mov ax, [es:_ni_cs + NIC_CACHESEG]
    mov ds, ax                      ; cc8086:allow the tile cache, restored
                                    ;   below; the scratch is reached through
                                    ;   ES, which is ours
    mov bp, ni_sl
.paint:
    mov si, [es:bp + NI_SL_OFF]
    mov di, [es:bp + NI_SL_X]
    add di, _ni_spr + 8
    mov bx, [es:bp + NI_SL_TAG]     ; BL = the tag, BH = 0
    cmp word [es:bp + NI_SL_FLIP], 0
    jne .pflip
    mov cx, 8
.pl:
    lodsb                           ; cc8086:allow DS is the cache
    or  al, al
    jz .pskip                       ; pattern value 0 is TRANSPARENT and is
                                    ;   not a pixel at all
    cmp byte [es:di], 0
    jne .pskip                      ; FIRST WRITER WINS: a lower OAM index
                                    ;   already owns this pixel
    or  al, bl
    mov [es:di], al                 ; cc8086:allow
.pskip:
    inc di
    dec cx
    jnz .pl
    jmp short .pnext
.pflip:
    add si, 7
    mov cx, 8
.pfl:
    mov al, [si]
    dec si
    or  al, al
    jz .pfskip
    cmp byte [es:di], 0
    jne .pfskip
    or  al, bl
    mov [es:di], al                 ; cc8086:allow
.pfskip:
    inc di
    dec cx
    jnz .pfl
.pnext:
    add bp, NI_SL_STRIDE
    dec dx
    jnz .paint
    push es
    pop ds                          ; cc8086:allow back to the package for the
                                    ;   merge, and restored by the pop below

    ; --- the merge, over the span the sprites actually touched --------------
    mov si, [ni_s_lo]
    mov di, [ni_s_hi]
    cmp di, 255
    jbe .hiok
    mov di, 255                     ; a sprite at x = 250 writes past the
.hiok:                              ;   visible line and those pixels are not
                                    ;   merged - but they ARE cleared, by the
                                    ;   pass above having written them and
                                    ;   the loop below stopping short. The
                                    ;   clear that matters is the one over the
                                    ;   merged span; the tail is cleared by
                                    ;   .tail
    mov dx, si                      ; DX walks the clear, SI the merge
.merge:
    cmp si, di
    ja  .tail
    mov bx, si
    mov al, [bx + _ni_spr + 8]
    or  al, al
    jz  .mnext
    test byte [_ni_cs + NIC_MASK], 0x04
    jnz .mvis
    cmp si, 8
    jb  .mnext                      ; the sprite left-column mask
.mvis:
    mov ah, [bx + _ni_line + 8]     ; bit 7 = the background was the backdrop
    test al, 0x10                   ; sprite 0?
    jz  .nohit
    test ah, 0x80
    jnz .nohit                      ; ...over a transparent background is not
                                    ;   a strike
    cmp word [_ni_cs + NIC_HIT], -1
    jne .nohit
    cmp si, 255
    je  .nohit                      ; the hardware cannot strike at x = 255
    mov [_ni_cs + NIC_HIT], si
.nohit:
    test al, 0x20                   ; behind the background?
    jz  .draw
    test ah, 0x80
    jz  .mnext                      ; ...and the background is opaque, so the
                                    ;   background wins this pixel
.draw:
    and al, 0x0F
    push bx
    mov bl, al
    xor bh, bh
    mov al, [bx + _ni_sppal]
    pop bx
    mov [bx + _ni_line + 8], al
.mnext:
    inc si
    jmp short .merge
.tail:
    ; EVERY BYTE ANY SPRITE WROTE IS PUT BACK TO 0, merged or not - the
    ; scratch is not cleared anywhere else, and a stale byte is a sprite that
    ; reappears on a line it is not on
    mov si, [ni_s_hi]
    mov di, dx
.clr:
    cmp di, si
    ja  .out
    mov bx, di
    mov byte [bx + _ni_spr + 8], 0
    inc di
    jmp short .clr
.out:
    mov ax, [_ni_cs + NIC_HIT]
    pop dx
    pop cx
    pop bx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void ni_present13(int row) - ONE composed line into mode 13h's framebuffer.
;
; 256 pixels of 320, so the picture is centred with 32 columns of border each
; side, and the row is the DESTINATION row the drop table chose (SPEC.md
; 91.6.2) - never the source line. One `rep movsw` of 128 words, which is what
; makes 13h the default mode on every tier (91.6.1).
;
; THE TAG BITS GO DOWN WITH THE PIXELS AND COST NOTHING, because the DAC
; carries the 64 NES colours four times over (91.5.3). A masking pass here
; would be 256 more instructions a line for a picture that is already right.
; -----------------------------------------------------------------------------
_ni_present13:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push bx
    push cx
    push dx
    cld
    mov es, [_ni_fbseg]             ; cc8086:allow ES is the framebuffer the
                                    ;   bracket owns (SPEC.md 53.7), and it is
                                    ;   restored below
    mov ax, [bp+4]
    mov bx, ax
    mov cl, 6
    shl ax, cl
    mov cl, 8
    shl bx, cl
    add ax, bx                      ; row * 320, with no `imul`
    add ax, 32                      ; ...and the border
    mov di, ax
    mov si, _ni_line + 8
    mov cx, 128
    rep movsw                       ; cc8086:allow DS is ours, ES the screen
    pop dx
    pop cx
    pop bx
    pop es
    pop di
    pop si
    pop bp
    ret
