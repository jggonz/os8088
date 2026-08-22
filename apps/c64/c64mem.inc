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
