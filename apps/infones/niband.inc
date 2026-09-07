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
