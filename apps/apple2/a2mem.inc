; =============================================================================
; os8088 - apps/apple2/a2mem.inc    the RAM/ROM claim accessors and the movers
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
; apps/apple2/ is GPL-2-or-later (apps/apple2/COPYING); c64mem.inc, itself
; rcmem.inc's shape (SPEC.md 74), is what this file is modelled on.
;
; %included by apps/apple2/apple2.asm after the compiled C, and by
; hosttest/a2memtest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS and an ES sentinel, with four negative controls
; (APPLE2-SPEC section 3.4), so what is tested is the shipping text.
;
; WHY THESE EXIST. The Apple's 48K lives in a heap claim of its own and its
; ROM is a PART in a second segment (APPLE2-SPEC section 3.1); the C side -
; the flush, the loader, the register files - has to read and write both. The
; SDK's os88_peek/os88_poke are FAR calls through the API cell (~47 us each,
; SPEC.md 73.11); a screen row read through them would be a program. So the
; package carries its own.
;
; THE RULE IS WIDER THAN THE MOVERS. Data in either claim is passed as an
; explicit (segment, offset) PAIR, never as a C pointer: a C pointer is a
; package-DS offset and nothing else, so "a pointer to a screen row" is a
; defect that assembles cleanly. Every `movs`/`stos`/`cmps` below loads DS/ES
; on purpose, restores both, leaves DF clear, and carries the `cc8086:allow`
; marker with its reason - the convention the gate would ask for if it read
; this file (it reads the compiled C only, SPEC.md 73.6).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. The claims are _a2_m+AM_RAMSEG and AM_ROMSEG (a2cpu.inc), banked by
; the C at launch.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; int a2_rd(unsigned a) - the byte at Apple address a, AS RAM.
;
; This is the FAST PATH of APPLE2-SPEC section 3.2 with no ladder: the C side
; uses it for the display page and for its own pokes, which are RAM by
; construction. The soft switches, the slot ladder and the ROM belong to the
; CORE's read ladder (a2cpu.inc, wave 2), and they are not this routine.
; -----------------------------------------------------------------------------
_a2_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_a2_m+AM_RAMSEG]
    mov al, [es:bx]
    xor ah, ah                      ; a shim that answers in AL must clear AH
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_rd16(unsigned a) - the little-endian word at a (a = $FFFF wraps).
; -----------------------------------------------------------------------------
_a2_rd16:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_a2_m+AM_RAMSEG]
    mov al, [es:bx]
    inc bx
    mov ah, [es:bx]
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_wr(unsigned a, int v) - one byte into RAM, and ONE BIT into the
; 256-page dirty bitmap (APPLE2-SPEC section 7.5). That `or` is the stated
; per-write cost of this whole design, and it is what lets the flush see a
; display page change without the C reading a byte of it.
;
; THE WRITE FENCE IS $C000 AND NOT $D000. On a 48K II+ everything above
; $BFFF is soft switches, slot space or ROM, and a write to any of it is
; DROPPED here - the core's own write ladder calls out to a2io.c for
; $C000-$C0FF before it ever reaches this routine. That is also what makes
; the core's scratch at $CF00 safe with ZERO stated deviations
; (section 3.3), where the C64 needed two.
; -----------------------------------------------------------------------------
_a2_wr:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    cmp bx, A2_IO_BASE
    jae .out                        ; $C000 and above is not RAM on this
                                    ; machine, in either direction
    mov es, [_a2_m+AM_RAMSEG]
    mov al, [bp+6]
    mov [es:bx], al
    mov byte [es:A2_SCR_BASE + A2_SCR_ANY], 1
    ; the WRITE WINDOW: the lowest and highest address written since the last
    ; flush, and what turns a one-byte poke back into a one-CELL compose
    ; instead of the eight scan lines a 256-byte page would say.
    ;
    ; IT IS TAKEN OVER THE WATCH RANGE, NOT OVER THE ADDRESS SPACE. Widening
    ; it on every write is exact while the only writer is the C's own poke and
    ; useless the day the core runs: a JSR writes the stack at $01xx and a
    ; BASIC statement writes zero page and the variables above $0800, so one
    ; slice leaves the window spanning the whole display page and the flush's
    ; per-row intersection always answers "all forty cells". The C writes the
    ; LIVE DISPLAY PAGE here from a2_watch_set.
    cmp bx, [es:A2_SCR_BASE + A2_SCR_WATLO]
    jb .nowin
    cmp bx, [es:A2_SCR_BASE + A2_SCR_WATHI]
    ja .nowin
    cmp bx, [es:A2_SCR_BASE + A2_SCR_WLO]
    jae .nolo
    mov [es:A2_SCR_BASE + A2_SCR_WLO], bx
