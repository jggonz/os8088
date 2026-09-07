; =============================================================================
; os8088 - apps/lemmings/lemmask.inc      THE SOLID MASK (SPEC.md 92.4)
;
; %included by apps/lemmings/lemmings.asm after the compiled C, exactly as
; tests/cfsx/cfsxvga.inc and apps/cword/cwtype.inc are. It defines C-callable
; cdecl entry points and switches back to .text at the end, as CLAUDE.md's
; section discipline requires.
;
; WHAT THE MASK IS. One bit per world pixel: 1 = solid terrain, 0 = air. The
; world is 1,584 x 160 (SPEC.md 92.3.1's ruling - lemmings_3ds and Lemmix agree
; and Lemmings.ts's 1,600 is wrong) and the STRIDE is 200 bytes = 1,600
; addressable pixels, the next multiple of 8 above 1,584, which makes the claim
; exactly 32,000 bytes and the 16 pixels past the world a harmless margin that
; a clipped write can land in.
;
; EVERY ADAPTER HAS ONE, VGA INCLUDED. Plane 3 of mode 0Dh carries the same
; information - a terrain pixel is 8..15 and an air pixel is 0, so plane 3 IS
; solidity - but reading it back is a VRAM read through the Read Map Select
; register, and the probe is the hottest call in the game.
;
; WHY IT IS ASSEMBLY. lem_has_pixel is called 4 to 8 times per lemming per tick
; and a tick on a 4.77 MHz 8088 is ~16,000 instructions
; (docs/LEMMINGS-PORT-PLAN.md's corrected frame budget). A SmallerC cdecl call
; is the caller pushing the arguments, the call, the callee's push bp / mov
; bp,sp / ... / leave / ret and the caller's add sp - 20 to 30 instructions
; BEFORE the body. Sixty lemmings at six probes is 360 calls a tick, 9,000 of
; the 16,000 in overhead alone. So lem_probe4() exists: ONE call carrying four
; offsets and answering four bits. tests/lembench measures both and prints the
; microseconds (SPEC.md 92.7).
;
; THE MASK IS ALSO WHAT THE 1bpp ADAPTERS' PICTURE IS MADE OF. On CGA and
; Hercules terrain is drawn by COLOUR CLASS (SPEC.md 39.4, 92.4.2) rather than
; in the level's palette, and every terrain pixel is the same class - so the
; whole world picture on those two adapters is a function of this array and
; nothing else. lem_m_derive() writes it in ONE pass over 32,000 bytes instead
; of the composer writing two pictures at once, which at 400 terrain pieces a
; level is both simpler and faster. VGA composes its own four planes in
; lemblit.inc, because there the picture is the level's own colours.
;
; THE CALLING CONVENTION is SmallerC's cdecl, the one apps/cword/cwmove.inc
; documents: arguments pushed right to left so the first is at [bp+4], the
; CALLER cleans up, AX is the return value, and AX/BX/CX/DX are the caller's to
; lose. SI, DI, BP, DS, SS and ES are preserved by everything here - ES because
; the kernel's callbacks are entered with KERNEL_SEG in it and expect it back
; (SPEC.md 20.4) - and DF is left clear because every ISR in the system assumes
; it.
;
; SS != DS IN A PACKAGE (docs/C-TOOLCHAIN.md's first rule is a consequence of
; it), and three of the routines below need TWO far segments at once - the
; style bank to read and the mask to write - which is exactly the pair of
; segment registers this machine has. So DS is repointed at the source and
; every scrap of this file's own per-call state lives in STACK LOCALS, which
; are SS-relative through BP and therefore always reachable. The only two
; exceptions are read-only: lm_nib2 is read through a CS override, which is
; legal whatever DS holds because a package is ONE flat segment; and lm_seg is
; ordinary .bss, read at the top of each routine while DS is still ours.
;
; ATTRIBUTION. Nothing here is derived from the original. The world size and
; the terrain flag bits are facts about a FILE FORMAT, cited where they are
; read in apps/lemmings/lemovl.c; Lemmings is (C) 1991 DMA Design / Psygnosis
; and README.TXT carries the full attribution.
; =============================================================================

section .text

LEM_MSTRIDE equ 200                 ; bytes per world row - 1,600 pixels
LEM_MROWS   equ 160
LEM_MWORLD  equ 1584                ; ...of which these are the world

; the flag bits of a terrain entry's top nibble, in the LVL format document's
; own words: "8 (do not overwrite existing terrain), 4 (display upside-down),
; or 2 (remove terrain instead of add it)"
LEM_TF_NOOVER equ 8
LEM_TF_UPDOWN equ 4
LEM_TF_ERASE  equ 2

; -----------------------------------------------------------------------------
; lem_moff - internal. in: AX = x, BX = y. out: DI = byte offset, CL = the bit
; NUMBER (7 = the leftmost pixel of the byte). AX and CH destroyed; BX, DX, SI
; and every segment preserved. NO range check: each caller checks what it needs.
;
; y * 200 is (y<<7) + (y<<6) + (y<<3) - a handful of shifts and two adds
; against `mul`'s ~120 cycles on an 8088. On the probe path that difference IS
; the routine.
; -----------------------------------------------------------------------------
lem_moff:
    push dx
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1                       ; y*8
    mov dx, bx
    mov cl, 6
    shl dx, cl                      ; y*64
    add di, dx
    shl dx, 1                       ; y*128
    add di, dx
    mov cl, al
    and cl, 7
    xor cl, 7                       ; 7 = leftmost
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    pop dx
    ret

; lem_rowoff - internal. in: AX = row. out: DI = row * LEM_MSTRIDE. AX kept.
lem_rowoff:
    push ax
    push cx                         ; it uses CL for the shift, and two of its
    push dx                         ; callers hold a byte count in CX
    mov di, ax
    shl di, 1
    shl di, 1
    shl di, 1                       ; row*8
    mov dx, ax
    mov cl, 6
    shl dx, cl                      ; row*64
    add di, dx
    shl dx, 1                       ; row*128
    add di, dx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; void lem_m_setup(unsigned maskseg)
;
; The claim the mask lives in, kept in this file's own word rather than passed
; on every call: the probe path may not spend two more pushed words per call
; (see the header), and there is exactly one mask for the life of a level.
; -----------------------------------------------------------------------------
_lem_m_setup:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lm_seg], ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_m_clear(void) - 32,000 bytes of air.
; -----------------------------------------------------------------------------
_lem_m_clear:
    push bp
    mov bp, sp
    push di
    push es
    mov ax, [lm_seg]
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, (LEM_MSTRIDE * LEM_MROWS) / 2
    cld
    rep stosw
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; int lem_has_pixel(int x, int y) - 1 = solid, 0 = air OR outside the world.
;
; OUTSIDE THE WORLD IS AIR AND NOT STEEL, deliberately: a lemming walking off
; an edge falls, which is what the original does. The action handlers test the
; world bounds themselves where the difference matters.
; -----------------------------------------------------------------------------
_lem_has_pixel:
    push bp
    mov bp, sp
    push di
    push es
    mov bx, [bp+6]
    cmp bx, LEM_MROWS
    jae .air                        ; unsigned, so a negative y is caught too
    mov ax, [bp+4]
    cmp ax, LEM_MWORLD
    jae .air
    mov dx, [lm_seg]
    call lem_moff
    mov es, dx
    mov al, [es:di]
    shr al, cl
    and al, 1
    xor ah, ah
    jmp .out
.air:
    xor ax, ax
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_set_pixel(int x, int y)
; -----------------------------------------------------------------------------
_lem_set_pixel:
    push bp
    mov bp, sp
    push di
    push es
    mov bx, [bp+6]
    cmp bx, LEM_MROWS
    jae .out
    mov ax, [bp+4]
    cmp ax, LEM_MWORLD
    jae .out
    mov dx, [lm_seg]
    call lem_moff
    mov es, dx
    mov al, 1
    shl al, cl
    or [es:di], al
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_clear_pixel(int x, int y)
; -----------------------------------------------------------------------------
_lem_clear_pixel:
    push bp
    mov bp, sp
    push di
    push es
    mov bx, [bp+6]
    cmp bx, LEM_MROWS
    jae .out
    mov ax, [bp+4]
    cmp ax, LEM_MWORLD
    jae .out
    mov dx, [lm_seg]
    call lem_moff
    mov es, dx
    mov al, 1
    shl al, cl
    not al
    and [es:di], al
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; int lem_probe4(int x, int y, const char *offs) - FOUR PROBES, ONE CALL.
;
; `offs` is eight SIGNED bytes - dx0,dy0, dx1,dy1, dx2,dy2, dx3,dy3 - relative
; to (x, y). The answer is a four-bit mask, bit n set where probe n found solid
; terrain; an out-of-world probe answers 0 exactly as lem_has_pixel does.
;
; THIS IS THE ONE OPTIMISATION THE PLAN NAMED BEFORE IT WAS MEASURED, and it is
; the right shape for the reason in the header: what a probe costs on this
; machine is mostly the CALL. A walker's step tests the pixel under its foot,
; the two ahead of it and the one above - four probes, one call, and the offsets
; ride as eight bytes of table rather than eight pushed words.
;
; `offs` is DS-relative, which every caller satisfies by construction: the
; tables are `static const` and the four C rules forbid the address of an
; automatic (SPEC.md 73.5, docs/C-TOOLCHAIN.md).
;
; It walks the table BACKWARDS - probe 3 first - so the answer can be built by
; `shl ch,1 / or ch,al` and still come out with probe n in bit n.
; -----------------------------------------------------------------------------
_lem_probe4:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [lm_seg]
    mov es, ax
    mov si, [bp+8]
    add si, 6                       ; the LAST pair
    xor ch, ch                      ; the answer under construction
    mov dl, 4                       ; ...and the four probes to do
.probe:
    mov al, [si+1]                  ; dy, signed
    cbw
    add ax, [bp+6]
    mov bx, ax
    cmp bx, LEM_MROWS
    jae .miss
    mov al, [si]                    ; dx, signed
    cbw
    add ax, [bp+4]
    cmp ax, LEM_MWORLD
    jae .miss
    call lem_moff                   ; DI, CL; BX and DX preserved
    mov al, [es:di]
    shr al, cl
    and al, 1
    jmp .put
.miss:
    xor al, al
.put:
    shl ch, 1
    or ch, al
    sub si, 2
    dec dl
    jnz .probe
    mov al, ch
    xor ah, ah
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_m_span(int x, int y, int w, int set) - a horizontal run, set or
; cleared. The digger's row and the builder's brick are both this.
;
; Bit at a time, and that is a measured choice rather than laziness: w is 9 for
; a digger and 6 for a builder's brick, so a head/middle/tail byte split would
; spend more instructions deciding than it could save moving.
; -----------------------------------------------------------------------------
_lem_m_span:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov bx, [bp+6]
    cmp bx, LEM_MROWS
    jae .out
    mov ax, [lm_seg]
    mov es, ax
    mov si, [bp+4]                  ; x
    mov cx, [bp+8]                  ; w
    cmp cx, 0
    jle .out
.cell:
    mov ax, si
    cmp ax, LEM_MWORLD
    jae .next
    push cx
    mov bx, [bp+6]
    call lem_moff
    mov al, 1
    shl al, cl
    cmp word [bp+10], 0
    je .clr
    or [es:di], al
    jmp .done
.clr:
    not al
    and [es:di], al
.done:
    pop cx
.next:
    inc si
    dec cx
    jnz .cell
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_m_apply(unsigned seg, unsigned off, int x, int y, int w, int h)
;
; A DESTRUCTION STENCIL over the mask: bash, mine and the explosion crater. The
; source is a 1bpp bitmap of w x h bits with w rounded up to a byte, and A SET
; BIT MEANS LEAVE ALONE (Lemmings.ts src/game/resources/mask-provider.ts
; Mask.at() - the sense is the opposite of what the word suggests, and getting
; it backwards erases exactly what a bash was supposed to keep).
;
; Bit at a time for lem_m_span's reason one size up: the largest stencil in
; MAIN.DAT section 1 is 16x22, so 352 bits, and the shifted-byte form would win
; perhaps a third of that on a routine that runs a few times a second.
;
; Locals: [bp-2] srow, [bp-4] scol, [bp-6] source bytes per row, [bp-8] maskseg.
; -----------------------------------------------------------------------------
_lem_m_apply:
    push bp
    mov bp, sp
    sub sp, 8
    push si
    push di
    push es
    push ds
    mov ax, [lm_seg]
    mov [bp-8], ax
    mov ax, [bp+16]                 ; w
    cmp ax, 0
    jle .out
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [bp-6], ax
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, [bp+14]                 ; h
    jge .out
    mov word [bp-4], 0
.col:
    mov ax, [bp-4]
    cmp ax, [bp+16]                 ; w
    jge .rowdone

    ; --- the source bit, read with DS on the stencil's own segment --------
    mov ax, [bp-6]
    mul word [bp-2]
    mov si, ax
    mov ax, [bp-4]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add si, ax
    add si, [bp+6]                  ; + off
    mov cl, [bp-4]
    and cl, 7
    xor cl, 7
    mov ax, [bp+4]
    mov ds, ax
    mov al, [si]
    shr al, cl
    push cs
    pop ds                          ; a package is ONE flat segment, so CS is
                                    ; our DS - and SS is NOT (see the header)
    test al, 1
    jnz .nextcol                    ; a SET source bit means LEAVE ALONE

    ; --- and the mask bit it clears ---------------------------------------
    mov bx, [bp+12]                 ; y
    add bx, [bp-2]
    cmp bx, LEM_MROWS
    jae .nextcol
    mov ax, [bp+8]                  ; x
    add ax, [bp-4]
    cmp ax, LEM_MWORLD
    jae .nextcol
    call lem_moff
    mov ax, [bp-8]
    mov es, ax
    mov al, 1
    shl al, cl
    not al
    and [es:di], al
.nextcol:
    inc word [bp-4]
    jmp .col
.rowdone:
    inc word [bp-2]
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 8                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_m_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
;                  int flags)
;
; ONE TERRAIN PIECE INTO THE MASK. `off` points at the piece's PLANE 3 inside
; the style bank, which is the piece's own transparency mask and its high
; colour bit at once (docs/lemband-format.md: "plane 3 IS the mask ... which
; makes the stored nibble the mode-0Dh colour directly"). `flags` is the LVL
; entry's top nibble.
;
; NO-OVERWRITE IS THE SAME AS NORMAL HERE, and that is arithmetic rather than a
; shortcut: "draw only where there is no terrain" is `dest |= (m & ~dest)`,
; which is `dest |= m`. The flag changes the PICTURE, where the colour under it
; matters, and lemblit.inc's VGA composer honours it there.
;
; BYTE AT A TIME WITH A RUNTIME SHIFT, unlike the two routines above, and the
; arithmetic says why: a level is up to 400 pieces averaging ~800 pixels, so
; bit at a time is ~320,000 iterations - about two and a half seconds on a
; 4.77 MHz 8088. Once per level, but visibly once per level.
;
; The shift identity is dest[i] = (src[i-1] << (8-s)) | (src[i] >> s), which is
; the LOW BYTE of ((src[i-1] << 8) | src[i]) >> s, with src[-1] and src[n] both
; zero. A negative x works unchanged: `sar` by 3 and `and 7` reconstruct it
; (x = -13 is byte -2, bit 3).
;
; Locals: [bp-2] srow, [bp-4] bytes/row, [bp-6] shift, [bp-8] first dest byte
; column (signed), [bp-10] maskseg, [bp-12] this dest row's base offset.
; -----------------------------------------------------------------------------
_lem_m_piece:
    push bp
    mov bp, sp
    sub sp, 12
    push si
    push di
    push es
    push ds

    mov ax, [lm_seg]
    mov [bp-10], ax
    mov ax, [bp+14]                 ; h
    cmp ax, 0
    jle .out
    mov ax, [bp+12]                 ; w
    cmp ax, 0
    jle .out
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [bp-4], ax                  ; source bytes per row

    mov ax, [bp+8]                  ; x
    mov cx, ax
    and cx, 7
    mov [bp-6], cx
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov [bp-8], ax                  ; first dest byte column, maybe negative

    mov word [bp-2], 0
.row:
    mov bx, [bp-2]
    cmp bx, [bp+14]                 ; h
    jge .out

    ; the destination row: y + srow, or y + h-1 - srow upside-down
    test word [bp+16], LEM_TF_UPDOWN
    jz .plain
    mov ax, [bp+14]
    dec ax
    sub ax, bx
    jmp .havedy
.plain:
    mov ax, bx
.havedy:
    add ax, [bp+10]                 ; + y
    cmp ax, LEM_MROWS
    jae .nextrow                    ; clipped off the top or the bottom
    call lem_rowoff
    mov [bp-12], di

    ; the source row's first byte
    mov ax, [bp-4]
    mul word [bp-2]
    add ax, [bp+6]
    mov si, ax

    mov ax, [bp-10]
    mov es, ax
    mov ax, [bp+4]
    mov ds, ax                      ; DS = the style bank from here to .rowend

    xor ah, ah                      ; src[i-1], zero before the first byte
    xor bx, bx                      ; the byte index inside the row
.byte:
    mov al, 0
    cmp bx, [bp-4]
    jge .haveal
    mov al, [si+bx]
.haveal:
    push ax
    mov cx, [bp-6]
    jcxz .noshift
    shr ax, cl                      ; AL becomes this dest byte's contribution
.noshift:
    mov cx, [bp-8]
    add cx, bx
    cmp cx, 0
    jl .skip
    cmp cx, LEM_MSTRIDE
    jge .skip
    add cx, [bp-12]
    mov di, cx
    test word [bp+16], LEM_TF_ERASE
    jz .orin
    not al
    and [es:di], al
    jmp .skip
.orin:
    or [es:di], al
.skip:
    pop ax
    mov ah, al                      ; the RAW byte becomes src[i-1]
    inc bx
    cmp bx, [bp-4]
    jle .byte                       ; <= : one dest byte past the last source

    push cs
    pop ds
.nextrow:
    inc word [bp-2]
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 12                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_m_derive(unsigned dstseg, int kind)
;
; THE WHOLE 1bpp/2bpp WORLD PICTURE OUT OF THE MASK, in one pass (see the file
; header). kind 0 writes a CGA 2bpp world - 400 bytes a row, every solid pixel
; the terrain colour - and kind 1 a Hercules 1bpp one, 200 bytes a row, every
; solid pixel dithered 50% so a solid-white lemming stays legible on top of it
; (SPEC.md 39.4: grey rounds to black on those two adapters, so a class has to
; be a SHAPE and not a shade).
;
; The CGA arm expands a NIBBLE through a 16-byte table rather than a byte
; through a 512-byte one. Sixteen bytes of a resident image that is spent
; before this file starts (SPEC.md 92.6) against half a kilobyte for one shift
; saved per byte.
;
; Locals: [bp-2] row, [bp-4] maskseg.
; -----------------------------------------------------------------------------
_lem_m_derive:
    push bp
    mov bp, sp
    sub sp, 4
    push si
    push di
    push es
    push ds
    mov ax, [lm_seg]
    mov [bp-4], ax
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, LEM_MROWS
    jge .out
    call lem_rowoff
    mov si, di                      ; the mask row
    mov ax, [bp-4]
    mov ds, ax
    mov ax, [bp+4]
    mov es, ax
    cmp word [bp+6], 0
    jne .herc

    ; --- CGA: 400 bytes out for 200 in, high nibble then low --------------
    mov di, si
    shl di, 1
    mov cx, LEM_MSTRIDE
    cld
.cga:
    lodsb
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov bl, al
    xor bh, bh
    mov al, [cs:lm_nib2+bx]         ; a CS override: legal whatever DS holds,
    stosb                           ; because a package is one flat segment
    mov al, ah
    and al, 15
    mov bl, al
    xor bh, bh
    mov al, [cs:lm_nib2+bx]
    stosb
    loop .cga
    jmp .rowdone

    ; --- Hercules: 200 bytes out for 200 in, dithered by row --------------
.herc:
    mov di, si
    mov ah, 0xAA
    test byte [bp-2], 1
    jz .dith
    mov ah, 0x55
.dith:
    mov cx, LEM_MSTRIDE
    cld
.hloop:
    lodsb
    and al, ah
    stosb
    loop .hloop

.rowdone:
    push cs
    pop ds
    inc word [bp-2]
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 4                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; The nibble expansion the CGA arm uses: each set bit becomes the two bits
; `10`, which is CGA colour 2 - the terrain class - and each clear bit becomes
; `00`, the background.
lm_nib2:
    db 0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A
    db 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA

section .bss
lm_seg:     resw 1                  ; inside .bss and before CC_IMAGE_END, so
                                    ; crt0's cc_bss_end counts it and the loader
                                    ; zeroes it with everything else

section .text
