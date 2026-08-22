; =============================================================================
; os8088 - apps/c64/c64mem.inc     the RAM/ROM claim accessors and the movers
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; apps/c64/ is
; GPL-2-or-later and apps/c64/COPYING is the licence text. Nothing of VICE's
; source is vendored. rcmem.inc (SPEC.md 74) is the shape.
;
; %included by apps/c64/c64.asm after the compiled C, and by
; hosttest/c64memtest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS and an ES sentinel, with negative controls
; (C64-SPEC §3.6), so what is tested is the shipping text.
;
; WHY THESE EXIST. The C64's 64KB is a heap claim of its own and its ROMs are
; a second (C64-SPEC §3.1); the C side - the flush, the loader, the
; register files - has to read and write both. The SDK's os88_peek/os88_poke
; are FAR calls through the API cell (~47 us each, SPEC.md 73.11); a screen
; row read through them would be a program. So the package carries its own.
;
; THE RULE IS WIDER THAN THE MOVERS (C64-SPEC §3.6). Data in either
; claim is passed as an explicit (segment, offset) PAIR, never as a C pointer:
; a C pointer is a package-DS offset and nothing else, so "a pointer to a
; screen row" is a defect that assembles cleanly. Every `movs`/`stos`/`cmps`
; below loads DS/ES on purpose, restores both, leaves DF clear, and carries
; the `cc8086:allow` marker with its reason - the convention the gate would
; ask for if it read this file (it reads the compiled C only, SPEC.md 73.6).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. The claims are _c64_m+CM_RAMSEG and CM_ROMSEG (c64cpu.inc), banked by
; the C at launch.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; int c64_rd(unsigned a) - the byte at C64 address a, as RAM.
;
; This is the FAST PATH of C64-SPEC §3.4 with no bank test: the C side
; uses it for the screen matrix and for its own pokes, which are RAM by
; construction. The banked read - $0000/$0001, the seven maps, the I/O window
; - is the CORE's (c64cpu.inc), and it is not this routine.
; -----------------------------------------------------------------------------
_c64_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_c64_m+CM_RAMSEG]
    mov al, [es:bx]
    xor ah, ah                      ; a shim that answers in AL must clear AH
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_rd16(unsigned a) - the little-endian word at a (a = $FFFF wraps).
; -----------------------------------------------------------------------------
_c64_rd16:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_c64_m+CM_RAMSEG]
    mov al, [es:bx]
    inc bx
    mov ah, [es:bx]
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_wr(unsigned a, int v) - one byte into RAM, and ONE BIT into the
; 256-page dirty bitmap (C64-SPEC §9.2). That `or` is the stated
; per-write cost of this whole design, and it is what lets the flush see a
; bitmap, a RAM character set or sprite data change - things a matrix-and-
; colour shadow provably cannot see.
;
; A write to the core's scratch ($FFC0-$FFF9) is DROPPED (3.5's second stated
; deviation): the scratch is the emulated machine's own top-of-memory bytes,
; and c64cputest.asm holds a case for it.
; -----------------------------------------------------------------------------
_c64_wr:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_c64_m+CM_RAMSEG]
    cmp bx, C64_SCR_BASE            ; the scratch is not writable memory (3.5)
    jb .store
    cmp bx, C64_SCR_BASE + C64_SCR_END
    jb .out
