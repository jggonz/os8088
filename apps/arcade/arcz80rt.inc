; =============================================================================
; os8088 - apps/arcade/arcz80rt.inc - the Pac-Man board's helpers inside a
; translated code part (ARCADE-SPEC 7.4)
;
; Assembled after arcrt.inc by every code part apps/arcade/arcz80t.py writes.
; DS = SS = the view, ES:DI = aux's write list, interrupts off - the core's
; environment, so nothing here may push more than the 8080 rule allows below
; the guest's SP (a few words, which is dead space by the Z80's definition).
; =============================================================================

; -----------------------------------------------------------------------------
; arc_iow - a store to 5000-50FF. SI = the address, AL = the byte; JMPed to
; with the way back in ST_RETIO. Clobbers SI, AL and FLAGS (the caller
; guards them). The latches go to PM_LATCH, the
; voices to PM_SOUND, the sprites' positions to PM_SPRXY; 5000 bit 0 is the
; VBlank interrupt's enable, and clearing it drops one already pending - the
; board's line is held until then (MAME pacman.cpp).
; -----------------------------------------------------------------------------
arc_iow:
    and si, 0x00FF
    cmp si, 0x08
    jae .snd
    mov [si+PM_LATCH], al
    or si, si
    jnz .r
    and al, 1
    mov [ST_IRQEN], al
    jnz .r
    mov byte [ST_PEND], 0
.r:
    jmp word [ST_RETIO]
.snd:
    cmp si, 0x40
    jb .r
    cmp si, 0x60
    jae .spr
    and al, 0x0F
    mov [si+PM_SOUND-0x40], al
    jmp word [ST_RETIO]
.spr:
    cmp si, 0x70
    jae .r
    mov [si+PM_SPRXY-0x60], al
    jmp word [ST_RETIO]

; -----------------------------------------------------------------------------
; arc_ldir / arc_lddr / arc_ldi / arc_ldd - LDIR, LDDR, LDI, LDD: bytes from
; (HL) to (DE), each through the board's map, so a copy into video lists its
; tiles. JMPed to with the way back in ST_RET; nothing goes on the stack.
; LDIR/LDDR keep S Z C (the caller clears P/V where it is live: BC ran out);
; LDI/LDD keep S Z C, clear H, and make P/V = BC <> 0 (ARCADE-SPEC 7.3).
; -----------------------------------------------------------------------------
%macro ARC_BLK1 1                   ; one byte; %1 = inc or dec
    mov al, [bx]
    mov si, dx
    cmp dh, 0x4C
    jb %%lo
    cmp dh, 0x50
    jae %%io
    mov [si], al
    jmp short %%n
%%lo:
    cmp dh, 0x40
    jb %%n
    cmp dh, 0x48
    jae %%n
    cmp [si], al
    je %%n
    mov [si], al
    and si, 0x03FF
    cmp byte [es:si+PM_TFLAG], 0
    jne %%n
    mov byte [es:si+PM_TFLAG], 1
    mov [es:di], si
    lea di, [di+2]
    jmp short %%n
%%io:
    mov word [ST_RETIO], %%n
    jmp arc_iow
%%n:
    %1 bx
    %1 dx
%endmacro

arc_ldir:
    lahf
    mov [ST_SAVE], ax
.l:
    ARC_BLK1 inc
    dec cx
    jnz .l
    mov ax, [ST_SAVE]
    sahf
    jmp word [ST_RET]
arc_lddr:
    lahf
    mov [ST_SAVE], ax
.l:
    ARC_BLK1 dec
    dec cx
    jnz .l
    mov ax, [ST_SAVE]
    sahf
    jmp word [ST_RET]
arc_ldi:
    lahf
    mov [ST_SAVE], ax
    ARC_BLK1 inc
    jmp short arc_ld1
arc_ldd:
    lahf
    mov [ST_SAVE], ax
    ARC_BLK1 dec
arc_ld1:
    mov ax, [ST_SAVE]
    and ah, 0xEB
    dec cx
    jcxz .z
    or ah, 0x04
.z:
    sahf
    jmp word [ST_RET]

; -----------------------------------------------------------------------------
; arc_cpir / arc_cpdr - CPIR, CPDR: compare A with (HL), HL+-, BC-, until a
; match or BC runs out. LOOPNE is the instruction exactly - it decrements CX
; and stops on Z or on CX = 0 without touching FLAGS - so the flags are the
; last compare's S Z H, then P/V = BC <> 0 and the entry CY (ARCADE-SPEC 7.3).
; -----------------------------------------------------------------------------
arc_cpir:
    lahf
    and ah, 1
    mov [ST_CYF], ah
.l:
    cmp al, [bx]
    lea bx, [bx+1]
    loopne .l
    jmp short arc_cpend
arc_cpdr:
    lahf
    and ah, 1
    mov [ST_CYF], ah
.l:
    cmp al, [bx]
    lea bx, [bx-1]
    loopne .l
arc_cpend:
    lahf
    and ah, 0xFA
    jcxz .z
    or ah, 0x04
.z:
    or ah, [ST_CYF]
    sahf
    jmp word [ST_RET]
