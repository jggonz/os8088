; =============================================================================
; os8088 - apps/infones/nimem.inc      the cross-segment movers
;
; Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
; InfoNES fe3295c0, restructured for 8086 real mode. THIS FILE carries no
; InfoNES material: it exists because os8088 has segments and InfoNES does
; not.
;
; %included by apps/infones/infones.asm AFTER the compiled C, and by
; hosttest/nimemtest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS and an ES sentinel, with negative controls - so what
; is gated is the shipping text.
;
; WHY IT EXISTS. The emulated machine is five heap claims (SPEC.md 91.3.1) and
; the C side - the loader, the register files, the reset - has to read and
; write all of them. os88_peek/os88_poke are near calls at ~11 us each: right
; for a sixteen-byte header, a program for a 32KB PRG bank.
;
; THE RULE IS WIDER THAN THE MOVERS. Data in a claim is passed as an explicit
; (segment, offset) PAIR and NEVER as a C pointer: a C pointer is a package-DS
; offset and nothing else, so "a pointer to a PRG bank" is a defect that
; assembles cleanly and reads the package's own image.
;
; ES IS LOADED IN THIS FILE, niband.inc, nicpu.inc AND nifsx.inc, AND NOWHERE
; ELSE - SPEC.md 91.13.1's table says which is pointed at what, and which of
; them nimemtest gates. This file is the movers'. Every `movs`/`stos`
; below loads DS and ES on purpose, restores both, leaves DF CLEAR, and
; carries the `cc8086:allow` marker with its reason - the convention the gate
; would ask for if it read this file (it reads the compiled C only, 73.6).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. ES is the KERNEL's on entry to every callback (20.4) and every routine
; here puts back what it found.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; void ni_move(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff,
;              unsigned n)
;
; n bytes from sseg:soff to dseg:doff. ASCENDING ONLY, which is right for
; every caller it has: the claims are disjoint, so there is no overlap to
; smear (os88_memcpy has the same property and the same note).
;
; n = 0 moves nothing rather than 65,536 bytes: `rep` with CX = 0 is already
; a no-op, which is the one place the 8086 is kind.
; -----------------------------------------------------------------------------
_ni_move:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov es, [bp+4]                  ; cc8086:allow ES is the destination claim,
    mov di, [bp+6]                  ;   loaded here and restored below
    mov ax, [bp+8]
    mov si, [bp+10]
    mov cx, [bp+12]
    mov ds, ax                      ; ...and DS the source claim
    rep movsb                       ; cc8086:allow both segments are ours
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void ni_fill(unsigned dseg, unsigned doff, int val, unsigned n)
; -----------------------------------------------------------------------------
_ni_fill:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [bp+4]                  ; cc8086:allow ES is the claim being
    mov di, [bp+6]                  ;   filled, restored below
    mov ax, [bp+8]
    mov cx, [bp+10]
    rep stosb                       ; cc8086:allow AL is the fill byte
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned ni_peekw(unsigned seg, unsigned off) - one little-endian word.
; void     ni_pokew(unsigned seg, unsigned off, unsigned v)
;
; The pair the C uses for the core's scratch words - the fetch bias, the
; countdown, the four PRG window segments. os88_peek/os88_poke are byte-wide
; and a word through them is two calls and a shift.
; -----------------------------------------------------------------------------
_ni_peekw:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]                  ; cc8086:allow
    mov bx, [bp+6]
    mov ax, [es:bx]
    pop es
    pop bp
    ret

_ni_pokew:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]                  ; cc8086:allow
    mov bx, [bp+6]
    mov ax, [bp+8]
    mov [es:bx], ax
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void ni_oam_move(unsigned ramoff) - $4014's fast path (SPEC.md 91.4.3).
;
; 256 bytes out of the CPU RAM claim into OAM, ONE call rather than 256 bus
; reads. Source and destination are both inside the machine claim, so this is
; the one mover whose two segments are the same one.
;
; The DESTINATION starts at OAM byte 0 and not at OAMADDR. That is a stated
; approximation and it is the one every emulator in this class makes: a game
; that sets OAMADDR to something other than 0 before a DMA is relying on a
; behaviour real hardware itself corrupts, and no shipped title does it.
; -----------------------------------------------------------------------------
_ni_oam_move:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov ax, [_ni_m + NIM_MACHSEG]
    mov es, ax                      ; cc8086:allow both halves are the machine
    mov ds, ax                      ;   claim, restored below
    mov si, [bp+4]
    mov di, NI_O_OAM
    mov cx, 256
    rep movsb                       ; cc8086:allow
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void ni_oam_grab(void) - OAM out of the machine claim and into the PACKAGE.
;
; The composer's sprite evaluation walks all 64 entries on every one of 240
; lines. Reaching them through os88_peek would be 15,360 near calls a frame -
; about 170 ms of a 4.77 MHz 8088 for 256 bytes that change once, in vblank -
; so the C copies them here ONCE A FRAME and niband.inc's evaluation reads
; them with a plain `mov`.
;
; It is the one mover whose DESTINATION is the package's own bss, which is why
; it takes no destination at all: a C pointer is a package-DS offset and
; nothing else, and hard-wiring the one destination there is means no caller
; can pass the wrong kind of address (nimem.inc's own rule, above).
; -----------------------------------------------------------------------------
_ni_oam_grab:
    push si
    push di
    push ds
    push es
    cld
    push ds
    pop es                          ; cc8086:allow ES is the PACKAGE - the
                                    ;   destination is our own bss - and it is
                                    ;   restored below
    mov ax, [_ni_m + NIM_MACHSEG]
    mov ds, ax                      ; ...and DS the machine claim
    mov si, NI_O_OAM
    mov di, _ni_oamc
    mov cx, 128
    rep movsw                       ; cc8086:allow both segments are ours
    pop es
    pop ds
    pop di
    pop si
    ret
