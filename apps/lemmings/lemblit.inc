; =============================================================================
; os8088 - apps/lemmings/lemblit.inc      THE THREE RASTERS (SPEC.md 116.4)
;
; %included by apps/lemmings/lemmings.asm AFTER lemmask.inc, which it reads
; `lm_seg` out of. Everything CLAUDE.md's section discipline and the cdecl
; contract in lemmask.inc's header say applies here word for word and is not
; repeated.
;
; ONE SET OF ENTRY POINTS, THREE BACKENDS BEHIND THEM. The C in lemdraw.c says
; what to draw and never which card it is on; `lb_kind` - 0 VGA, 1 CGA, 2
; Hercules - is read once per call and the routine branches. That is the shape
; SPEC.md 39 asks of everything in this tree, and it is what lets the harness
; model ONE interface (apps/lemmings/hosttest/os88.h).
;
; ---------------------------------------------------------------------------
; VGA - mode 0Dh, 320x200x16 planar, and the scroll costs FOUR `out`s a frame
; ---------------------------------------------------------------------------
; The CRTC's Offset register is moved from 0x14 (40 bytes a row, the mode's
; own) to 0x64 (200 bytes a row), so each plane holds a canvas 1,600 pixels
; wide displayed 320 at a time. The world is then a real bitmap in VRAM and a
; horizontal scroll is the Start Address (8 pixels at a time) plus the
; Attribute Controller's Horizontal Pixel Panning (the other 7) - four `out`s,
; whatever the picture is, against a 12,800-byte copy.
;
;     VRAM offset      0     the 320x40 skill panel, 40 bytes of each 200
;                  8,000     the 1,584x160 world, 200 bytes a row
;                 40,000     the frame's compose scratch (125 rows)
;                 65,000     the pointer's save-under
;
; The CRTC LINE COMPARE is set to scanline 319, SO THAT THE SPLIT BEGINS AT
; SCANLINE 320. The VGA resets its address counter at the start of the scanline
; AFTER the one the compare matches, so the register holds the split row minus
; one - in a double-scanned mode 2 * 160 - 1 = 319. Wave 2 wrote 320 and the
; panel's top glyph row came out at HALF HEIGHT: scanline 320 was the world's
; row 160 (VRAM 8000 + 160 * 200 = 40,000, the compose scratch, which
; is a strip of the frame's compose scratch in wave 3) and glyph row 0 had
; only scanline 321 (build/port-shots/wave2g-vga-fun1.png, read scanline by
; scanline). Mode 0Dh is 200 rows double scanned, so scanline 320 is pixel row
; 160: above it the CRTC shows the world
; from the start address, below it it restarts its address counter at 0 and
; shows the panel - and with the Attribute Controller's Pixel Panning Mode bit
; set the panel does not pan with the world. The whole 160-row world height is
; on the glass at once, which is what the original does: Lemmings scrolls
; horizontally and never vertically.
;
; tests/cfsx proved every one of those registers on QEMU before this file was
; written (build/port-shots/wave1-cfsx-s2-split-t0.png and -t1.png), which is
; what a capability gate is for.
;
; ---------------------------------------------------------------------------
; CGA and HERCULES - one shadow, one composer, two presenters (SPEC.md 85.3.1)
; ---------------------------------------------------------------------------
; Neither card can scroll, so the picture is composed in RAM and blitted. The
; SHADOW is 80 bytes a row by 200 rows on both - a CGA mode 4 row is 320
; pixels at 2bpp = 80 bytes, and a Hercules row here is the same 320 pixels
; DOUBLED to 640 = 80 bytes - so one composer serves both and only the
; presenter differs.
;
; The WORLD claim holds the whole 1,584-pixel level in the adapter's own form,
; derived from the solid mask in one pass by lem_m_derive() (see lemmask.inc's
; header for why the mask is enough): 400 bytes a row on CGA, 200 on Hercules.
; A scroll is then a windowed copy into the shadow.
;
; THE SCROLL STEP IS COARSER ON THESE TWO AND THAT IS A STATED FACT, not a
; defect: the copy is byte-aligned, so the step is 4 pixels on CGA (2bpp) and 8
; on Hercules (1bpp), against 1 pixel on VGA where the pel pan does it in
; hardware. A shifted copy would cost a shift and a merge on every one of
; 12,800 bytes - about 40% of the whole frame on a 4.77 MHz 8088 - to make a
; scroll that is already smooth at 17 fps smoother. SPEC.md 116.4.2 records it
; and README.TXT says it.
;
; PER-ROW DIRTY SPANS, in BYTES, and two sets of them (SPEC.md 85.3.1). A
; frame restores the rows the LAST frame's sprites dirtied, draws this frame's,
; and blits the union - so a frame that scrolls costs the whole shadow and a
; frame that does not costs the sprites twice over and nothing else.
;
; Hercules' 720x348 box holds the 640x200 picture at (40, 74), which centres it
; and lands on a byte boundary in x (40 pixels = 5 bytes); its four-way row
; interleave is 0x2000 a bank, 90 bytes a row within one. CGA's two-way is
; 0x2000 a bank, 80 bytes a row.
;
; ATTRIBUTION. Nothing here is derived from the original. The VRAM layout, the
; line compare and the interleaves are facts about IBM and Hercules hardware;
; Lemmings is (C) 1991 DMA Design / Psygnosis and README.TXT carries the full
; attribution.
; =============================================================================

section .text

LEM_KIND_VGA  equ 0
LEM_KIND_CGA  equ 1
LEM_KIND_HERC equ 2

LEM_VSTRIDE   equ 200               ; VGA bytes per world row
LEM_VTERR     equ 8000              ; ...and where the world starts in VRAM
LEM_VCOMP     equ 40000             ; THE FRAME'S COMPOSE SCRATCH: every
LEM_VCOMPH    equ 125               ; rectangle a game frame changes - T, the
                                    ; objects over it, the lemmings and the
                                    ; pointer - is built here, off the glass,
                                    ; and latch-copied on in one pass, so no
                                    ; pixel of any of them is ever missing
                                    ; from the glass (lemsprite.inc
                                    ; lem_r_cband). 125 rows of the world's
                                    ; 200-byte stride, 40,000-64,999, up to
                                    ; the pointer's save-under at 65,000. The
                                    ; sprites keep NO save-under on VGA any
                                    ; more, which is what freed the 7,400
                                    ; bytes the band grew into
LEM_VSPLIT    equ 319               ; the line compare, in SCANLINES - the
                                    ; split STARTS one scanline after the
                                    ; match, so this is 2 * 160 - 1 (see the
                                    ; header: 320 halved the panel's top row)

LEM_SSTRIDE   equ 80                ; the 1bpp/2bpp shadow, bytes per row
LEM_SROWS     equ 200
LEM_VIEWH     equ 160               ; the world's height, and the view's
LEM_HERCX     equ 5                 ; the 640x200 box at x = 40 pixels...
LEM_HERCY     equ 74                ; ...y = 74, in a 720x348 frame

VGA_SEQ_IDX   equ 0x3C4
VGA_GC_IDX    equ 0x3CE
VGA_AC_IDX    equ 0x3C0
VGA_AC_RD     equ 0x3C1
VGA_DAC_WIDX  equ 0x3C8
VGA_DAC_DATA  equ 0x3C9
BDA_SEG       equ 0x40
BDA_CRTC      equ 0x63

; -----------------------------------------------------------------------------
; internal helpers
; -----------------------------------------------------------------------------
; lb_crtc - out: DX = the CRTC index port, read from the BDA and never assumed
; to be 0x3D4. Clobbers DX only.
lb_crtc:
    push ax
    push es
    mov ax, BDA_SEG
    mov es, ax
    mov dx, [es:BDA_CRTC]
    pop es
    pop ax
    ret

; lb_gc - write AH into Graphics Controller index AL. Clobbers AX, DX.
;
; THE INDEX GOES OUT FIRST AND THE VALUE SECOND, which is the whole of what an
; indexed register pair is - and the first version of this had an `xchg al, ah`
; in front of the first `out`, so every one of the nine calls below wrote its
; VALUE to the index port and its INDEX to whatever that selected. What it
; looked like was a completely black screen in a correctly set mode 0Dh with
; the split and the start address correct (build/port-shots/wave2-vga-black.png):
; nothing on the glass, no crash, and nothing in the C to blame.
lb_gc:
    mov dx, VGA_GC_IDX
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    ret

; lb_seq - write AH into Sequencer index AL. Clobbers AX, DX. Same rule.
lb_seq:
    mov dx, VGA_SEQ_IDX
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    ret

; lb_rowoff8 - in: AX = row. out: DI = row * LEM_SSTRIDE (80). AX kept.
lb_rowoff8:
    push ax
    push cx
    push dx
    mov di, ax
    mov cl, 4
    shl di, cl                      ; row*16
    mov dx, ax
    mov cl, 6
    shl dx, cl                      ; row*64
    add di, dx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
;                  unsigned shadowseg)
;
; Everything the raster needs that does not change inside one level. The FSI
; block's segment is passed rather than assumed: SPEC.md 53's contract is that
; os88_fsx_mode() describes the mode it just set, and a Hercules second page
; would change it.
; -----------------------------------------------------------------------------
_lem_r_setup:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lb_kind], ax
    mov ax, [bp+6]
    mov [lb_fb], ax
    mov ax, [bp+8]
    mov [lb_world], ax
    mov ax, [bp+10]
    mov [lb_shadow], ax
    mov word [lb_scroll], -1        ; "no view yet", so the first lem_r_scroll
                                    ; always composes
    xor ax, ax                      ; ...and no sprite or pointer is on a glass
    mov [lb_spn], ax                ; that a mode set has just cleared: a
    mov [lb_curn0], ax              ; stale save-under from the last bracket
                                    ; would be restored over the new one
    mov [lb_curn1], ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_prep(void) - put the card into the game's layout.