.nolo:
    cmp bx, [es:A2_SCR_BASE + A2_SCR_WHI]
    jbe .nowin
    mov [es:A2_SCR_BASE + A2_SCR_WHI], bx
.nowin:
    ; the dirty bit: page = a >> 8, byte = page >> 3, bit = 0x80 >> (page & 7)
    mov al, bh                      ; the page
    mov cl, al
    and cl, 7
    shr al, 1
    shr al, 1
    shr al, 1
    mov bl, al
    xor bh, bh
    add bx, A2_SCR_BASE + A2_SCR_DIRTY
    mov al, 0x80
    shr al, cl
    or [es:bx], al
.out:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_dirty(unsigned a) - mark a's page dirty WITHOUT writing anything.
;
; What a mode change needs: PAGE2 makes a page the flush has never looked at
; the live one, and every byte in it is news even though nothing wrote to it.
; -----------------------------------------------------------------------------
_a2_dirty:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:A2_SCR_BASE + A2_SCR_ANY], 1
    mov al, bh
    mov cl, al
    and cl, 7
    shr al, 1
    shr al, 1
    shr al, 1
    mov bl, al
    xor bh, bh
    add bx, A2_SCR_BASE + A2_SCR_DIRTY
    mov al, 0x80
    shr al, cl
    or [es:bx], al
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_wrote(void) - has anything been written since the last take?
;
; ONE BYTE, ONE READ. The wake has to decide whether to flush BEFORE it has
; read the page bitmap, and reading 32 bytes of scratch through a near call
; each to find out would cost more than the decision is worth on an 8088. One
; `mov byte [es:..], 1` on the write path (a2_wr, above) answers it here in
; one read, and a2_dirty_take clears it in the same pass that takes the
; bitmap.
;
; WITHOUT IT THE WAKE NEVER FLUSHES AT ALL, which is not a subtle failure and
; was found by the host harness on its first run: the C's own `a2_dirty_any`
; is set by a2_dirty_scan, a2_dirty_scan runs inside the flush, and the flush
; runs only when a2_dirty_any is set - so a poke that never reached the C side
; sat in the emulated machine's memory with the glass stale and the wake
; idling. Every cost row read 0 ms and 0 blits, which looked like a very fast
; emulator.
; -----------------------------------------------------------------------------
_a2_wrote:
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov al, [es:A2_SCR_BASE + A2_SCR_ANY]
    xor ah, ah
    pop es
    ret