.store:
    mov al, [bp+6]
    mov [es:bx], al
    mov byte [es:C64_SCR_BASE + C64_SCR_ANY], 1
    ; the WRITE WINDOW (C64-SPEC §9.2): the lowest and highest address
    ; written since the last flush, and what turns a one-byte poke back into a
    ; one-CELL compose instead of the seven rows a 256-byte page would say.
    ;
    ; IT IS TAKEN OVER THE WATCH RANGE, NOT OVER THE ADDRESS SPACE. The first
    ; draft widened it on every write below the scratch, which is exact while
    ; the only writer is c64_poke_boot and useless the day the core runs: a
    ; JSR writes the stack at $01xx, a BASIC statement writes zero page and
    ; the variables above $0800, so one slice leaves the window spanning every
    ; matrix row in full and the flush's per-row intersection always answers
    ; "all forty cells". The C writes mbase..mbase+999 into the watch range
    ; from c64_frame_regs; two compares here keep the window exact for the
    ; screen matrix, which is the only thing it narrows.
    cmp bx, [es:C64_SCR_BASE + C64_SCR_WATLO]
    jb .nowin
    cmp bx, [es:C64_SCR_BASE + C64_SCR_WATHI]
    ja .nowin
    cmp bx, [es:C64_SCR_BASE + C64_SCR_WLO]
    jae .nolo
    mov [es:C64_SCR_BASE + C64_SCR_WLO], bx
.nolo:
    cmp bx, [es:C64_SCR_BASE + C64_SCR_WHI]
    jbe .nowin
    mov [es:C64_SCR_BASE + C64_SCR_WHI], bx
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
    add bx, C64_SCR_BASE + C64_SCR_DIRTY
    mov al, 0x80
    shr al, cl
    or [es:bx], al
.out:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_dirty(unsigned a) - set the dirty bit for address a without writing
; anything. The C side uses it after a c64_zcopy_in or a register change that
; the flush must see; the core sets it inline on its own write path.
; -----------------------------------------------------------------------------
_c64_dirty:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_c64_m+CM_RAMSEG]
    mov al, bh
    mov cl, al
    and cl, 7
    shr al, 1
    shr al, 1
    shr al, 1
    mov bl, al
    xor bh, bh
    add bx, C64_SCR_BASE + C64_SCR_DIRTY
    mov al, 0x80
    shr al, cl
    or [es:bx], al
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_scr_rd(unsigned off) / void c64_scr_wr(unsigned off, int v)
;
; RAW access to the core's scratch (C64-SPEC §3.5), which c64_rd/c64_wr
; deliberately cannot reach: the flush reads the dirty bitmap through these
; and clears it under the same lock, so nothing is lost between a write and a
; read of it (9.2).
; -----------------------------------------------------------------------------
_c64_scr_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    add bx, C64_SCR_BASE
    mov es, [_c64_m+CM_RAMSEG]
    mov al, [es:bx]
    xor ah, ah
    pop es
    pop bp
    ret