;
; VGA only: everything the two 1bpp backends need was done by the mode set.
;
; IT MUST SET THE START ADDRESS AND THE FIRST VERSION DID NOT, which is the
; whole loading screen wrong for the twenty-five seconds a level takes to
; compose. The Offset write below makes a display row 200 bytes rather than
; mode 0Dh's 40, and the world lives at LEM_VTERR while the panel lives at 0 -
; so a CRTC Start Address of 0, which is what int 10h left, shows display row r
; as bytes r*200..r*200+39: the PANEL twice (once above the split out of the
; 40 bytes of each 200, once below it where it belongs) and, under it, world
; rows 0..119 at world x 0 rather than the level's own ScreenPosition.
; build/port-shots/wave2f-vga-loading.png photographed exactly that. The first
; lem_r_scroll() of a launch is in lem_frame_first(), AFTER the compose, so
; nothing corrected it until the level was already built.
;
; It is lem_r_start and NOT lem_r_scroll: lem_r_scroll writes [lb_scroll], and
; leaving that at -1 is what makes the shadow backends' first scroll compose at
; all. This arm is VGA-only and the two 1bpp backends reach .done above it.
;
; A HERCULES LETTERBOX BORDER IS NOT CLEARED HERE, and it was tried. The game
; is a 640x200 box at (40,74) of a 720x348 frame and FSXM_HERC is the mode the
; DESKTOP is already in, so nothing clears what is outside the box; under QEMU
; with HERCSEG= a frame of leftovers shows around the level. A `rep stosw` over
; all four banks was written, measured and TAKEN OUT: with 0xFF it demonstrably
; reaches banks 1..3 and not the first 720 bytes of bank 0, and with 0x00 the
; frame it produces is byte for byte the frame without it (51,624 lit pixels
; either way) - so it cannot be shown to fix anything, and the residue's shape
; (only rows where row MOD 4 = 0 carry it) is the shape of a LINEAR image read
; through a BANKED de-interleave, which is the emulator's own arrangement
; rather than this program's. It wants a machine that can show the border,
; which is 86Box or the 5150, before anything is spent on it.
; -----------------------------------------------------------------------------
_lem_r_prep:
    push bp
    mov bp, sp
    cmp word [lb_kind], LEM_KIND_VGA
    jne .done

    call lb_crtc                    ; Offset = 100 words = 200 bytes a row
    mov al, 0x13
    out dx, al
    inc dx
    mov al, LEM_VSTRIDE / 2
    out dx, al

    mov ax, LEM_VSPLIT
    push ax
    call lem_r_split
    add sp, 2

    mov ax, 1                       ; Pixel Panning Mode: the panel below the
    push ax                         ; split does NOT pan with the world
    call lem_r_panmode
    add sp, 2

    mov ah, 0                       ; write mode 0, read mode 0
    mov al, 5
    call lb_gc
    mov ah, 0xFF                    ; every bit of the byte
    mov al, 8
    call lb_gc
    mov ah, 0                       ; no data rotate, no set/reset
    mov al, 3
    call lb_gc
    mov ah, 0
    mov al, 1
    call lb_gc

    xor ax, ax                      ; ...and the top window at the WORLD, pan 0
    push ax                         ; (see the header: without this the card
    mov ax, LEM_VTERR               ; displays the panel twice and world x 0
    push ax                         ; until the first scroll, which is after
    call lem_r_start                ; the whole compose)
    add sp, 4
.done:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_unprep(void) - the line compare past the bottom of any mode and
; the Offset back to what the BIOS set, so the mode change on the way out
; starts from a card in a state the BIOS would recognise.
; -----------------------------------------------------------------------------
_lem_r_unprep:
    push bp
    mov bp, sp
    cmp word [lb_kind], LEM_KIND_VGA
    jne .done
    mov ax, 0x03FF
    push ax
    call lem_r_split
    add sp, 2
    xor ax, ax
    push ax
    call lem_r_panmode
    add sp, 2
    call lb_crtc
    mov al, 0x13
    out dx, al
    inc dx
    mov al, 0x14                    ; mode 0Dh's own 40 bytes a row
    out dx, al
    xor ax, ax
    push ax
    push ax
    call lem_r_start
    add sp, 4
.done:
    pop bp
    ret

; -----------------------------------------------------------------------------
; lem_r_split(unsigned scanline) - THE CRTC LINE COMPARE, in three pieces.
;
;   0x18 Line Compare      bits 0-7
;   0x07 Overflow    bit 4 bit 8
;   0x09 Max Scan Line bit 6 bit 9
;
; Two of those hold something else in every one of their other bits, so both
; are read back and merged. Writing 0x09 whole is the classic way to lose a
; 200-line mode's double scan and get a 400-line picture.
; -----------------------------------------------------------------------------
lem_r_split:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call lb_crtc
    mov al, 0x18
    out dx, al
    inc dx
    mov ax, si
    out dx, al
    dec dx
    mov al, 0x07
    out dx, al
    inc dx
    in al, dx
    and al, 0xEF
    test si, 0x0100
    jz .o8
    or al, 0x10
.o8:
    out dx, al
    dec dx
    mov al, 0x09
    out dx, al
    inc dx
    in al, dx
    and al, 0xBF
    test si, 0x0200
    jz .o9
    or al, 0x40
.o9:
    out dx, al
    pop si
    pop bp
    ret

; lem_r_panmode(int on) - Attribute Controller 0x10 bit 5. Set, the rows below
; the line compare are not pel panned; clear, the panel jitters by up to seven
; pixels while the world scrolls smoothly, which is easy to blame on the scroll.
lem_r_panmode:
    push bp
    mov bp, sp
    call lb_crtc
    add dx, 6
    in al, dx                       ; reset the AC's index/data flip-flop
    mov dx, VGA_AC_IDX
    mov al, 0x10 | 0x20
    out dx, al
    mov dx, VGA_AC_RD
    in al, dx
    mov dx, VGA_AC_IDX              ; the flip-flop is toggled by WRITES to
    and al, 0xDF                    ; 0x3C0 only, so it is still expecting the
    cmp word [bp+4], 0              ; DATA and this write is that data
    je .put
    or al, 0x20
.put:
    out dx, al
    pop bp
    ret

