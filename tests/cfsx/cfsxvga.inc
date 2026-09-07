; =============================================================================
; os8088 - tests/cfsx/cfsxvga.inc      the hand-written half of the FSX gate
;
; %included by tests/cfsx/cfsx.asm, after the compiled C, exactly as
; apps/cword/cwmove.inc is included after cword.gen.asm. It defines C-callable
; cdecl entry points and switches back to .text at the end, as the section
; discipline in CLAUDE.md requires.
;
; WHY ANY OF THIS IS ASSEMBLY. Inside SPEC.md 53's bracket every kernel
; drawing slot is refused, so the only two ways to put a pixel on the glass
; are a write into the framebuffer segment the FSI block names and a word with
; the VGA's own registers. C can do neither: it has no `out dx, al`, and a
; per-pixel loop in C costs 3-5x what the same loop costs here (SPEC.md 73.11).
; The C above calls these once per bar and once per frame.
;
; THE CALLING CONVENTION is SmallerC's cdecl, the one cwmove.inc documents:
; arguments pushed right to left so the first is at [bp+4], the CALLER cleans
; up, AX is the return value, and AX/BX/CX/DX are the caller's to lose. SI,
; DI, BP, DS, SS and ES are preserved by everything below - ES because the
; kernel's callbacks are entered with KERNEL_SEG in it and expect it back
; (SPEC.md 20.4), and DF is left clear because every ISR in the system assumes
; it (CLAUDE.md's register discipline).
;
; THE CRTC PORT IS READ FROM THE BIOS DATA AREA, never assumed to be 0x3D4.
; Every mode this gate sets is a colour mode so it always is 0x3D4 in
; practice, but the six routines that need it all go through cf_crtcport and
; the assumption is then made in exactly one place instead of nine.
; =============================================================================

section .text

VGA_GC_IDX  equ 0x3CE               ; Graphics Controller index / data
VGA_AC_IDX  equ 0x3C0               ; Attribute Controller: index AND data...
VGA_AC_RD   equ 0x3C1               ; ...on one port, read back on another
BDA_SEG     equ 0x40
BDA_CRTC    equ 0x63                ; word: the CRTC index port for this mode
BDA_COLS    equ 0x4A                ; word: screen columns - what the BIOS
                                    ; wraps a TTY line at, which is why the
                                    ; split-screen stage has to move it
BDA_TICK    equ 0x6C                ; dword: the 18.2 Hz tick

; -----------------------------------------------------------------------------
; cf_crtcport - internal. out: DX = the CRTC index port. Clobbers DX only.
; -----------------------------------------------------------------------------
cf_crtcport:
    push ax
    push es
    mov ax, BDA_SEG
    mov es, ax
    mov dx, [es:BDA_CRTC]
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; void cf_gcmode(int mode) - put the Graphics Controller into a known state.
;
; mode 2 is the write mode a solid planar fill wants: the byte the CPU writes
; is a COLOUR and the bit mask says which of the eight pixels in that byte take
; it. With the mask at 0xFF all eight do, which is why no latch read is needed
; anywhere below - nothing survives from memory.
;
; mode 0 with the mask still 0xFF is the state the BIOS's own character writer
; expects, so this is called with 0 before any int 10h text and before leaving
; a stage. Data rotate (index 3) and enable-set/reset (index 1) are zeroed
; here as well: this program never uses them, but the BIOS mode set is the
; only other thing that touches them and a stage that ran before this one
; could have left either armed.
; -----------------------------------------------------------------------------
_cf_gcmode:
    push bp
    mov bp, sp
    mov dx, VGA_GC_IDX
    mov al, 5                       ; Mode: bits 0-1 = write mode
    out dx, al
    inc dx
    mov ax, [bp+4]
    out dx, al
    dec dx
    mov al, 8                       ; Bit Mask: every pixel of the byte
    out dx, al
    inc dx
    mov al, 0xFF
    out dx, al
    dec dx
    mov al, 3                       ; Data Rotate / function select: none
    out dx, al
    inc dx
    xor al, al
    out dx, al
    dec dx
    mov al, 1                       ; Enable Set/Reset: off on all four planes
    out dx, al
    inc dx
    xor al, al
    out dx, al
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_fill(unsigned seg, unsigned off, unsigned n, int colour)
;
; n bytes of the framebuffer set to one colour. Write mode 2 must already be
; on (cf_gcmode(2)); this is the raw byte loop and knows nothing about the VGA.
; -----------------------------------------------------------------------------
_cf_fill:
    push bp
    mov bp, sp
    push di
    push es
    mov es, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov ax, [bp+10]
    jcxz .done
.l:
    mov [es:di], al
    inc di
    loop .l
.done:
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_ramp(unsigned seg, unsigned stride, unsigned y, unsigned h)
;
; SIXTEEN BARS, colour 0 to colour 15, left to right, h rows tall from row y.
; Each bar is stride/16 bytes wide, so it lands on a byte boundary and the
; whole ramp is exactly the width the mode's row is: 2 bytes (16 pixels) a bar
; in mode 0Dh, 5 bytes (40 pixels) in mode 12h.
;
; It is sixteen bars rather than a gradient because the failure it is looking
; for is a PLANE failure: a write that reaches three planes instead of four
; loses colour 8 and up, and a mask left at something other than 0xFF doubles
; a colour or drops one. Both read as "a bar is missing or repeated" and
; neither reads as anything at all in a gradient.
; -----------------------------------------------------------------------------
_cf_ramp:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+4]
    mov si, [bp+8]                  ; SI = the row being drawn
    mov di, [bp+8]
    add di, [bp+10]                 ; DI = one past the last row
