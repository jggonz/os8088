; =============================================================================
; os8088 - apps/runcpm/rcmem.inc       the Z80-RAM accessors and movers
;
; Part of RUNCPM (SPEC.md 74), a reimplementation of RunCPM 6.9 by Marcelo
; Dantas / "Mockba the Borg" (MIT). %included by apps/runcpm/runcpm.asm after
; the compiled C, and by hosttest/rcmemtest.asm, the boot-sector harness that
; runs these on a real x86 with SS != DS and negative controls (the cwmovetest
; precedent, LESSONS.md 4), so what is tested is the shipping text.
;
; WHY THESE EXIST. The Z80's 64KB is a heap claim of its own (rc_z.seg), and
; the C side - the BDOS, the loader, the DMA - has to read and write it. The
; SDK's os88_peek/os88_poke are FAR calls through the API cell (~47 us each,
; SPEC.md 73.11); a BDOS call that moves a 128-byte record through them would
; be a program. So the package carries its own: near cdecl accessors for a
; byte or a word, and block movers that are the ONE place in RUNCPM where ES
; is loaded and a string instruction runs. Every one restores ES (it arrives
; as KERNEL_SEG on every callback, SPEC.md 20.4) and leaves DF clear; every
; `rep` line is marked cc8086:allow with its reason, which is the convention
; the gate would ask for if it read this file (it reads the compiled C only -
; the marks are for the reviewer, SPEC.md 73.6).
;
; RunCPM's own equivalents are ram.h's _RamRead/_RamWrite/_RamLoad and the
; direct RAM[] access of RAM_FAST; there is nothing to port there, only the
; addresses that these take (a Z80 address is a 16-bit offset into the claim,
; and it wraps at 64KB exactly as the Z80's does).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. rc_z.seg is the claim, banked by the C at launch (rcz80.inc _rc_z).
; =============================================================================

section .text

; int rc_rd(unsigned a) - the byte at Z80 address a
_rc_rd:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_rc_z+RZ_SEG]
    mov al, [es:bx]
    mov ah, 0
    pop es
    pop bp
    ret

; int rc_rd16(unsigned a) - the little-endian word at a (a = 0xFFFF wraps)
_rc_rd16:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_rc_z+RZ_SEG]
    mov al, [es:bx]
    inc bx
    mov ah, [es:bx]
    pop es
    pop bp
    ret

; void rc_wr(unsigned a, int v) - one byte
_rc_wr:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_rc_z+RZ_SEG]
    mov al, [bp+6]
    mov [es:bx], al
    pop es
    pop bp
    ret

; void rc_wr16(unsigned a, int v) - one little-endian word
_rc_wr16:
    push bp
    mov bp, sp
    push es
    mov bx, [bp+4]
    mov es, [_rc_z+RZ_SEG]
    mov ax, [bp+6]
    mov [es:bx], al
    inc bx
    mov [es:bx], ah
    pop es
    pop bp
    ret

; void rc_zcopy_in(unsigned zaddr, const void *src, unsigned n)
; n bytes from the package's own src into the Z80 at zaddr
_rc_zcopy_in:
    push bp
    mov bp, sp
    push es
    mov di, [bp+4]                  ; zaddr
    mov si, [bp+6]                  ; src, DS-relative: ours
    mov cx, [bp+8]
    mov es, [_rc_z+RZ_SEG]
    cld
    rep movsb                       ; cc8086:allow ES = the Z80 claim, loaded
                                    ; above and restored below
    pop es
    pop bp
    ret

; void rc_zcopy_out(void *dst, unsigned zaddr, unsigned n)
; n bytes from the Z80 at zaddr into the package's own dst
_rc_zcopy_out:
    push bp
    mov bp, sp
    push es
    push ds
    mov di, [bp+4]                  ; dst, ours
    mov si, [bp+6]                  ; zaddr
    mov cx, [bp+8]
    push ds
    pop es                          ; ES = DS: the destination is ours
    mov ds, [_rc_z+RZ_SEG]          ; DS = the claim: the source (read while
                                    ; DS was still ours - the mov above)
    cld
    rep movsb                       ; cc8086:allow ES = this package (loaded
                                    ; from DS above), DS = the Z80 claim,
                                    ; both restored below
    pop ds
    pop es
    pop bp
    ret

; void rc_zzcopy_in(unsigned zaddr, unsigned seg, unsigned off, unsigned n)
; n bytes from another claim seg:off into the Z80 at zaddr (the CCP image,
; a file's record)
_rc_zzcopy_in:
    push bp
    mov bp, sp
    push es
    push ds
    mov di, [bp+4]                  ; zaddr
    mov ax, [bp+6]                  ; seg
    mov si, [bp+8]                  ; off
    mov cx, [bp+10]
    mov es, [_rc_z+RZ_SEG]
    mov ds, ax
    cld
    rep movsb                       ; cc8086:allow ES = the Z80 claim, DS =
                                    ; the source claim, both restored below
    pop ds
    pop es
    pop bp
    ret

; void rc_zzcopy_out(unsigned seg, unsigned off, unsigned zaddr, unsigned n)
; n bytes from the Z80 at zaddr into another claim seg:off
_rc_zzcopy_out:
    push bp
    mov bp, sp
    push es
    push ds
    mov es, [bp+4]                  ; seg
    mov di, [bp+6]                  ; off
    mov si, [bp+8]                  ; zaddr
    mov cx, [bp+10]
    mov ds, [_rc_z+RZ_SEG]
    cld
    rep movsb                       ; cc8086:allow ES = the destination
                                    ; claim, DS = the Z80 claim, both
                                    ; restored below
    pop ds
    pop es
    pop bp
    ret

; void rc_zfill(unsigned zaddr, int v, unsigned n) - n bytes of v
_rc_zfill:
    push bp
    mov bp, sp
    push es
    mov di, [bp+4]
    mov ax, [bp+6]
    mov cx, [bp+8]
    mov es, [_rc_z+RZ_SEG]
    cld
    rep stosb                       ; cc8086:allow ES = the Z80 claim, loaded
                                    ; above and restored below
    pop es
    pop bp
    ret

; void rc_sfill(unsigned seg, unsigned off, int v, unsigned n) - n bytes of
; v into ANOTHER claim seg:off (a file's gap written past its end is zeros,
; SPEC.md 74.3 - the one fill this program makes outside the Z80's RAM)
_rc_sfill:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]                  ; seg
    mov di, [bp+6]                  ; off
    mov ax, [bp+8]
    mov cx, [bp+10]
    cld
    rep stosb                       ; cc8086:allow ES = the file's claim,
                                    ; loaded above and restored below
    pop es
    pop bp
    ret

section .text