; lem_r_start(unsigned addr, int pan) - one frame of scrolling: Start Address
; High/Low move the picture eight pixels at a time and the Attribute
; Controller's Horizontal Pixel Panning moves it the other seven.
lem_r_start:
    push bp
    mov bp, sp
    call lb_crtc
    mov al, 0x0C
    out dx, al
    inc dx
    mov ax, [bp+4]
    mov al, ah
    out dx, al
    dec dx
    mov al, 0x0D
    out dx, al
    inc dx
    mov ax, [bp+4]
    out dx, al
    add dx, 5
    in al, dx
    mov dx, VGA_AC_IDX
    mov al, 0x13 | 0x20
    out dx, al
    mov ax, [bp+6]
    and al, 7
    out dx, al
    pop bp
    ret

; void lem_r_vstart(int x) / void lem_r_vpan(void) - ONE VGA SCROLL STEP IN
; TWO HALVES, so the C can put a retrace between them (SPEC.md 116.4.6). The
; CRTC latches the Start Address at the start of vertical retrace and the
; Attribute Controller's pel pan takes effect AT ONCE, so writing both
; together showed every 4-pixel step as one refresh of the old start with the
; new pan - a shear of up to seven pixels. lem_r_vstart sets [lb_scroll] and
; the Start Address only; the caller waits for retrace (os88_fsx_wait,
; FSXW_VSYNC) and then lem_r_vpan writes the pel pan, so both land on the same
; frame - Abrash's order. Neither touches a byte of VRAM.
_lem_r_vstart:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    cmp ax, 0
    jge .lo
    xor ax, ax
.lo:
    cmp ax, LEM_MWORLD - 320
    jle .hi
    mov ax, LEM_MWORLD - 320
.hi:
    mov [lb_scroll], ax
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add ax, LEM_VTERR
    mov bx, ax
    call lb_crtc
    mov al, 0x0C
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    dec dx
    mov al, 0x0D
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    pop bp
    ret

_lem_r_vpan:
    call lb_crtc
    add dx, 6
    in al, dx                       ; reset the AC's flip-flop
    mov dx, VGA_AC_IDX
    mov al, 0x13 | 0x20
    out dx, al
    mov ax, [lb_scroll]
    and al, 7
    out dx, al
    ret

; -----------------------------------------------------------------------------
; void lem_r_pal(unsigned seg, unsigned stdoff, unsigned cusoff)
;
; THE LEVEL'S SIXTEEN COLOURS, and every one of them is DATA. Entries 0..6 are
; the style bank's `vga_standard` - the lemmings' and the panel's colours, read
; out of GROUNDxO.DAT by tools/os88lem.py - 8..15 are its `vga_custom`, the
; level's own, and entry 7 is a COPY OF ENTRY 8, which is what
; lemmings_3ds/src/import/import_level.c:102-103 does
; (`palette[7] = palette[8]`) and is the reason plane 3 alone separates terrain
; from sprite. The components are already 6-bit as the DAC wants them.
;
; The Attribute Controller's own palette registers are set to the IDENTITY
; first. Mode 0Dh boots with the EGA's 16-of-64 default (00 01 02 03 04 05 14
; 07 38 39 ...), so without this, colour 6 would read DAC entry 0x14 and the
; level's browns would come out of somebody else's table.
; -----------------------------------------------------------------------------
_lem_r_pal:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out

    call lb_acid                    ; the AC palette registers, identity

    ; --- the DAC, sixteen entries of three 6-bit components ---------------
    mov dx, VGA_DAC_WIDX
    xor al, al
    out dx, al
    mov ax, [bp+4]
    mov ds, ax
    mov si, [bp+6]                  ; vga_standard
    mov cx, 7                       ; ...entries 0..6 only
    mov dx, VGA_DAC_DATA
.std:
    lodsb
    out dx, al
    lodsb
    out dx, al
    lodsb
    out dx, al
    loop .std
    mov si, [bp+8]                  ; vga_custom - and entry 7 is a COPY of
    mov al, [si]                    ; entry 8, which is import_level.c:103
    out dx, al
    mov al, [si+1]
    out dx, al
    mov al, [si+2]
    out dx, al
    mov cx, 8                       ; ...and then 8..15 are custom[0..7]
.cus:
    lodsb
    out dx, al
    lodsb
    out dx, al
    lodsb
    out dx, al
    loop .cus
.out:
    pop ds
    pop di
    pop si
    pop bp
    ret

; lb_acid - the Attribute Controller's sixteen palette registers to the
; IDENTITY, so DAC entry n is colour n (lem_r_pal's header says why: mode 0Dh,
; and mode 12h with it, boot with the EGA's 16-of-64 default). SHARED with
; lemfont.inc's lem_p_pal, which needs the same sixteen writes for the two
; screens' mode 12h (wave 5). Clobbers AX, BX, DX.
lb_acid:
    call lb_crtc
    add dx, 6
    in al, dx                       ; reset the AC's index/data flip-flop
    xor bx, bx
.acp:
    mov dx, VGA_AC_IDX
    mov al, bl
    or al, 0x20                     ; keep the palette ENABLED throughout
    out dx, al
    mov al, bl
    out dx, al
    inc bx
    cmp bx, 16
    jb .acp
    ret

; -----------------------------------------------------------------------------
; void lem_r_clearworld(int vram) - the world surface to colour 0.
;
; VGA: T's three colour planes in RAM, and with vram != 0 the world's 32,000
; bytes of VRAM too - all four planes at once, which the Sequencer's Map Mask
; makes one `rep stosw` rather than four. The preview passes 0: its mode is
; 12h, where those bytes are its own rows 35 on, and the play mode's BIOS set
; clears them before ovl_frame_first copies T over them anyway. The two 1bpp
; backends have nothing to do: their world claim is written whole by
; lem_m_derive().
; -----------------------------------------------------------------------------
_lem_r_clearworld:
    push bp
    mov bp, sp
    push di
    push es
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
    cmp word [bp+4], 0
    je .tonly
    mov ah, 0x0F                    ; Map Mask: every plane
    mov al, 2
    call lb_seq
    mov ax, [lb_fb]
    mov es, ax
    mov di, LEM_VTERR
    xor ax, ax
    mov cx, (LEM_VSTRIDE * LEM_VIEWH) / 2
    cld
    rep stosw
    ; ...and T's three colour planes, which the RAM composer draws into (plane
    ; 3 is the mask's, cleared by lem_m_clear and copied over by lem_r_tmask)
.tonly:
    cld
    mov bx, [lb_t]
    cmp bx, 0
    je .out
    mov dx, 3
.tp:
    mov es, bx
    xor di, di
    xor ax, ax
    mov cx, (LEM_MSTRIDE * LEM_MROWS) / 2
    rep stosw
    add bx, LEM_TPLANE
    dec dx
    jnz .tp
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE WORLD IS COMPOSED IN RAM AND COPIED TO THE CARD ONCE (SPEC.md 116.7.2 -
; wave 3's first design decision, and a measured one: 22.0 ms a 32x16 piece
; into T and the mask together, 330 ms for the copy, on a 4.77 MHz 8088).
;
; Wave 2 composed straight into the four VGA planes, a read-modify-write per
; byte per plane under the Read Map Select and the Map Mask, and measured it at
; 53.8 ms a 32x16 piece plus 8.8 ms more for the same piece into the solid mask
; - ~25 seconds for a 400-piece level on a 4.77 MHz 8088, with the machine
; looking dead for all of it. The design that replaced it is in two halves:
;
;   lem_r_wpiece   one terrain piece into the world PLANES IN RAM: the T claim's
;                  planes 0..2 (a VGA only - the shadow backends have no colour
;                  planes and pass 0) and plane 3, which is the SOLID MASK
;                  itself. One pass per plane per row, no port I/O anywhere, and
;                  the mask is written by the same pass that writes the picture
;                  rather than by a second routine walking the same piece again
;   lem_r_tload    the four RAM planes into VRAM, once: four `rep movsw` of
;                  16,000 words under the Map Mask
;
; THE PLANES ARE KEPT IN RAM FOR THE LEVEL'S LIFE on a VGA (the T claim), and
; that is what pays for most of wave 3's raster: an animated object or a
; destroyed rectangle is put back by COPYING T into VRAM, and leaving the
; bracket with 'f' and coming back in is one more lem_r_tload rather than a
; second compose (SPEC.md 116.4.1).
;
; THE PACKED-NIBBLE ALTERNATIVE WAS BENCHED AND LOST (tests/lembench's two LUT
; rows): it composes into a 126,720-byte packed world and then converts it to
; planes through a 256-entry table. Its conversion pass ALONE is 7,810 ms -
; 24 times this file's whole tload - before the swizzle a planar piece needs on the way
; in - and planar RAM is the SAME claim size, needs no table and lets plane 3
; be the mask.
;
; THE FLAG RULE IS LEMMIX'S (src/Game.Rendering.pas PrepareTerrainBitmap):
; no-overwrite WINS over erase when a piece carries both. Wave 2 let erase win,
; which turned an (erase | no-overwrite) piece - lemmings_3ds notes one in
; LEVEL002.DAT section 2 - into a piece that drew nothing, where Lemmix draws
; it under the existing terrain.
; -----------------------------------------------------------------------------

; void lem_r_wsetup(unsigned p0, unsigned p3) - where lem_r_wpiece writes.
; p0 = plane 0 of the T claim, planes 1 and 2 following at +LEM_TPLANE
; paragraphs each, or 0 = no colour planes; p3 = plane 3, the solid mask.
LEM_TPLANE    equ 2000              ; 32,000 bytes = one world plane, paragraphs

_lem_r_wsetup:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lb_wp0], ax
    mov ax, [bp+6]
    mov [lb_wp3], ax
    pop bp
    ret