; -----------------------------------------------------------------------------
; void a2_watch_set(unsigned lo, unsigned hi) - the range the write window is
; taken over (APPLE2-SPEC section 7.5). The C writes the LIVE DISPLAY PAGE.
; -----------------------------------------------------------------------------
_a2_watch_set:
    push bp
    mov bp, sp
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov ax, [bp+4]
    mov [es:A2_SCR_BASE + A2_SCR_WATLO], ax
    mov ax, [bp+6]
    mov [es:A2_SCR_BASE + A2_SCR_WATHI], ax
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_scratch_clear(void) - the whole scratch page to a known state.
;
; Called once at launch and again on a power cycle. The window is set EMPTY -
; WLO above WHI - which is what "nothing has been written" means to the flush,
; and is why WLO starts at $FFFF and not at 0.
; -----------------------------------------------------------------------------
_a2_scratch_clear:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [_a2_m+AM_RAMSEG]
    mov di, A2_SCR_BASE
    xor ax, ax
    mov cx, 128
    rep stosw                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; here on purpose and restored below
    mov word [es:A2_SCR_BASE + A2_SCR_WLO], 0xFFFF
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_dirty_take(unsigned char *dst36)
;
; The 32-byte page bitmap and the write window's two words into the package,
; AND the scratch reset, in ONE call - about 190 us. The C64 measured the
; alternative at ~50 near thunks at ~38 us each = ~1.8 ms a flush spent before
; a pixel was decided, and invisible to a call-counting cost model
; (APPLE2-SPEC section 7.5).
;
; dst36[0..31]  the page bitmap
; dst36[32..33] the window's low address
; dst36[34..35] ...and its high
; -----------------------------------------------------------------------------
_a2_dirty_take:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov bx, [_a2_m+AM_RAMSEG]       ; ...through the PACKAGE's DS, BEFORE it
                                    ; goes: [_a2_m] is a package-DS reference
    mov di, [bp+4]                  ; dst is a package pointer, so ES = DS
    mov ax, ds
    mov es, ax
    mov ds, bx                      ; ...AFTER every [bp+n] is read: [bp+n] is
                                    ; SS-relative, so DS may go now
    mov si, A2_SCR_BASE + A2_SCR_DIRTY
    mov cx, 16
    rep movsw                       ; cc8086:allow both segments loaded here
                                    ; on purpose and both restored below
    mov si, A2_SCR_BASE + A2_SCR_WLO
    movsw                           ; the window's two words follow the bitmap
    movsw                           ; in dst[32..35]
    mov es, bx                      ; ES = the RAM claim: the same pass CLEARS
    mov di, A2_SCR_BASE + A2_SCR_DIRTY
    xor ax, ax
    mov cx, 16
    rep stosw                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; two lines above and restored below
    mov word [A2_SCR_BASE + A2_SCR_WLO], 0xFFFF     ; DS is the claim here too
    mov [A2_SCR_BASE + A2_SCR_WHI], ax
    mov [A2_SCR_BASE + A2_SCR_ANY], al
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_rom_rd(unsigned off) - one byte out of the ROM PART, by offset in
; APPLE2-SPEC section 1.4's layout (0 = $D000, $3000 = CHARGEN, $3800 = P5).
; -----------------------------------------------------------------------------
_a2_rom_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_a2_m+AM_ROMSEG]
    mov al, [es:bx]
    xor ah, ah
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_chargen(void *dst, unsigned seg, unsigned off, unsigned n)
;
; THE CHARACTER-GENERATOR DECODE'S MOVER (APPLE2-SPEC section 7.3). Copy n
; bytes out of the ROM part into the package's own bss, masking each to seven
; bits.
;
; WHY THE MASK IS THE WHOLE DECODE. AppleWin's NTSC_CharSet.cpp reads
; `rom[i*8 + (y&7)]` and XORs $7F where the entry is in the low 1KB and bit 7
; is clear - which turns the ROM's own storage into the normal form for blocks
; $40, $80 and $C0 and the inverse form for block $00. Reading block $C0
; instead makes that XOR unnecessary: those 64 characters are already the
; NORMAL form and bit 7 carries nothing, so masking it off is the decode.
; The inverse and the flash are then ONE per-cell XOR mask in the composer,
; which is why 64 glyphs of 8 bytes cover the ROM's 128 distinct bitmaps
; (tools/a2ref.py --romshape asserts that count, so the decision is checkable
; rather than remembered).
;
; AND THE BIT ORDER IS ALREADY RIGHT. In the character ROM bit 6 is the
; LEFTMOST pixel, which is the framebuffer's own order (SPEC.md 5.4.2, MSB
; first) - so a decoded glyph goes into the shift accumulator as it stands.
; HI-RES DATA IS THE OTHER WAY UP (bit 0 leftmost) and is what the 128-entry
; reverse table exists for; the two are not the same convention and this
; comment is the only place that says so.
; -----------------------------------------------------------------------------
_a2_chargen:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov di, [bp+4]                  ; dst - a package pointer, so ES = DS
    mov ax, ds
    mov es, ax
    mov cx, [bp+10]                 ; n, before DS goes
    mov si, [bp+8]                  ; off
    mov ax, [bp+6]                  ; seg
    mov ds, ax