.row:
    cmp si, di
    jae .done
    mov ax, si
    mul word [bp+6]                 ; DX:AX = row * stride; the framebuffer is
    push di                         ; under 64KB in every mode here, so DX is 0
    mov di, ax                      ; DI = the row's first byte
    xor bx, bx                      ; BX = the bar's colour, 0..15
.bar:
    mov cx, [bp+6]                  ; CX = stride / 16, the bar's width in
    shr cx, 1                       ; bytes. Four shifts of one: an 8086 has
    shr cx, 1                       ; no `shr reg, imm` other than 1, and a
    shr cx, 1                       ; CL of 4 would cost the same two bytes
    shr cx, 1                       ; to load
    jcxz .nextbar
    mov al, bl
.px:
    mov [es:di], al
    inc di
    loop .px
.nextbar:
    inc bx
    cmp bx, 16
    jb .bar
    pop di
    inc si
    jmp short .row
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_diag(unsigned seg, unsigned stride, unsigned row0, unsigned rows)
;
; The SCROLLING content of the split-screen stage: diagonal colour bands, with
; a white eight-pixel post every 64 pixels.
;
;   colour(byte b of row r) = ((b + (r >> 3)) >> 1) & 15,  or 15 when b & 7 = 0
;
; The bands slope, so a scroll that moves is unmistakable in a still
; screendump taken twice; the posts are what makes it COUNTABLE, because they
; are one byte wide on a 64-pixel pitch and a pel-panned frame shows one of
; them part-way across a byte.
; -----------------------------------------------------------------------------
_cf_diag:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+4]
    mov si, [bp+8]
    mov di, [bp+8]
    add di, [bp+10]
.row:
    cmp si, di
    jae .done
    mov ax, si
    mul word [bp+6]
    push di
    mov di, ax                      ; DI = the row's first byte
    mov ax, si
    mov cl, 3
    shr ax, cl                      ; AX = r >> 3, the band's slope term
    mov dx, ax
    xor bx, bx                      ; BX = the byte column
.px:
    mov ax, bx
    test al, 7
    jz .post                        ; every 64 pixels: a white post
    add ax, dx
    shr ax, 1
    and al, 15
    jmp short .put
.post:
    mov al, 15
.put:
    mov [es:di], al
    inc di
    inc bx
    cmp bx, [bp+6]
    jb .px
    pop di
    inc si
    jmp short .row
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_panel(unsigned seg, unsigned stride, unsigned rows)
;
; The STANDING STILL content, written at framebuffer row 0 because that is
; where the split screen displays the panel from: the CRTC's address counter
; is reset to 0 at the line compare, whatever the start address is, so the
; bottom of the screen shows the TOP of memory. Getting that backwards is the
; whole reason this is a separate routine with its own comment.
;
; Horizontal bands eight rows deep, so it cannot be mistaken for cf_diag's
; sloping ones in a screendump.
; -----------------------------------------------------------------------------
_cf_panel:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [bp+4]
    xor si, si
.row:
    cmp si, [bp+8]
    jae .done
    mov ax, si
    mul word [bp+6]
    mov di, ax
    mov ax, si
    mov cl, 3
    shr ax, cl
    add al, 9                       ; 9..13: nothing cf_diag's ramp opens with
    and al, 15
    mov cx, [bp+6]
