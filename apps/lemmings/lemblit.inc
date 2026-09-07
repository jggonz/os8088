; =============================================================================
; os8088 - apps/lemmings/lemblit.inc      THE THREE RASTERS (SPEC.md 92.4)
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
;                 40,000     the sprite save-under scratch
;
; The CRTC LINE COMPARE is set to scanline 320. Mode 0Dh is 200 rows double
; scanned, so scanline 320 is pixel row 160: above it the CRTC shows the world
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
; scroll that is already smooth at 17 fps smoother. SPEC.md 92.4.2 records it
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
LEM_VSAVE     equ 40000             ; the sprite save-under scratch
LEM_VSPLIT    equ 320               ; the line compare, in SCANLINES

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

    ; --- the AC palette registers, identity ------------------------------
    call lb_crtc
    add dx, 6
    in al, dx
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

; -----------------------------------------------------------------------------
; void lem_r_clearworld(void) - the world surface to colour 0.
;
; VGA: 32,000 bytes of all four planes at once, which the Sequencer's Map Mask
; makes one `rep stosw` rather than four. The two 1bpp backends have nothing to
; do: their world claim is written whole by lem_m_derive().
; -----------------------------------------------------------------------------
_lem_r_clearworld:
    push bp
    mov bp, sp
    push di
    push es
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
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
.out:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
;                  int flags)
;
; ONE TERRAIN PIECE'S PICTURE. VGA only - the 1bpp backends take their whole
; world from lem_m_derive() in one pass (see the file header).
;
; IT IS CALLED BEFORE lem_m_piece(), AND THE ORDER IS LOan-BEARING. The
; no-overwrite flag means "draw only where there is no terrain yet", and the
; cheapest place to ask that is the solid MASK, which at this instant still
; describes the world WITHOUT this piece. Asking VRAM instead would be a read
; through the Read Map Select register per byte.
;
; FOUR PLANE PASSES. Each sets the Read Map Select and the Map Mask to one
; plane and walks the row doing d = (d & ~m) | (s & m), so a masked planar
; write is two VRAM accesses a byte a plane and NO port I/O in the inner loop.
; Write mode 2 would be a bit mask and a colour per PIXEL - two `out`s each,
; and a level is 400 pieces of ~800 pixels.
;
; Locals: [bp-2] srow, [bp-4] bytes/row (source), [bp-6] shift, [bp-8] first
; dest byte column, [bp-10] dest row base, [bp-12] plane stride, [bp-14] dest
; bytes this row, [bp-16] plane.
; -----------------------------------------------------------------------------
_lem_r_piece:
    push bp
    mov bp, sp
    sub sp, 16
    push si
    push di
    push es
    push ds
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
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
    mov [bp-4], ax
    mul word [bp+14]
    mov [bp-12], ax                 ; plane stride = (w/8) * h

    mov ax, [bp+8]                  ; x
    mov cx, ax
    and cx, 7
    mov [bp-6], cx
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov [bp-8], ax

    mov ax, [bp-4]
    inc ax
    mov [bp-14], ax                 ; dest bytes this row = source bytes + 1

    mov word [bp-2], 0
.row:
    mov bx, [bp-2]
    cmp bx, [bp+14]
    jge .out

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
    cmp ax, LEM_VIEWH
    jae .nextrow
    call lem_rowoff                 ; DI = row * 200 (lemmask.inc)
    add di, LEM_VTERR
    mov [bp-10], di

    ; --- the effective mask for this row, into lb_mrow --------------------
    call lb_pmask
    ; --- and then the four planes ----------------------------------------
    mov word [bp-16], 0
.plane:
    call lb_pplane
    inc word [bp-16]
    cmp word [bp-16], 4
    jb .plane
.nextrow:
    inc word [bp-2]
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 16                        ; the `sub sp` at the top, undone EXPLICITLY
    pop bp
    ret

; lb_pmask - internal to _lem_r_piece: build lb_mrow[0..destbytes-1], the
; shifted plane-3 bytes, ANDed with the complement of the solid mask when the
; no-overwrite flag is set. Uses the caller's frame through BP.
lb_pmask:
    push si
    push di
    push es
    push ds
    ; the source: plane 3 = off + 3 * plane stride, row srow
    mov ax, [bp-4]
    mul word [bp-2]
    add ax, [bp+6]
    mov si, ax
    mov ax, [bp-12]
    add si, ax
    add si, ax
    add si, ax
    mov ax, [bp+4]
    mov ds, ax
    push cs
    pop es                          ; lb_mrow is ours, and a package is one
                                    ; flat segment
    mov di, lb_mrow
    xor ah, ah
    xor bx, bx
.b:
    mov al, 0
    cmp bx, [bp-4]
    jge .have
    mov al, [si+bx]
.have:
    push ax
    mov cx, [bp-6]
    jcxz .nos
    shr ax, cl