.b:
    lodsb                           ; cc8086:allow DS is the ROM part, loaded
                                    ; here on purpose and restored below
    and al, 0x7F
    stosb                           ; cc8086:allow ES is the package's own DS
    loop .b
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_zfill(unsigned a, int v, unsigned n) - n bytes of v into the Apple's
; RAM at a. The POWER-ON PATTERN is a2_zpower's above, not this one; this is
; the flat fill the harness lays a display page down with and the one Load
; Program clears a program's tail with (APPLE2-SPEC section 12).
;
; It does NOT touch the dirty bitmap: a fill is followed by the C marking what
; it filled, in one call rather than in n.
; -----------------------------------------------------------------------------
_a2_zfill:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [_a2_m+AM_RAMSEG]
    mov di, [bp+4]
    mov ax, [bp+6]
    mov cx, [bp+8]
    jcxz .out
    rep stosb                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; here on purpose and restored below
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_zpower(unsigned a, unsigned n) - AppleWin's `FF FF 00 00` POWER-ON
; PATTERN over n bytes of the Apple's RAM at a (APPLE2-SPEC section 4.5).
;
; IT IS A MOVER AND NOT A LOOP IN C. The pattern's period is four bytes over
; 49,152 of them, so the C form is 12,288 iterations of two near calls -
; ~270 ms on the target, spent inside os88_main before the window is up. Two
; string instructions do it in one call.
;
; It does NOT touch the dirty bitmap, for a2_zfill's reason: a fill is
; followed by the C marking what it filled, in one call rather than in n.
; The pattern is written in two passes because `rep stosw` cannot alternate:
; everything to zero, then $FFFF into every fourth byte pair.
; -----------------------------------------------------------------------------
_a2_zpower:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [_a2_m+AM_RAMSEG]
    mov di, [bp+4]
    mov cx, [bp+6]
    shr cx, 1                       ; n/2 words
    jcxz .out
    xor ax, ax
    rep stosw                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; here on purpose and restored below
    mov di, [bp+4]
    mov cx, [bp+6]
    shr cx, 1
    shr cx, 1                       ; n/4 groups of four
    jcxz .out
    mov ax, 0xFFFF
.g:
    stosw                           ; cc8086:allow ...and the same ES
    inc di                          ; ...over the two zero bytes of the group
    inc di
    loop .g
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_zcopy_in(unsigned a, const void *src, unsigned n)
; void a2_zcopy_out(void *dst, unsigned a, unsigned n)
;
; The package's bss to and from the Apple's RAM. Both take the emulated
; address as a NUMBER and the package's end as a POINTER, which is the (seg,
; off) rule written in the signature.
; -----------------------------------------------------------------------------
_a2_zcopy_in:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov es, [_a2_m+AM_RAMSEG]
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    jcxz .out
    rep movsb                       ; cc8086:allow ES is the RAM claim; DS is
                                    ; the package's own and does not move
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

_a2_zcopy_out:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov bx, [_a2_m+AM_RAMSEG]
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov ax, ds
    mov es, ax                      ; ES = the package: dst is a near pointer
    mov ds, bx                      ; DS = the RAM claim
    jcxz .out
    rep movsb                       ; cc8086:allow both loaded on purpose here
                                    ; and both restored below