.px:
    mov [es:di], al
    inc di
    loop .px
    inc si
    jmp short .row
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_tty(int row, int col, const char *s, int colour)
;
; TEXT IN A FOREIGN MODE, through the BIOS. int 10h AH=02h sets the cursor and
; AH=0Eh writes a character, and both work in modes 0Dh and 12h with the ROM's
; own 8x8 or 8x16 font - so this gate carries no font of its own. It is the
; BIOS and not the kernel, so it is legal inside the bracket where every
; os88_font_* slot is refused.
;
; The string is DS-relative like every other object a C program can name.
; Printable characters only: AH=0Eh gives CR, LF, BS and BEL their TTY
; meanings and this makes no attempt to.
; -----------------------------------------------------------------------------
_cf_tty:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cld
    mov ah, 0x02                    ; set cursor position
    xor bh, bh
    mov dh, [bp+4]
    mov dl, [bp+6]
    int 0x10
    mov si, [bp+8]
.ch:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E                    ; teletype output
    mov bl, [bp+10]
    xor bh, bh
    int 0x10
    jmp short .ch
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cf_crtcget(int index) - READ A CRTC REGISTER BACK.
;
; It is here so the gate can print what the line compare actually holds rather
; than what it was told to hold. Three registers in three pieces is three
; chances to write the wrong bit into the wrong one, and every one of those
; mistakes produces a picture that is merely WRONG - no error, no refusal.
; -----------------------------------------------------------------------------
_cf_crtcget:
    push bp
    mov bp, sp
    call cf_crtcport
    mov ax, [bp+4]
    out dx, al
    inc dx
    in al, dx
    xor ah, ah
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cf_acget(int index) - read an Attribute Controller register back.
;
; The Attribute Controller is the awkward one: index and data share port 3C0
; and which of the two the next write means is a FLIP-FLOP, cleared by reading
; the input status register at CRTC+6. Every access below resets it first, so
; no routine here depends on the state another left behind.
;
; Bit 5 of the index keeps the palette RAM addressed by the CRTC rather than
; by the CPU - clear it and the screen goes black until somebody sets it
; again. It is ORed in unconditionally.
; -----------------------------------------------------------------------------
_cf_acget:
    push bp
    mov bp, sp
    call cf_crtcport
    add dx, 6
    in al, dx
    mov dx, VGA_AC_IDX
    mov ax, [bp+4]
    or al, 0x20
    out dx, al
    mov dx, VGA_AC_RD
    in al, dx
    xor ah, ah
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_split(unsigned scanline) - THE CRTC LINE COMPARE (SPEC.md 92.4)
;
; Below `scanline` the CRTC restarts its address counter at 0 and, with the
; Attribute Controller's Pixel Panning Mode bit set, stops applying the pel
; pan. That is the whole of a split screen: the top scrolls and the panel
; underneath stands still, out of the same framebuffer, with no per-frame work
; at all.
;
; THE NUMBER IS A SCANLINE AND NOT A PIXEL ROW. Mode 0Dh is 200 rows double
; scanned to 400, so the 320 this gate asks for is pixel row 160 and leaves 40
; pixel rows below - which is why the value needs bit 8 at all, and bit 8 is
; the reason the register is in three pieces:
;
;   0x18 Line Compare      bits 0-7
;   0x07 Overflow    bit 4 bit 8
;   0x09 Max Scan Line bit 6 bit 9
;
; Two of those registers hold something else in every one of their other bits,
; so both are read back and merged rather than written whole. Writing 0x09
; whole is the classic way to lose the double scan and get a 400-line picture
; out of a 200-line mode.
; -----------------------------------------------------------------------------
_cf_split:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call cf_crtcport
    mov al, 0x18                    ; Line Compare: bits 0-7
    out dx, al
    inc dx
    mov ax, si
    out dx, al
    dec dx
    mov al, 0x07                    ; Overflow bit 4 = line compare bit 8
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
    mov al, 0x09                    ; Max Scan Line bit 6 = line compare bit 9
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

; -----------------------------------------------------------------------------
; void cf_panmode(int on) - Attribute Controller index 0x10 bit 5.
;
; Set, the panel below the line compare is NOT pel panned. Clear, it pans with
; the top half - which is the defect this bit exists to prevent and is worth
; being able to turn back on, because "the panel jitters by up to seven pixels
; while the top scrolls smoothly" is what it looks like and is easy to blame
; on the scroll.
; -----------------------------------------------------------------------------
_cf_panmode:
    push bp
    mov bp, sp
    call cf_crtcport
    add dx, 6
    in al, dx
    mov dx, VGA_AC_IDX
    mov al, 0x10 | 0x20
    out dx, al
    mov dx, VGA_AC_RD
    in al, dx                       ; the flip-flop is toggled by WRITES to
    mov dx, VGA_AC_IDX              ; 3C0 and by nothing else, so after the
    and al, 0xDF                    ; index went out it is still expecting the
                                    ; DATA and this write is that data - no
                                    ; second index, which would land as a
                                    ; value and turn the screen off
    cmp word [bp+4], 0
    je .put
    or al, 0x20