_c64_scr_wr:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    add bx, C64_SCR_BASE
    mov es, [_c64_m+CM_RAMSEG]
    mov al, [bp+6]
    mov [es:bx], al
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_dirty_take(unsigned char *dst) - THE WHOLE OF WHAT A FLUSH HAS TO
; READ OUT OF THE CORE'S SCRATCH, in ONE call: 36 bytes into the package's own
; memory, and the scratch reset in the same pass.
;
;   dst[0..31]  the 256-page dirty bitmap (C64-SPEC §9.2), and the scratch's
;               copy is zeroed
;   dst[32..33] the write window's LOW word, and the scratch's is reset to
;               $FFFF
;   dst[34..35] ...its HIGH word, reset to 0
;   and C64_SCR_ANY is cleared, because this IS the read of it
;
; WHY IT EXISTS. c64_dirty_scan read all of that a BYTE AT A TIME through
; _c64_scr_rd / _c64_scr_wr: 32 bitmap reads, up to 32 clears, four window
; reads and five window writes - about fifty near calls whose bodies are ten
; instructions each. PERFORMANCE.md prices a bare near call + ret at 11 us and
; the 8088 fetches at 4.34 clocks a byte, so each of those is ~35 us: ~1.8 ms
; a flush, comparable to the WHOLE 3.8 ms of §9.7's `one changed cell` row,
; spent before a single pixel is decided - and invisible to a cost model that
; counts only gfx calls. Two `rep` instructions over 32 bytes is ~150 us.
; PERFORMANCE.md's rule is that a redraw is priced by the calls it makes.
;
; The flush holds the gfx lock across this, so nothing is lost between the
; core's write and this read (9.2).
; -----------------------------------------------------------------------------
_c64_dirty_take:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov bx, [_c64_m+CM_RAMSEG]      ; ...through the PACKAGE's DS, BEFORE it
                                    ; goes: [_c64_m] is a package-DS reference
    mov di, [bp+4]                  ; dst is a package pointer, so ES = DS
    mov ax, ds
    mov es, ax
    mov ds, bx                      ; ...AFTER every [bp+n] is read: [bp+n] is
                                    ; SS-relative, so DS may go now
    mov si, C64_SCR_BASE + C64_SCR_DIRTY
    mov cx, 16
    rep movsw                       ; cc8086:allow both segments loaded here
                                    ; on purpose and both restored below
    mov si, C64_SCR_BASE + C64_SCR_WLO
    movsw                           ; the window's two words follow the bitmap
    movsw                           ; in dst[32..35]
    mov es, bx                      ; ES = the RAM claim: the same pass CLEARS
    mov di, C64_SCR_BASE + C64_SCR_DIRTY
    xor ax, ax
    mov cx, 16
    rep stosw                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; two lines above and restored below
    mov word [C64_SCR_BASE + C64_SCR_WLO], 0xFFFF   ; DS is the claim here too
    mov [C64_SCR_BASE + C64_SCR_WHI], ax
    mov [C64_SCR_BASE + C64_SCR_ANY], al
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_rom_rd(unsigned off) - a byte of the ROM claim, off 0..20479, in
; C64.ROM's fixed layout (C64-SPEC §1.4): KERNAL at 0, BASIC at $2000,
; CHARGEN at $4000.
; -----------------------------------------------------------------------------
_c64_rom_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_c64_m+CM_ROMSEG]
    mov al, [es:bx]
    xor ah, ah
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned c64_div32(unsigned hi, unsigned lo, unsigned d) - (hi:lo) / d.
;
; The two-word counters of C64-SPEC §10.2 have to be divided to become a
; percentage and a frame rate, and "no long" is a C RULE and not a no-32-bit
; rule (Decision 19): SmallerC cannot express this and the 8086 does it in one
; instruction. A quotient that will not fit in 16 bits, and a divisor of zero,
; both answer 0xFFFF rather than raising #DE - a status row is not worth a
; trap, and the caller clamps.
; -----------------------------------------------------------------------------
_c64_div32:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ax, [bp+6]
    mov bx, [bp+8]
    or bx, bx
    jz .over
    cmp dx, bx
    jae .over
    div bx
    pop bp
    ret
.over:
    mov ax, 0xFFFF
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned c64_muldiv(unsigned a, unsigned b, unsigned d) - (a * b) / d, with
; the product kept in 32 bits. `frames * 182 / elapsed` overflows 16 bits at
; five seconds and `%` of a real 6510 needs the same shape; the 8086 does both
; in two instructions and a 16-bit C cannot express either. Overflow and a
; zero divisor answer 0xFFFF, as c64_div32 does.
; -----------------------------------------------------------------------------
_c64_muldiv:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+8]
    or bx, bx
    jz .over
    mul word [bp+6]
    cmp dx, bx
    jae .over
    div bx
    pop bp
    ret