.out:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2_zzcopy_in(unsigned a, unsigned seg, unsigned off, unsigned n)
; void a2_zzcopy_out(unsigned seg, unsigned off, unsigned a, unsigned n)
;
; A HEAP CLAIM to and from the Apple's RAM - FAR TO FAR, neither side a C
; pointer, which is this file's own (segment, offset) rule with both ends
; wearing it (APPLE2-SPEC section 3.4). Load Program reads its file into a
; transient claim, walks it there and moves the accepted program in with the
; first; Save Program moves the program out with the second and writes the
; claim. c64mem.inc's `_c64_zzcopy_in` is the shape; the OUT direction has no
; precedent in that package because the C64 saves nothing.
;
; The arguments are read into registers BEFORE either segment register moves,
; because [bp+n] is SS-relative and _a2_m is DS-relative: the DS load has to
; be the last thing that happens before the move.
; -----------------------------------------------------------------------------
_a2_zzcopy_in:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov di, [bp+4]                  ; the Apple address
    mov ax, [bp+6]                  ; ...the claim's segment
    mov si, [bp+8]                  ; ...and offset
    mov cx, [bp+10]
    mov es, [_a2_m+AM_RAMSEG]       ; through DS, which has not moved yet
    mov ds, ax
    jcxz .out
    rep movsb                       ; cc8086:allow both segments loaded here on
                                    ; purpose and both restored below
.out:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

_a2_zzcopy_out:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov ax, [bp+4]                  ; the claim's segment...
    mov di, [bp+6]                  ; ...and offset
    mov si, [bp+8]                  ; the Apple address
    mov cx, [bp+10]
    mov es, ax
    mov ds, [_a2_m+AM_RAMSEG]       ; ...last, for the reason in the header
    jcxz .out
    rep movsb                       ; cc8086:allow as above
.out:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int a2_copy_row(unsigned dseg, unsigned doff, int n)
;     - ONE row of Edit > Copy, end to end (APPLE2-SPEC section 6.5): the n
;       screen bytes already in a2_scrow, folded to ASCII through a2_astab,
;       written into the CLAIM at dseg:doff with the trailing spaces trimmed
;       and one CR appended. Answers the bytes written, at most n + 1.
;
; THIS IS THE per-BYTE LOOP, AND THAT IS WHY IT IS HERE. docs/C-TOOLCHAIN.md's
; rule in one line: "the inner loop is assembly - anything that touches bytes
; per iteration is a hand-written proc that C calls once". 24 calls for the
; screen against 960 table indexes and 984 os88_pokes, which is the shape the
; C64 measured at ~95 us a cell before c64_copy_row replaced it.
;
; THE MASK IS THE WHOLE OF THE INVERSE/FLASH/NORMAL QUESTION. An Apple II+
; character generator holds 64 glyphs and the screen byte's top two bits pick
; the ATTRIBUTE, not the character (section 7.3), so `and 0x7F` collapses
; inverse and flashing onto the normal codes and a 128-entry table folds what
; is left to ASCII. What a user sees flashing and what the clipboard gets are
; the same letter, which is what a listing copied off a `]` prompt has to be.
;
; DS is the package's (a2_scrow and a2_astab are its bss); ES is the claim and
; is restored.
; -----------------------------------------------------------------------------
_a2_copy_row:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+4]
    mov es, ax                      ; cc8086:allow ES is the claim, loaded here
                                    ; on purpose and restored below
    mov di, [bp+6]
    mov dx, di                      ; DX = one past the last non-space
    mov cx, [bp+8]
    mov si, _a2_scrow
    xor bh, bh
    or cx, cx
    jz .end
.cell:
    mov bl, [si]
    inc si
    and bl, 0x7F                    ; the attribute bits are not a character
    mov al, [_a2_astab+bx]
    mov [es:di], al
    inc di
    cmp al, ' '
    je .sp
    mov dx, di                      ; a non-space: the row keeps this far
.sp:
    dec cx
    jnz .cell