.nos:
    mov [es:di], al
    pop ax
    mov ah, al
    inc di
    inc bx
    cmp bx, [bp-14]
    jl .b
    push cs
    pop ds

    test word [bp+16], LEM_TF_NOOVER
    jz .done
    ; AND with the complement of the solid mask's own row
    mov ax, [bp-10]
    sub ax, LEM_VTERR               ; back to a mask offset: the two strides
    mov di, ax                      ; are both 200, which is why this is a
                                    ; subtraction and not a second walk
    mov ax, [lm_seg]
    mov es, ax
    mov si, lb_mrow
    mov cx, 0
    mov bx, [bp-8]
.nb:
    cmp bx, 0
    jl .nskip
    cmp bx, LEM_MSTRIDE
    jge .nskip
    push bx
    add bx, di
    mov al, [es:bx]
    not al
    and [cs:si], al
    pop bx
.nskip:
    inc si
    inc bx
    inc cx
    cmp cx, [bp-14]
    jl .nb
.done:
    pop ds
    pop es
    pop di
    pop si
    ret

; lb_pplane - internal to _lem_r_piece: one plane of one row.
lb_pplane:
    push si
    push di
    push es
    push ds
    ; select the plane for reading and for writing
    mov ax, [bp-16]
    mov cl, al
    mov ah, al
    mov al, 4                       ; Read Map Select
    call lb_gc
    mov ax, 1
    shl ax, cl
    mov ah, al
    mov al, 2                       ; Map Mask
    call lb_seq

    ; the source row for this plane
    mov ax, [bp-4]
    mul word [bp-2]
    add ax, [bp+6]
    mov si, ax
    mov ax, [bp-12]
    mov cx, [bp-16]
    jcxz .nop
.mulp:
    add si, ax
    loop .mulp
.nop:
    ; THE FRAMEBUFFER SEGMENT IS READ BEFORE DS MOVES, and that ordering is
    ; the whole of a defect this file shipped once: lb_fb is ordinary .bss and
    ; therefore DS-RELATIVE, so reading it with DS already pointed at the style
    ; bank reads two bytes of TERRAIN as a segment. What it looked like was a
    ; correctly set mode 0Dh, a correct split, a correct start address and a
    ; completely black screen (build/port-shots/wave2-vga-black.png).
    mov ax, [lb_fb]
    mov es, ax
    mov ax, [bp+4]
    mov ds, ax

    xor ah, ah
    xor bx, bx
.b:
    mov al, 0
    cmp bx, [bp-4]
    jge .have
    mov al, [si+bx]
.have:
    push ax
    mov cx, [bp-6]
    jcxz .nos
    shr ax, cl
.nos:
    ; AL is this byte's source contribution; the mask is lb_mrow[bx]
    mov dx, [bp-8]
    add dx, bx
    cmp dx, 0
    jl .skip
    cmp dx, LEM_MSTRIDE
    jge .skip
    add dx, [bp-10]
    mov di, dx
    mov dx, bx
    add dx, lb_mrow
    push bx
    mov bx, dx
    mov ah, [cs:bx]                 ; the effective mask byte
    pop bx
    test word [bp+16], LEM_TF_ERASE
    jz .draw
    not ah
    and [es:di], ah
    jmp .skip
.draw:
    and al, ah
    not ah
    mov cl, [es:di]
    and cl, ah
    or al, cl
    mov [es:di], al
.skip:
    pop ax
    mov ah, al
    inc bx
    cmp bx, [bp-14]
    jl .b
    pop ds
    pop es
    pop di
    pop si
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
; no palette to put it in (SPEC.md 39.4, 92.4.2): nibble 0 is black, 1..7 is
; the SPRITE/panel/text class - the panel, its buttons and their captions - and
; 8..15 the TERRAIN class, which is the direction lb_spix, SPEC.md 92.4 and
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
; THE BATCH (SPEC.md 92.4.4), and it is a MEASURED fix rather than a tidy-up.
;
; lb_rpix used to reprogram the VGA on EVERY PIXEL - GC 8 (bit mask), GC 5
; (write mode 2), Seq 2 (map mask), then GC 5 and GC 8 back again: five indexed
; pairs, TEN `out`s, per pixel, around one read and one write. Measured on a
; 4.77 MHz 8088 that is the whole of SPEC.md 92.7.1's 583 us a minimap cell, and
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
; lem_view_rect() and lem_mm_draw() each make dozens of one-pixel lem_r_rect
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
lb_mrow:    resb 96                 ; one row's effective mask - the widest
                                    ; terrain piece in the five sets is 80
                                    ; pixels, so 11 bytes plus the shift's
                                    ; extra; 96 is the round number above it
lb_dlo:     resb LEM_SROWS          ; the dirty span per shadow row, 0xFF =
lb_dhi:     resb LEM_SROWS          ; clean (SPEC.md 85.3.1)

section .text
