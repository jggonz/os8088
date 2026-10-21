; =============================================================================
; os8088 - tests/lembench/lembench.asm
;
; The assembly shim of LEMBENCH, the LEMMINGS raster's BENCH (SPEC.md 116.7).
;
; IT %includes THE SHIPPING RASTER, IT DOES NOT CARRY A COPY OF IT. The three
; .inc files below are apps/lemmings' own, included from their own directory,
; so what this bench times is the code that ships and a change to either half
; cannot leave them measuring different things (WEAVE-SPEC 1.2's rule for the
; Weave family, and the same reason). That is also why this is a package rather
; than a hand-written loop: the numbers have to come off the real routines,
; through the real cdecl boundary, with the real segment arithmetic.
;
; NOTHING UNDER tests/ SHIPS (CLAUDE.md): `make lembench` builds it on demand and
; no floppy `all` writes carries it.
; =============================================================================

%define CC_PKG_NAME 'LEMBENCH'
%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_FSX                  ; the bench runs INSIDE the bracket, because
                                    ; half of what it times is VRAM and port I/O

%include "cc/crt0.asm"
%include "lembench.gen.asm"          ; the compiled C, found through -I build/

; The shipping raster, in the order lemmings.asm includes it: lemblit.inc reads
; lemmask.inc's lm_seg and lem_rowoff, and lemfont.inc reads lemblit.inc's
; lb_rpix.
%include "lemmings/lemmask.inc"
%include "lemmings/lemblit.inc"
%include "lemmings/lemfont.inc"
%include "lemmings/lemsprite.inc"

; -----------------------------------------------------------------------------
; THE ALTERNATIVE THE COMPOSER WAS CHOSEN AGAINST, timed here and shipped
; nowhere (SPEC.md 116.7.2's composer rows). Wave 3's brief proposed composing
; the world as PACKED 4bpp nibbles in RAM and converting it to the four VGA
; planes in one pass through a 256-entry table: one input byte is two pixels,
; and four of them make the four plane bytes of an 8-pixel group. This is that
; pass, written as tight as the shipping composer, so the two designs are
; compared on the CONVERSION ALONE - the half the packed design adds - before
; anyone counts the swizzle a planar terrain piece needs on its way INTO a
; packed world.
;
; void lbb_lutconv(unsigned src) - 1,600 x 160 packed pixels at src:0 (800
; bytes a row, crossing segments by advancing src 50 paragraphs a row) into
; VRAM at LEM_VTERR under the Map Mask. lbb_lut is 256 x 4 bytes, filled in C:
; entry b, byte p = plane p's two bits for pixels (b >> 4, b & 15).
; -----------------------------------------------------------------------------
_lbb_lutconv:
    push bp
    mov bp, sp
    sub sp, 4                       ; [bp-2] row, [bp-4] source segment
    push si
    push di
    push es
    push ds
    mov ax, [bp+4]
    mov [bp-4], ax
    mov word [bp-2], 0
.row:
    cmp word [bp-2], LEM_MROWS
    jb .rgo
    jmp .done
.rgo:
    ; --- 800 input bytes into four 200-byte plane rows ----------------------
    mov ax, [bp-4]
    mov ds, ax
    xor si, si
    xor di, di                      ; the plane-row index, 0..199
.grp:
    xor dx, dx
    xor cx, cx
%rep 4
    lodsb
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl dl, 1
    shl dl, 1
    or dl, [cs:_lbb_lut+bx]
    shl dh, 1
    shl dh, 1
    or dh, [cs:_lbb_lut+bx+1]
    shl cl, 1
    shl cl, 1
    or cl, [cs:_lbb_lut+bx+2]
    shl ch, 1
    shl ch, 1
    or ch, [cs:_lbb_lut+bx+3]
%endrep
    mov [cs:lbb_r0+di], dl
    mov [cs:lbb_r1+di], dh
    mov [cs:lbb_r2+di], cl
    mov [cs:lbb_r3+di], ch
    inc di
    cmp di, LEM_MSTRIDE
    jb .grp
    push cs
    pop ds
    ; --- ...and the four rows out under the Map Mask ------------------------
    mov ax, [bp-2]
    call lem_rowoff                 ; DI = row * 200
    add di, LEM_VTERR
    mov bx, di
    mov ax, [lb_fb]
    mov es, ax
    mov dl, 1
    mov si, lbb_r0
.pl:
    push dx
    mov ah, dl
    mov al, 2
    call lb_seq
    pop dx
    mov di, bx
    mov cx, LEM_MSTRIDE / 2
    cld
    rep movsw                       ; SI walks r0, r1, r2, r3 in turn
    shl dl, 1
    cmp dl, 16
    jb .pl
    add word [bp-4], 50             ; 800 bytes = 50 paragraphs a row
    inc word [bp-2]
    jmp .row
.done:
    mov ah, 0x0F
    mov al, 2
    call lb_seq
    pop ds
    pop es
    pop di
    pop si
    add sp, 4
    pop bp
    ret

section .bss
lbb_r0:     resb LEM_MSTRIDE        ; one output row a plane, contiguous so the
lbb_r1:     resb LEM_MSTRIDE        ; four `rep movsw` walk them in order
lbb_r2:     resb LEM_MSTRIDE
lbb_r3:     resb LEM_MSTRIDE
section .text

    CC_IMAGE_END