.end:
    mov di, dx                      ; the trailing spaces come off - an Apple
                                    ; text row is space-padded to 40 cells and
                                    ; a listing pasted elsewhere must not be
    mov byte [es:di], 0x0D          ; ...and the separator is CR, which is what
    inc di                          ; this OS's own clipboard readers store
                                    ; (SPEC.md 27.6 folds CR LF and a lone LF
                                    ; onto it) AND what Edit > Paste feeds the
                                    ; Apple, so a Copy pastes back byte for byte
    mov ax, di
    sub ax, [bp+6]
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned a2_scan0(unsigned seg, unsigned off, unsigned n)
;     - the offset of the first zero byte in seg:off .. seg:off+n-1, or
;       $FFFF when there is none.
;
; THE SENTINEL IS $FFFF AND NOT -1, and that is a 16-bit `int` decision rather
; than a style one: a 48K program's offsets reach 47,615, which read as a
; NEGATIVE int, so a `< 0` test at the call site would refuse every line past
; the 32KB mark of a large listing. $FFFF is unreachable as an offset here -
; the caller's claim is at most 47 KB - so it is a sentinel that cannot
; collide.
;
; THE LINKED-LINE WALK'S INNER LOOP (APPLE2-SPEC section 12). A tokenised
; Applesoft line ends at a $00 and Applesoft's own FIX.LINKS finds it by
; scanning, so the walk is once per LINE here and once per BYTE without it:
; a 5KB program would be five thousand os88_peek far calls, ~235 ms, INSIDE
; os88_onfile - which is dispatched under the desktop's gfx lock. One call a
; line is ~47 us; docs/C-TOOLCHAIN.md's rule again, and the bound the caller
; can state.
;
; ES is the claim and is restored; DF is left clear.
; -----------------------------------------------------------------------------
_a2_scan0:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [bp+4]                  ; cc8086:allow ES is the claim, loaded here
                                    ; on purpose and restored below
    mov di, [bp+6]
    mov cx, [bp+8]
    mov ax, 0xFFFF
    jcxz .out
    xor al, al                      ; the byte scanned for - and it is AX's
                                    ; low half, so the sentinel is re-loaded
                                    ; on the miss path below rather than
                                    ; assumed to have survived
    repne scasb
    jne .none
    lea ax, [di-1]                  ; scasb has already stepped past it
    jmp .out
.none:
    mov ax, 0xFFFF
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned a2_div32(unsigned hi, unsigned lo, unsigned d)
;     - (hi:lo) / d, in ONE 32-bit division. 0xFFFF when d is 0 or when the
;       quotient would not fit in sixteen bits.
;
; THE SPEAKER'S FREQUENCY IS ONE ROUNDING AND NOT TWO (APPLE2-SPEC section 8),
; and that is the whole reason this routine exists rather than a pair of
; sixteen-bit divides in the C. `hz = 1,020,484 / (2 * delta)` is
; `510,242 / delta`, and 510,242 does not fit in the `int` this C has - it is
; not even the product of two sixteen-bit factors (510,242 = 2 x 255,121 and
; 255,121 is prime), so apps/c64/c64mem.inc's `(a * b) / d` shape cannot hold
; it either and the DIVIDEND is passed as its two halves instead. The C64
; measured what rounding twice costs one machine along: F = 341 answered 19 Hz
; against a true 20, which is the difference between the 20 Hz floor refusing
; the note and playing it.
;
; The overflow guard is not decoration: `div` on an 8086 raises INT 0 when the
; quotient does not fit, and INT 0 in a package is a machine that stops. The
; caller's own band (delta 42..25,512 for 20..12,000 Hz) keeps it far away
; from that, and the guard is what makes the routine safe for the next caller
; who has not done that arithmetic.
; -----------------------------------------------------------------------------
_a2_div32:
    push bp
    mov bp, sp
    mov dx, [bp+4]                  ; hi
    mov ax, [bp+6]                  ; lo
    mov cx, [bp+8]                  ; d
    or cx, cx
    jz .over
    cmp dx, cx                      ; DX >= CX would overflow the quotient
    jae .over
    div cx
    pop bp
    ret
.over:
    mov ax, 0xFFFF
    pop bp
    ret