.put:
    out dx, al
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_nosplit(void) - the line compare back past the bottom of any mode
; (0x3FF), and the pixel panning mode bit cleared. What the BIOS leaves.
; -----------------------------------------------------------------------------
_cf_nosplit:
    push bp
    mov bp, sp
    mov ax, 0x03FF                  ; an 8086 has no `push imm16` (CLAUDE.md's
    push ax                         ; hard rules), so every cdecl argument
    call _cf_split                  ; below goes through a register
    add sp, 2
    xor ax, ax
    push ax
    call _cf_panmode
    add sp, 2
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_offset(int words) - CRTC 0x13, the LOGICAL line width.
;
; In byte mode - which every graphics mode the BIOS sets is - the value is the
; row pitch in units of two bytes, so the 0x14 mode 0Dh boots with is 40 bytes
; and the 0x28 this gate sets is 80: a 640-pixel-wide canvas displayed 320
; pixels at a time, which is what makes a horizontal scroll a scroll rather
; than a shear. Without it, advancing the start address by one byte pulls the
; NEXT ROW's first pixels in at the right-hand edge.
; -----------------------------------------------------------------------------
_cf_offset:
    push bp
    mov bp, sp
    call cf_crtcport
    mov al, 0x13
    out dx, al
    inc dx
    mov ax, [bp+4]
    out dx, al
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_scroll(unsigned addr, int pan) - one frame's worth of scrolling.
;
; The start address (0x0C high, 0x0D low) moves the picture eight pixels at a
; time and the pel pan (Attribute Controller 0x13) moves it the other seven,
; so the pair of them is a one-pixel step. That is the mechanism SPEC.md 92.4
; is built on, and it costs four `out`s a frame whatever the picture is.
; -----------------------------------------------------------------------------
_cf_scroll:
    push bp
    mov bp, sp
    call cf_crtcport
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
    add dx, 5                       ; CRTC data + 5 = the input status
    in al, dx                       ; register: reset the AC flip-flop
    mov dx, VGA_AC_IDX
    mov al, 0x13 | 0x20             ; Horizontal Pixel Panning
    out dx, al
    mov ax, [bp+6]
    and al, 7
    out dx, al
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cf_bdacols(int cols) - swap the BIOS's columns word, answering the old.
;
; It is the COLUMN COUNT and not the pitch. The pitch the VGA BIOS's graphics
; character writer uses is the CRTC's own Offset register, read back at the
; call - which is worth knowing because it is the opposite of what the BDA
; name suggests, and because it means the Offset register has to be moved
; BEFORE the text is drawn and not after (tests/cfsx/cfsx.c says what that
; looked like). What this word buys is the WRAP: a caption written at column
; 40 folds onto the next line while the BIOS still thinks the screen is 40
; wide. Put back before the stage ends.
; -----------------------------------------------------------------------------
_cf_bdacols:
    push bp
    mov bp, sp
    push es
    mov ax, BDA_SEG
    mov es, ax
    mov ax, [es:BDA_COLS]
    push ax
    mov ax, [bp+4]
    mov [es:BDA_COLS], ax
    pop ax
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cf_vsync(void) - wait for the start of the vertical retrace, BOUNDED.
;
; SPEC.md 37.90: the way to hang a machine forever is to wait for a status bit
; that reads 0FFh on the adapter you did not think about. This gives up after
; three BIOS ticks whatever the bit says, so a card whose status register
; never moves costs a sixth of a second per frame and does not cost the
; machine.
;
; The elapsed count is a SUBTRACT rather than a compare against a target, so
; the midnight rollover - which sets the tick to 0 rather than stepping it -
; ends the wait instead of trapping it for a day.
; -----------------------------------------------------------------------------
_cf_vsync:
    push si
    push es
    mov ax, BDA_SEG
    mov es, ax
    mov si, [es:BDA_TICK]
    call cf_crtcport
    add dx, 6
.notin:
    in al, dx                       ; first leave any retrace already running,
    test al, 8                      ; so what is waited for below is an EDGE
    jz .wait
    mov ax, [es:BDA_TICK]
    sub ax, si
    cmp ax, 3
    jb .notin
    jmp short .done
.wait:
    in al, dx
    test al, 8
    jnz .done
    mov ax, [es:BDA_TICK]
    sub ax, si
    cmp ax, 3
    jb .wait
.done:
    pop es
    pop si
    ret