; void lem_r_tsetup(unsigned t) - the T claim for the backends that keep one
; (VGA): four planes of 32,000 bytes at t, t+2000, t+4000, t+6000. 0 = none.
_lem_r_tsetup:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lb_t], ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_wpiece(unsigned seg, unsigned off, int x, int y, int w, int h,
;                   int flags)
;
; `seg:off` is the piece's PLANE 0 in the style bank; planes 1..3 follow at
; one plane stride each (w/8 * h bytes) and plane 3 IS the transparency mask
; (docs/lemband-format.md). `flags` is the LVL entry's top nibble.
;
; The shift identity is dest[i] = LOW BYTE OF ((src[i-1] << 8) | src[i]) >> s,
; src[-1] = src[bpr] = 0, and a negative x reconstructs through `sar` and
; `and 7` (wave 2's lem_m_piece header has the worked case). The clip in x is
; done ONCE per piece as a byte range [i0, i1] of the bpr+1 destination bytes a
; shifted row touches, so the inner loops carry no bounds test at all.
;
; Locals:
;   [bp-2]  source row r           [bp-14] i1, the last byte index
;   [bp-4]  bpr                    [bp-16] this row's dest offset (at i0)
;   [bp-6]  plane stride           [bp-18] plane index p
;   [bp-8]  shift s                [bp-20] this row's source offset (plane 0)
;   [bp-10] first dest byte col    [bp-22] mode: 0 draw, 1 erase, 2 no-overwrite
;   [bp-12] i0, the first index    [bp-24] main count: bytes i0.. with a cur
;   [bp-26] tail: 1 = byte bpr (cur = 0) is inside [i0, i1]
; -----------------------------------------------------------------------------
_lem_r_wpiece:
    push bp
    mov bp, sp
    sub sp, 26
    push si
    push di
    push es
    push ds
    mov ax, [bp+14]                 ; h
    cmp ax, 0
    jg .hok
    jmp .out
.hok:
    mov ax, [bp+12]                 ; w
    cmp ax, 0
    jg .wok
    jmp .out
.wok:
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [bp-4], ax                  ; bpr
    mul word [bp+14]
    mov [bp-6], ax                  ; plane stride
    mov ax, [bp+8]                  ; x
    mov cx, ax
    and cx, 7
    mov [bp-8], cx                  ; s
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov [bp-10], ax                 ; bx0
    ; i0 = max(0, -bx0)
    neg ax
    cmp ax, 0
    jge .i0ok
    xor ax, ax
.i0ok:
    mov [bp-12], ax
    ; i1 = min(bpr, LEM_MSTRIDE - 1 - bx0)
    mov ax, LEM_MSTRIDE - 1
    sub ax, [bp-10]
    cmp ax, [bp-4]
    jle .i1ok
    mov ax, [bp-4]
.i1ok:
    mov [bp-14], ax
    cmp ax, [bp-12]
    jge .rngok
    jmp .out                        ; wholly off the world's left or right
.rngok:
    ; the tail byte (index bpr, whose cur is 0) is in range iff i1 == bpr
    xor bx, bx
    cmp ax, [bp-4]
    jne .notail
    inc bx
    dec ax                          ; the main loop's last index is bpr - 1
.notail:
    mov [bp-26], bx
    sub ax, [bp-12]
    inc ax
    mov [bp-24], ax                 ; main count, may be 0 (i0 == bpr)
    ; the mode, Lemmix's precedence: no-overwrite, then erase, then draw
    xor ax, ax
    test word [bp+16], LEM_TF_NOOVER
    jz .noover0
    mov al, 2
    jmp .modeok
.noover0:
    test word [bp+16], LEM_TF_ERASE
    jz .modeok
    mov al, 1
.modeok:
    mov [bp-22], ax
    mov ax, [bp+6]                  ; off
    add ax, [bp-12]                 ; + i0: every source row starts there
    mov [bp-20], ax
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, [bp+14]
    jl .rowgo
    jmp .out
.rowgo:
    test word [bp+16], LEM_TF_UPDOWN
    jz .plain
    mov ax, [bp+14]
    dec ax
    sub ax, [bp-2]
.plain:
    add ax, [bp+10]                 ; + y
    cmp ax, LEM_MROWS
    jb .inrow
    jmp .nextrow                    ; unsigned: catches a negative row too
.inrow:
    call lem_rowoff                 ; DI = dy * 200
    add di, [bp-10]
    add di, [bp-12]
    mov [bp-16], di

    ; --- M: plane 3 shifted, into lb_mrow, and the no-overwrite AND -------
    mov si, [bp-20]
    mov ax, [bp-6]
    add si, ax
    add si, ax
    add si, ax                      ; plane 3
    mov ax, [bp+4]
    mov cx, [bp-8]                  ; CL = s
    push cs
    pop es
    mov di, lb_mrow
    mov ds, ax                      ; the bank
    call lb_wshift                  ; lb_mrow[0..n-1], DS left as the bank
    push cs
    pop ds
    cmp word [bp-22], 2
    jne .mdone
    ; M &= ~dest3, against the mask as it is BEFORE this piece
    mov ax, [lb_wp3]
    mov es, ax
    mov di, [bp-16]
    mov si, lb_mrow
    mov cx, [bp-24]
    add cx, [bp-26]
.nov:
    mov al, [es:di]
    not al
    and [si], al
    inc si
    inc di
    loop .nov
.mdone:

    ; --- the planes: 0..2 where there is a T claim, then 3 ----------------
    mov word [bp-18], 0
    cmp word [lb_wp0], 0
    jne .plane
    mov word [bp-18], 3
.plane:
    ; the destination segment
    mov ax, [bp-18]
    cmp ax, 3
    jne .pcol
    mov ax, [lb_wp3]
    jmp .pseg
.pcol:
    mov bx, ax
    mov ax, [lb_wp0]
    cmp bx, 0
    je .pseg
    add ax, LEM_TPLANE
    cmp bx, 1
    je .pseg
    add ax, LEM_TPLANE
.pseg:
    mov es, ax
    mov di, [bp-16]
    mov bx, lb_mrow
    cmp word [bp-22], 1
    jne .pdraw
    ; --- erase: d &= ~M, no source at all --------------------------------
    mov cx, [bp-24]
    add cx, [bp-26]
.er:
    mov al, [bx]
    not al
    and [es:di], al
    inc bx
    inc di
    loop .er
    jmp .pnext
.pdraw:
    ; --- plane 3 is M itself: the shifted plane 3 AND M is M, so the
    ; picture's plane 3 and the mask both take d |= M with no source read at
    ; all - which is the whole of the compose on CGA and Hercules -------------
    cmp word [bp-18], 3
    jne .pcolour
    mov cx, [bp-24]
    add cx, [bp-26]
.p3:
    mov al, [bx]
    or [es:di], al
    inc bx
    inc di
    loop .p3
    jmp .pnext
.pcolour:
    ; --- draw: d = (d & ~M) | (shifted src_p & M) -------------------------
    mov si, [bp-20]
    mov cx, [bp-18]
    jcxz .p0
.pp:
    add si, [bp-6]
    loop .pp
.p0:
    mov cx, [bp-8]                  ; CL = s, CH is the loop's temporary
    mov dx, [bp-24]                 ; DX = the main count
    mov ax, [bp+4]
    push bp
    mov bp, [bp-26]                 ; BP = tail flag: the frame is not read
    mov ds, ax                      ; again until it is popped
    xor ah, ah
    cmp word [cs:lb_wi0], 0         ; prev = src[i0 - 1] when i0 > 0
    je .noprev
    mov ah, [si-1]
.noprev:
    cmp dx, 0
    je .ptail
.pd:
    lodsb                           ; AL = cur
    mov ch, al
    shr ax, cl                      ; AL = the shifted byte
    and al, [cs:bx]
    mov ah, [cs:bx]
    not ah
    and ah, [es:di]
    or al, ah
    stosb
    mov ah, ch                      ; prev = cur
    inc bx
    dec dx
    jnz .pd
.ptail:
    cmp bp, 0
    je .pend
    xor al, al                      ; cur = src[bpr] = 0
    shr ax, cl
    and al, [cs:bx]
    mov ah, [cs:bx]
    not ah
    and ah, [es:di]
    or al, ah
    stosb
.pend:
    pop bp
    push cs
    pop ds
.pnext:
    inc word [bp-18]
    cmp word [bp-18], 4
    jae .nextrow
    jmp .plane
.nextrow:
    mov ax, [bp-4]
    add [bp-20], ax                 ; the next source row
    inc word [bp-2]
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 26                      ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lb_wshift - internal to _lem_r_wpiece: one source row, shifted, out to ES:DI.
; in: DS:SI = src[i0], CL = s, [bp-12] i0, [bp-24] main count, [bp-26] tail.
; It writes main + tail bytes. It sets lb_wi0 = i0, which the plane loop reads
; through CS once DS is the bank's. Clobbers AX, CH, DX, SI, DI.
lb_wshift:
    mov ax, [bp-12]
    mov [cs:lb_wi0], ax
    xor ah, ah
    cmp word [bp-12], 0
    je .nop
    mov ah, [si-1]
.nop:
    mov dx, [bp-24]
    cmp dx, 0
    je .tail
.l:
    lodsb
    mov ch, al
    shr ax, cl
    stosb
    mov ah, ch
    dec dx
    jnz .l
.tail:
    cmp word [bp-26], 0
    je .r
    xor al, al
    shr ax, cl
    stosb
.r:
    ret

; -----------------------------------------------------------------------------
; void lem_r_tload(void) - the T claim's four planes into the VGA world, whole.
; A no-op on the shadow backends, which keep no T claim.
; -----------------------------------------------------------------------------
_lem_r_tload:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
    mov bx, [lb_t]
    cmp bx, 0
    je .out
    mov dx, 1                       ; the Map Mask bit
.pl:
    push dx
    push bx
    mov ah, dl
    mov al, 2
    call lb_seq
    pop bx
    pop dx
    mov ax, [lb_fb]                 ; ...before DS moves: see lb_wshift
    mov es, ax
    mov di, LEM_VTERR
    xor si, si
    mov ds, bx
    mov cx, (LEM_MSTRIDE * LEM_MROWS) / 2
    cld
    rep movsw
    push cs
    pop ds
    add bx, LEM_TPLANE
    shl dl, 1
    cmp dl, 16
    jb .pl
    mov ah, 0x0F                    ; every plane again, the state the rest of
    mov al, 2                       ; this file assumes
    call lb_seq
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; void lem_r_tmask(void) - the solid mask into T's plane 3, after a compose.
; Plane 3 of the PICTURE starts equal to solidity and then parts from it the
; first time a builder lays a brick, which is colour 7: solid, and plane 3
; clear (lemmings_main_dat_file_format.txt section 2). VGA only.
_lem_r_tmask:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
    mov ax, [lb_t]
    cmp ax, 0
    je .out
    add ax, 3 * LEM_TPLANE
    mov es, ax
    xor di, di
    xor si, si
    mov ax, [lm_seg]
    mov ds, ax
    mov cx, (LEM_MSTRIDE * LEM_MROWS) / 2
    cld
    rep movsw
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_derive(void) - the 1bpp/2bpp world out of the solid mask.
; A thin cover over lem_m_derive() so lemdraw.c names one raster interface.
; -----------------------------------------------------------------------------
_lem_r_derive:
    push bp
    mov bp, sp
    cmp word [lb_kind], LEM_KIND_VGA
    je .out
    mov ax, [lb_kind]
    dec ax                          ; 1 -> CGA 0, 2 -> Hercules 1
    push ax
    push word [lb_world]
    call _lem_m_derive
    add sp, 4
.out:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_scroll(int x) - the view's left edge, in WORLD pixels.
;
; VGA: four `out`s and nothing else. The two 1bpp backends compose the window
; into the shadow, and only when it MOVED - a frame that does not scroll costs
; nothing here at all.
; -----------------------------------------------------------------------------
_lem_r_scroll:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    mov ax, [bp+4]
    cmp ax, 0
    jge .lo
    xor ax, ax
.lo:
    cmp ax, LEM_MWORLD - 320
    jle .hi
    mov ax, LEM_MWORLD - 320
.hi:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .shadow

    mov [lb_scroll], ax
    mov bx, ax
    and bx, 7
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add ax, LEM_VTERR
    push bx
    push ax
    call lem_r_start
    add sp, 4
    jmp .out

.shadow:
    ; the copy is byte aligned, so the step is 4 pixels on CGA and 8 on
    ; Hercules (see the file header - it is a stated fact, not a defect)
    cmp word [lb_kind], LEM_KIND_CGA
    jne .h8
    and ax, 0xFFFC
    jmp .snap
.h8:
    and ax, 0xFFF8
.snap:
    cmp ax, [lb_scroll]
    je .out                         ; the view did not move: nothing to compose
    mov [lb_scroll], ax
    call lb_compose
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lb_compose - internal: the 320-pixel window of the world claim into shadow
; rows 0..159, and every one of them marked dirty to the full width.
lb_compose:
    push bp
    mov bp, sp
    sub sp, 4                       ; [bp-2] row, [bp-4] source byte offset
    push si
    push di
    push es
    push ds
    mov ax, [lb_scroll]
    cmp word [lb_kind], LEM_KIND_CGA
    jne .hsrc
    shr ax, 1
    shr ax, 1                       ; 2bpp: 4 pixels a byte
    mov [bp-4], ax
    jmp .go
.hsrc:
    shr ax, 1
    shr ax, 1
    shr ax, 1                       ; 1bpp: 8 pixels a byte
    mov [bp-4], ax
.go:
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, LEM_VIEWH
    jge .done
    call lb_rowoff8
    push di                         ; the shadow row
    cmp word [lb_kind], LEM_KIND_CGA
    jne .hrow

    ; --- CGA: 400 bytes a world row, 80 of them copied straight ----------
    call lem_rowoff                 ; DI = row * 200
    shl di, 1                       ; ...and the CGA world is 400
    add di, [bp-4]
    mov si, di
    pop di
    mov ax, [lb_shadow]             ; ...before DS moves: see lb_pplane
    mov es, ax
    mov ax, [lb_world]
    mov ds, ax
    mov cx, LEM_SSTRIDE / 2
    cld
    rep movsw
    jmp .rowdone

    ; --- Hercules: 40 world bytes DOUBLED into 80 shadow bytes ------------
.hrow:
    call lem_rowoff                 ; DI = row * 200
    add di, [bp-4]
    mov si, di
    pop di
    mov ax, [lb_shadow]             ; ...before DS moves: see lb_pplane
    mov es, ax
    mov ax, [lb_world]
    mov ds, ax
    mov cx, 40
    cld
.hb:
    lodsb
    mov bl, al
    xor bh, bh
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov ah, [cs:lb_dbl+bx]
    push ax
    mov al, ah
    stosb
    pop ax
    mov bl, al
    and bl, 15
    xor bh, bh
    mov al, [cs:lb_dbl+bx]
    stosb
    loop .hb

.rowdone:
    push cs
    pop ds
    mov ax, LEM_SSTRIDE - 1
    push ax                         ; cdecl: right to left, so b1 first and the
    xor ax, ax                      ; row last (apps/cword/cwmove.inc)
    push ax
    push word [bp-2]
    call lem_r_dirty
    add sp, 6
    mov ax, [bp-2]
    and ax, 15
    cmp ax, 15
    jne .nms
    call lb_msample                 ; the pointer's buttons, every 16 rows
.nms:
    inc word [bp-2]
    jmp .row
.done:
    pop ds
    pop es
    pop di
    pop si
    add sp, 4                       ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lb_msample - THE BUTTONS, LATCHED, IN THE MIDDLE OF A LONG DRAW (SPEC.md
; 116.7 point 3). os88_mouse() answers a LEVEL and has no press latch, and on
; the two shadow backends a scrolling tick is ~178 ms of compose and present
; (SPEC.md 116.7.1) - longer than a click. So the compose and the present ask
; OSAPI_MOUSE every sixteen rows they move and OR the buttons in here, and
; lem_r_mlatch() hands the C the union: a click that went down and up inside
; one long draw is still a press. A far call (46.7 us) a sixteenth of the
; rows; nothing at all on a tick that moved no row. Every register kept.
;
; THE VGA COMPOSE ASKS TOO (wave 6's review): a busy VGA pass is ~1.35 s of
; compose (SPEC.md 116.7.4) and nothing in it sampled, so a click inside one
; was lost. lemsprite.inc's lem_r_sprite, lem_r_obj, lem_r_bgrect,
; lem_r_clatch and lem_r_spray's drawing arm each call lb_msvga once on entry -
; ON VGA ONLY, ~3 far calls a lemming, ~9 ms of a 1.35 s compose at sixty. The
; two shadow cards already sample every sixteen rows above and in the present,
; so there lb_msvga is one compare and no far call (wave 6's second review:
; ~100 far calls a busy shadow frame bought nothing). The POINTER still moves
; once a pass; only the buttons are latched.
lb_msvga:
    cmp word [lb_kind], LEM_KIND_VGA
    je lb_msample
    ret
lb_msample:
    push ax
    push cx
    push dx
    call OSAPI_MOUSE
    or [cs:lb_mlatch], al
    pop dx
    pop cx
    pop ax
    ret

; int lem_r_mlatch(void) - the buttons lb_msample saw since the last call,
; and the latch cleared.
_lem_r_mlatch:
    xor ax, ax
    xchg al, [cs:lb_mlatch]
    ret

; -----------------------------------------------------------------------------
; void lem_r_dirty(int row, int b0, int b1) - mark shadow row `row` dirty from
; byte b0 to byte b1 inclusive. THE SPAN IS IN BYTES, which is the unit the
; presenter moves in; a pixel-accurate span would be split back into bytes at
; the blit anyway (SPEC.md 85.3.1).
; -----------------------------------------------------------------------------
_lem_r_dirty:
lem_r_dirty:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    cmp si, 0
    jl .out
    cmp si, LEM_SROWS
    jge .out
    mov ax, [bp+6]
    cmp ax, 0
    jge .l0
    xor ax, ax
.l0:
    mov bx, [bp+8]
    cmp bx, LEM_SSTRIDE
    jl .h0
    mov bx, LEM_SSTRIDE - 1
.h0:
    cmp bx, ax
    jl .out
    cmp byte [lb_dlo+si], 0xFF
    jne .widen
    mov [lb_dlo+si], al
    mov [lb_dhi+si], bl
    jmp .out
.widen:
    cmp al, [lb_dlo+si]
    jae .nolo
    mov [lb_dlo+si], al
.nolo:
    cmp bl, [lb_dhi+si]
    jbe .out
    mov [lb_dhi+si], bl
.out:
    pop si
    pop bp
    ret

; void lem_r_undirty(void) - every row clean. The presenter calls it after it
; has blitted, so a frame's spans never leak into the next one.
_lem_r_undirty:
    push bp
    mov bp, sp
    push di
    push es
    push cs
    pop es
    mov di, lb_dlo
    mov al, 0xFF
    mov cx, LEM_SROWS
    cld
    rep stosb
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_present(void) - the shadow onto the glass, dirty rows only.
;
; VGA has nothing to present: every write went into VRAM as it was made. The
; two 1bpp backends blit, and the interleave is the whole of the difference
; between them.
; -----------------------------------------------------------------------------
_lem_r_present:
    push bp
    mov bp, sp
    sub sp, 2                       ; [bp-2] row
    push si
    push di
    push es
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    je .out
    mov word [bp-2], 0
.row:
    mov si, [bp-2]
    cmp si, LEM_SROWS
    jge .out
    cmp byte [lb_dlo+si], 0xFF
    je .next
    mov al, [lb_dlo+si]
    xor ah, ah
    mov bx, ax                      ; BX = the first byte
    mov al, [lb_dhi+si]
    xor ah, ah
    sub ax, bx
    inc ax
    mov cx, ax                      ; CX = the byte count

    mov ax, si
    call lb_rowoff8
    add di, bx
    push di                         ; the shadow's own offset

    cmp word [lb_kind], LEM_KIND_CGA
    jne .herc
    ; --- CGA mode 4: two banks, 0x2000 apart, 80 bytes a row -------------
    mov ax, si
    shr ax, 1
    push cx
    call lb_rowoff8
    pop cx
    test si, 1
    jz .cno
    add di, 0x2000
.cno:
    add di, bx
    jmp .blit

    ; --- Hercules: four banks, 0x2000 apart, 90 bytes a row, at (40, 74) --
    ; DI = (dest_row >> 2) * 90 + (dest_row & 3) * 0x2000 + LEM_HERCX + b0,
    ; and 90 is 64 + 16 + 8 + 2, so no `mul` runs 200 times a frame.
.herc:
    push cx
    mov ax, si
    add ax, LEM_HERCY
    mov dx, ax
    and dx, 3                       ; the bank
    shr ax, 1
    shr ax, 1                       ; the row within it
    mov di, ax
    mov cl, 6
    shl di, cl                      ; *64
    mov cx, ax
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; *16
    add di, cx
    mov cx, ax
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; *8
    add di, cx
    mov cx, ax
    shl cx, 1                       ; *2
    add di, cx
    mov cl, 13
    shl dx, cl
    add di, dx
    pop cx
    add di, LEM_HERCX
    add di, bx

.blit:
    mov ax, [lb_fb]
    mov es, ax
    pop si
    push ds
    mov ax, [lb_shadow]
    mov ds, ax
    cld
    rep movsb
    pop ds
    mov si, [bp-2]
    mov ax, si
    and ax, 15
    cmp ax, 15
    jne .next
    call lb_msample                 ; a press inside a long present is kept
.next:
    inc word [bp-2]
    jmp .row
.out:
    call _lem_r_undirty
    pop ds
    pop es
    pop di
    pop si
    add sp, 2                       ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_panel(unsigned seg, unsigned off)
;
; THE 320x40 SKILL PANEL, from MAIN.DAT section 6 offset 0 through
; tools/os88lem.py's LEMMAIN bank. It is FOUR PLANES of 1,600 bytes - 40 bytes
; a row, 40 rows - which on VGA is four straight copies and on the two
; paletteless adapters is a colour-CLASS conversion, because those cards have
; no palette to put it in (SPEC.md 39.4, 116.4.2): nibble 0 is black, 1..7 is
; the SPRITE/panel/text class - the panel, its buttons and their captions - and
; 8..15 the TERRAIN class, which is the direction lb_spix, SPEC.md 116.4 and
; README.TXT all state. THE FIRST VERSION OF THIS COMMENT HAD IT THE OTHER WAY
; ROUND, which is the fourth on-the-glass defect of this wave left behind in
; the one place a reader of the panel path would look
; (build/port-shots/wave2-herc-inverted.png is what the code reading that way
; round looks like).
;
; WHAT THE CLASS BECOMES IS PER CARD and the second version of this comment had
; THAT wrong: only Hercules is 1bpp, where the sprite class is solid and the
; terrain class a 50% dither. A CGA here is mode 4, TWO bits a pixel, so
; lb_spix gives the sprite class colour 3 and the terrain class colour 2 and
; nothing on a CGA is dithered at all.
;
; Locals: [bp-2] row, [bp-4] plane, [bp-6] column.
; -----------------------------------------------------------------------------
_lem_r_panel:
    push bp
    mov bp, sp
    sub sp, 6
    push si
    push di
    push es
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    jne .shadow

    ; --- VGA: four copies of 40 bytes x 40 rows into VRAM at 0 ------------
    mov word [bp-4], 0
.vplane:
    mov ax, 1
    mov cx, [bp-4]
    shl ax, cl
    mov ah, al
    mov al, 2                       ; Map Mask
    call lb_seq
    mov word [bp-2], 0
.vrow:
    mov ax, [bp-2]
    cmp ax, 40
    jge .vnext
    call lem_rowoff                 ; DI = row * 200, the VGA panel stride
    push di
    ; source: off + plane*1600 + row*40
    mov ax, [bp-4]
    mov bx, 1600
    mul bx
    add ax, [bp+6]
    mov si, ax
    mov ax, [bp-2]
    mov bx, 40
    mul bx
    add si, ax
    pop di
    mov ax, [lb_fb]                 ; ...before DS moves: see lb_pplane
    mov es, ax
    mov ax, [bp+4]
    mov ds, ax
    mov cx, 20
    cld
    rep movsw
    push cs
    pop ds
    inc word [bp-2]
    jmp .vrow
.vnext:
    inc word [bp-4]
    cmp word [bp-4], 4
    jb .vplane
    jmp .out

    ; --- CGA and Hercules: one class per pixel, into the shadow -----------
.shadow:
    mov word [bp-2], 0
.srow:
    mov ax, [bp-2]
    cmp ax, 40
    jge .out
    mov word [bp-6], 0
.scol:
    mov ax, [bp-6]
    cmp ax, 320
    jge .srowend
    ; the nibble: bit (7 - col&7) of each of the four planes
    call lb_pnib                    ; AL = the nibble, DS left as ours
    mov bx, [bp-6]
    mov cx, [bp-2]
    add cx, LEM_VIEWH               ; the panel occupies shadow rows 160..199
    call lb_spix
    inc word [bp-6]
    jmp .scol
.srowend:
    inc word [bp-2]
    jmp .srow
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 6                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lb_pnib - internal to _lem_r_panel's shadow arm: the 4bpp nibble at
; ([bp-6], [bp-2]) of the panel bitmap, into AL. DS is ours on entry and exit.
lb_pnib:
    push bx
    push cx
    push dx
    push si
    push ds
    mov ax, [bp-2]
    mov bx, 40
    mul bx
    mov si, ax
    mov ax, [bp-6]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add si, ax
    add si, [bp+6]
    mov cl, [bp-6]
    and cl, 7
    xor cl, 7
    mov ax, [bp+4]
    mov ds, ax
    xor ch, ch                      ; the nibble under construction - CH and
    mov bx, 3                       ; not DX, because `mul` below owns DX
.pl:
    push bx
    mov ax, bx
    mov bx, 1600
    mul bx
    mov bx, ax
    mov al, [si+bx]
    pop bx
    shr al, cl
    and al, 1
    shl ch, 1
    or ch, al
    dec bx
    jns .pl
    push cs
    pop ds
    mov al, ch
    xor ah, ah
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    ret

; lb_spix - internal: plot the colour CLASS of nibble AL at shadow pixel
; (BX, CX). CGA writes two bits, Hercules one, dithered for the middle class.
; Clobbers AX, BX, DX, DI, ES.
lb_spix:
    push si
    push cx
    cmp bx, 0
    jl .out
    cmp bx, 320
    jge .out
    cmp cx, 0
    jl .out
    cmp cx, LEM_SROWS
    jge .out

    ; THE ROW IS MARKED DIRTY HERE, and the first build of this file did not do
    ; it anywhere: lb_compose() marks the rows IT writes, but the panel, the
    ; minimap, the view rectangle and the status line all come through this
    ; plotter, so on CGA and Hercules every one of them was written into the
    ; shadow and never blitted. What it looked like was a correct terrain
    ; picture with a completely black bottom forty rows
    ; (build/port-shots/wave2-cga-nopanel.png).
    ;
    ; IT MARKS THE WHOLE ROW rather than the byte, and that is deliberate: two
    ; byte stores against a cdecl call per PIXEL - the panel alone is 12,800 of
    ; them - and the rows this plotter touches are the panel's forty, which are
    ; redrawn as a block or not at all.
    push bx
    mov bx, cx
    mov byte [lb_dlo+bx], 0
    mov byte [lb_dhi+bx], LEM_SSTRIDE - 1
    pop bx

    push ax
    mov ax, [lb_shadow]
    mov es, ax
    mov ax, cx
    call lb_rowoff8                 ; DI = row * 80
    pop ax
    cmp word [lb_kind], LEM_KIND_CGA
    jne .herc

    ; CGA: 4 pixels a byte, the class in two bits
    mov si, bx
    mov cx, si
    and cl, 3
    xor cl, 3
    shl cl, 1                       ; the bit pair's shift
    shr si, 1
    shr si, 1
    add di, si
    ; THE CLASS, AND WHICH WAY ROUND IT GOES IS THE WHOLE POINT. The level's
    ; palette is 0..7 the LEMMINGS' and the PANEL's and 8..15 the LEVEL's
    ; (lemmings_3ds import_level.c:102, and it is what makes plane 3 mean
    ; "terrain"), so on a card with no palette the sprites, the panel and the
    ; status line are SOLID and the terrain is the second colour - which is
    ; what lem_m_derive() writes the whole world as. The first build had it the
    ; other way round and the status line came out as a row of dashes on
    ; Hercules while the terrain was solid white
    ; (build/port-shots/wave2-herc-inverted.png).
    cmp al, 8
    jae .cterr
    cmp al, 1
    jb .cclr
    mov ah, 3                       ; 1..7: a sprite, the panel, the text
    jmp .cput
.cterr:
    mov ah, 2                       ; 8..15: terrain, the colour lm_nib2 writes
    jmp .cput
.cclr:
    xor ah, ah
.cput:
    mov al, 3
    shl al, cl
    not al
    and [es:di], al
    mov al, ah
    shl al, cl
    or [es:di], al
    jmp .out

    ; Hercules: one pixel a bit, doubled - so a source pixel is TWO bits
.herc:
    mov si, bx
    shl si, 1                       ; the doubled x
    mov cx, si
    and cl, 7
    xor cl, 7
    shr si, 1
    shr si, 1
    shr si, 1
    add di, si
    ; ...and the same the same way round on Hercules: 1..7 SOLID, 8..15
    ; dithered 50% so a solid lemming stays legible on top of the terrain, 0
    ; black (see the CGA arm above for why it is this way round).
    cmp al, 1
    jb .hclr
    cmp al, 8
    jb .hset
    test cl, 2                      ; terrain: a 50% dither, and on a DOUBLED
    jz .hclr                        ; picture the PAIR is the unit
.hset:
    mov al, 3
    dec cl
    shl al, cl
    or [es:di], al
    jmp .out
.hclr:
    mov al, 3
    dec cl
    shl al, cl
    not al
    and [es:di], al
.out:
    pop cx
    pop si
    ret

; -----------------------------------------------------------------------------
; void lem_r_rect(int x, int y, int w, int h, int colour, int fill)
;
; A rectangle in PANEL coordinates - 0..319 by 0..39 - which is where the two
; things that need one live: the minimap's frame and the VIEW RECTANGLE inside
; it. `fill` 0 draws the outline only, which is what a view rectangle is.
;
; `fill` 0 IS A REAL OUTLINE PATH AND IT DID NOT USED TO BE. The first version
; walked all w*h cells and tested each one - so the 20x20 view rectangle
; iterated 400 times to plot 76 pixels, ~24 ms of pure loop on a 4.77 MHz 8088,
; every time the rectangle moved. An interior row now jumps straight from column
; 0 to column w-1, which makes an outline 2*w + 2*(h-2) iterations: 4*(w+h)-4
; plots and no wasted test. (The jump cannot loop: the skip is only taken on a
; column strictly between 0 and w-1, so w-1 is always ahead of it.)
;
; IT BRACKETS ITS OWN VGA STATE (lb_batch_in), so a rectangle sets write mode 2
; and the map mask ONCE rather than per pixel, and it nests inside a caller's
; lem_r_batch() bracket without disturbing it.
;
; Locals: [bp-2] row, [bp-4] col.
; -----------------------------------------------------------------------------
_lem_r_rect:
    push bp
    mov bp, sp
    sub sp, 4
    push si
    push di
    push es
    push ds
    call lb_batch_in
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, [bp+10]                 ; h
    jge .out
    mov word [bp-4], 0
.col:
    mov ax, [bp-4]
    cmp ax, [bp+8]                  ; w
    jge .rowend
    cmp word [bp+14], 0             ; fill?
    jne .plot
    ; the outline: the first or last row is drawn whole
    mov ax, [bp-2]
    cmp ax, 0
    je .plot
    mov bx, [bp+10]
    dec bx
    cmp ax, bx
    je .plot
    ; ...and an interior row is the first and last COLUMN and nothing between
    mov ax, [bp-4]
    cmp ax, 0
    je .plot
    mov bx, [bp+8]
    dec bx
    cmp ax, bx
    je .plot
    mov [bp-4], bx                  ; SKIP the interior rather than walking it
    jmp .col
.plot:
    mov bx, [bp+4]
    add bx, [bp-4]
    mov cx, [bp+6]
    add cx, [bp-2]
    mov ax, [bp+12]                 ; colour
    call lb_rpix
.colend:
    inc word [bp-4]
    jmp .col
.rowend:
    inc word [bp-2]
    jmp .row
.out:
    call lb_batch_out
    pop ds
    pop es
    pop di
    pop si
    add sp, 4                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE BATCH (SPEC.md 116.4.4), and it is a MEASURED fix rather than a tidy-up.
;
; lb_rpix used to reprogram the VGA on EVERY PIXEL - GC 8 (bit mask), GC 5
; (write mode 2), Seq 2 (map mask), then GC 5 and GC 8 back again: five indexed
; pairs, TEN `out`s, per pixel, around one read and one write. Measured on a
; 4.77 MHz 8088 that is the whole of SPEC.md 116.7.1's 583 us a minimap cell, and
; everything on the panel side goes through it - the status line, the minimap
; and the view rectangle.
;
; The right technique was already in this file 900 lines up: lem_r_piece's own
; header says "write mode 2 would be a bit mask and a colour per PIXEL - two
; `out`s each" and uses four plane passes with NO port I/O in the inner loop -
; and then the panel side was built the way that header rejects.
;
; So the mode and the map mask are set ONCE PER RUN and only the Bit Mask moves
; per pixel: two `out`s instead of ten. lb_batch is a DEPTH and not a flag, so a
; caller that brackets a group of rectangles (lem_r_batch, from C) nests with
; _lem_r_rect's own bracket and a bare lb_rpix outside either still sets and
; restores the state itself. On a card that is not a VGA the whole thing is a
; compare and a return: lb_spix touches no port at all.
; -----------------------------------------------------------------------------
lb_batch_on:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .r
    push ax
    mov ah, 2                       ; write mode 2: the CPU byte is a COLOUR
    mov al, 5
    call lb_gc
    mov ah, 0x0F
    mov al, 2                       ; every plane, so the colour reaches all 4
    call lb_seq
    pop ax
.r:
    ret

lb_batch_off:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .r
    push ax
    mov ah, 0                       ; ...and back to the state the composer and
    mov al, 5                       ; the panel copy expect
    call lb_gc
    mov ah, 0xFF
    mov al, 8
    call lb_gc
    pop ax
.r:
    ret

lb_batch_in:
    inc word [lb_batch]
    cmp word [lb_batch], 1
    jne .r
    call lb_batch_on
.r:
    ret

lb_batch_out:
    cmp word [lb_batch], 0
    je .r
    dec word [lb_batch]
    jnz .r
    call lb_batch_off
.r:
    ret

; -----------------------------------------------------------------------------
; void lem_r_batch(int on) - the bracket, from C.
;
; ovl_view_rect() and ovl_mm_draw() each make dozens of one-pixel lem_r_rect
; calls; wrapping the group in one batch takes the VGA state out of every one of
; them. It is REFERENCE COUNTED, so a caller need not know whether it is already
; inside one.
; -----------------------------------------------------------------------------
_lem_r_batch:
    push bp
    mov bp, sp
    cmp word [bp+4], 0
    je .off
    call lb_batch_in
    jmp .out
.off:
    call lb_batch_out
.out:
    pop bp
    ret

; lb_rpix - internal: one PANEL pixel. VGA writes it through write mode 2, the
; two 1bpp backends through lb_spix's class map (the colour is already a nibble
; either way, so one caller serves both). Inside a batch the only port write
; here is the Bit Mask; outside one it sets and restores the whole state.
lb_rpix:
    cmp word [lb_kind], LEM_KIND_VGA
    jne lb_spix_panel
    push si
    push di
    push es
    push ax
    cmp bx, 0
    jl .out
    cmp bx, 320
    jge .out
    cmp cx, 0
    jl .out
    cmp cx, 40
    jge .out
    mov ax, cx
    call lem_rowoff                 ; DI = row * 200
    mov si, bx
    mov ax, si
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    and si, 7
    mov cx, 7
    sub cx, si                      ; an 8086 has no byte form of SI, so the
                                    ; subtraction is a word one and CL is what
                                    ; the shift below reads
    mov al, 1
    shl al, cl
    mov ah, al
    mov al, 8                       ; the Bit Mask: this pixel only, and the ONE
    call lb_gc                      ; port pair a batched pixel writes
    cmp word [lb_batch], 0
    jne .have                       ; the mode and the map mask are already set
    call lb_batch_on
.have:
    mov ax, [lb_fb]
    mov es, ax
    mov al, [es:di]                 ; load the latches
    pop ax
    push ax
    mov [es:di], al
    cmp word [lb_batch], 0
    jne .out
    call lb_batch_off
.out:
    pop ax
    pop es
    pop di
    pop si
    ret

; the panel arm, where lb_spix wants the row in SHADOW coordinates
lb_spix_panel:
    add cx, LEM_VIEWH
    jmp lb_spix

; the nibble-to-byte doubling table the Hercules composer uses: bit n of the
; nibble becomes two adjacent pixels, which is what puts a 320-pixel picture on
; a 640-pixel row.
lb_dbl:
    db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
    db 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF

section .bss
lb_kind:    resw 1
lb_fb:      resw 1
lb_world:   resw 1
lb_shadow:  resw 1
lb_scroll:  resw 1
lb_batch:   resw 1                  ; the VGA state bracket's DEPTH (lb_batch_in)
lb_wp0:     resw 1                  ; lem_r_wpiece's colour planes, or 0
lb_wp3:     resw 1                  ; ...and its plane 3, the solid mask
lb_wi0:     resw 1                  ; this piece's first byte index (lb_wshift)
lb_t:       resw 1                  ; the T claim (VGA): the world's four planes
                                    ; in RAM, kept for the level's life
lb_mrow:    resb 96                 ; one row's effective mask - the widest
                                    ; terrain piece in the five sets is 80
                                    ; pixels, so 11 bytes plus the shift's
                                    ; extra; 96 is the round number above it
lb_dlo:     resb LEM_SROWS          ; the dirty span per shadow row, 0xFF =
lb_dhi:     resb LEM_SROWS          ; clean (SPEC.md 85.3.1)
lb_mlatch:  resb 1                  ; lb_msample's buttons since lem_r_mlatch

section .text