.over:
    mov ax, 0xFFFF
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_zcopy_in(unsigned a, const void *src, unsigned n)
;     - n bytes from the PACKAGE's own memory into the C64's RAM at a.
;
; The caller is responsible for the dirty bits (c64_dirty, or c64_dirty_all
; when the run is a screenful).
; -----------------------------------------------------------------------------
_c64_zcopy_in:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov es, [_c64_m+CM_RAMSEG]
    rep movsb                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; two lines above and restored below; DS
                                    ; is the package's, which is the source
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_zcopy_out(void *dst, unsigned a, unsigned n)
;     - n bytes of the C64's RAM at a into the PACKAGE's own memory.
; -----------------------------------------------------------------------------
_c64_zcopy_out:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    mov ax, ds
    mov es, ax                      ; ES = the package (the destination)
    mov ds, [_c64_m+CM_RAMSEG]      ; DS = the RAM claim (the source)
    rep movsb                       ; cc8086:allow both segments loaded here
                                    ; on purpose and both restored below
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_zzcopy_in(unsigned a, unsigned sseg, unsigned soff, unsigned n)
;     - n bytes from ANOTHER CLAIM (sseg:soff) into the C64's RAM at a. This
;       is the .PRG landing of C64-SPEC §11.3: far to far, so neither
;       side is a C pointer.
; -----------------------------------------------------------------------------
_c64_zzcopy_in:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov di, [bp+4]
    mov si, [bp+8]
    mov cx, [bp+10]
    mov es, [_c64_m+CM_RAMSEG]
    mov ds, [bp+6]                  ; ...AFTER every [bp+n] is read: [bp+n] is
                                    ; SS-relative, so DS may go now
    rep movsb                       ; cc8086:allow both segments loaded here
                                    ; on purpose and both restored below
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int c64_copy_row(unsigned dseg, unsigned doff, int n)
;     - ONE row of Edit > Copy, end to end (C64-SPEC 7.7): the n screen codes
;       already in c64_scrow, converted through c64_sctab, written into the
;       CLAIM at dseg:doff with the trailing spaces trimmed and one '\n'
;       appended. Answers the number of bytes written, which is at most n + 1.
;
; THIS IS THE per-BYTE LOOP, AND THAT IS WHY IT IS HERE. docs/C-TOOLCHAIN.md's
; rule in one line: "the inner loop is assembly - anything that touches bytes
; per iteration is a hand-written proc that C calls once". The C version was
; a table index and an os88_poke per cell - ~95 us a cell, ~95 ms for the
; screen - against 25 calls into this, which is ~20 clocks a cell and about
; 4 ms for all 1,000. clipboard_read_screen_output (src/clipboard.c:41-100) is
; the behaviour, last_non_whitespace (:67) and the trim (:81-85) included.
;
; DS is the package's (c64_scrow and c64_sctab are its bss); ES is the claim
; and is restored. The reverse-video bit is masked off because it is not a
; character (charset.c:204).
; -----------------------------------------------------------------------------
_c64_copy_row:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+4]
    mov es, ax                      ; ES = the claim  (cc8086:allow loaded
                                    ; here on purpose and restored below)
    mov di, [bp+6]
    mov dx, di                      ; DX = one past the last non-space
    mov cx, [bp+8]
    mov si, _c64_scrow
    xor bh, bh
    or cx, cx
    jz .end
.cell:
    mov bl, [si]
    inc si
    and bl, 0x7F
    mov al, [_c64_sctab+bx]
    mov [es:di], al
    inc di
    cmp al, ' '
    je .sp
    mov dx, di                      ; a non-space: the row keeps this far
.sp:
    dec cx
    jnz .cell
.end:
    mov di, dx                      ; the trailing spaces come off
    mov byte [es:di], 0x0A          ; ...and the line ending is '\n', which is
    inc di                          ; what every non-Windows VICE build uses
                                    ; (actions-clipboard.c:54-58)
    mov ax, di
    sub ax, [bp+6]
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64_zfill(unsigned a, int v, unsigned n) - n bytes of v into the C64's
; RAM at a. The power-cycle RAM pattern (C64-SPEC §11.1) is two of
; these per 128 bytes; a screen clear is one.
; -----------------------------------------------------------------------------
_c64_zfill:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov di, [bp+4]
    mov al, [bp+6]
    mov cx, [bp+8]
    mov es, [_c64_m+CM_RAMSEG]
    rep stosb                       ; cc8086:allow ES is the RAM claim, loaded
                                    ; above and restored below
    pop es
    pop di
    pop bp
    ret
