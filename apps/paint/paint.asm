; =============================================================================
; os8088 - apps/paint/paint.asm
;
; PAINT, the seventh shipped package: a bitmap editor with eight tools, a
; 4bpp offscreen canvas, one-level undo/redo, an internal clipboard and
; BMP load/save through the Standard File dialog (SPEC.md 38). It runs on
; all three adapters (SPEC.md 39) and needs no kernel change of any kind -
; every pixel it puts on screen goes through the published gfx_* slots.
;
; THE CANVAS LIVES OUTSIDE THE PACKAGE REGION. A 448x280 image at 4bpp is
; 62,720 bytes against a 19.5KB region (SPEC.md 20.1), so the pixels cannot
; be here. They sit in four 64KB windows starting at linear 0x66000 - the
; first paragraph above BB_SEG's four planes (SPEC.md 2: BB_SEG's 4 x 0x9600
; bytes end at 0x657FF), which is the lowest address in the machine that no
; kernel structure can ever reach:
;
;   0x66000  PT_CVSEG   canvas      118-byte BMP header + rows, bottom-up
;   0x76000  PT_UNSEG   undo image  same layout, row-granular (see pt_umark)
;   0x86000  PT_CBSEG   clipboard   also the load staging buffer (64KB)
;   0x96000  PT_SCSEG   scratch     the claim record + the flood-fill stack
;
; That is memory nobody granted us, and it is the ONE thing here that a
; kernel service should own instead (see the notes in docs/PAINT-NOTES.md).
; Three consequences are handled rather than hoped about:
;
;  1. The region only exists on a machine with ~620KB of conventional RAM,
;     so pt_entry asks int 12h first and puts up "Not enough memory" instead
;     of a canvas when the answer is short. A 256KB or 512KB machine gets a
;     window that explains itself and touches nothing.
;  2. Two instances would share one canvas, so the SECOND one refuses. The
;     claim record at PT_SCSEG:0 carries a magic pair and the owner's window
;     pointer; pt_dupchk trusts it only if that record is still a used
;     window whose title string is ours (SPEC.md 11's W_FLAGS bit 0 and
;     W_TITLE, read through DS = KERNEL_SEG). A closed Paint leaves a stale
;     magic behind - there is no close hook for a task-less package - and
;     that test is what makes the staleness harmless.
;  3. BB_SEG is never touched, so the Control Panel's Display page can arm
;     or disarm double buffering under us with no effect but the flush.
;
; PERFORMANCE. Two decisions carry the whole app:
;
;  * The canvas is 4bpp packed, high nibble = the left pixel, rows stored
;    BOTTOM-UP behind a 118-byte BMP header - i.e. the canvas IS a
;    BITMAPINFOHEADER DIB. Saving is therefore one OSAPI_FILE_WRITE of the
;    canvas segment with no staging pass and no copy, which also keeps a
;    full-size picture inside the file API's 64KB ceiling (SPEC.md 18.4).
;    Bottom-up costs nothing: pt_rowtab maps canvas y to a byte offset once
;    at startup and every access is a table lookup.
;  * Nothing ever repaints more of the screen than it changed. A brush
;    stroke emits only the pixels the dab uncovered (pt_seg walks the line
;    and draws the leading edge - one gfx_fill per step, never a redab), a
;    flood fill emits its own spans as it finds them, and a rectangle is one
;    gfx_fill. The run-coalescing blit (pt_blit) exists for the paths that
;    genuinely need the whole canvas - W_PAINT, undo, paste, load - and uses
;    `repe scasb` over the packed bytes, so a flat region costs ~7 cycles a
;    pixel and one gfx_hline a run.
;
; Window callbacks run with the gfx lock HELD (SPEC.md 11). The three
; tracking loops here - brush, rubber band, marquee - therefore follow
; ui_drag's discipline in reverse (SPEC.md 13): they draw, RELEASE the lock
; so gfx_unlock's flush pushes the work to VRAM (a package's gfx_* calls go
; through the back buffer when double buffering is on, so without the
; release a stroke would be invisible until the button came up), yield, then
; re-take it. They exit with the lock held, as the contract requires.
;
; BP is used as a plain value register only, never dereferenced: SS != DS
; for packages (SPEC.md 20.5).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PAINT', pt_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A painter's palette: the thumb notch bottom-left, four dabs of paint. The
; mask is the silhouette dilated and span-filled, so the glyph sits on a
; solid white underlay the way the built-in icons do.
;
;   data                          mask
;   ....########....              .##############.
;   ..##........##..              ################
;   .#....##......#.              ################
;   #.....##.......#              ################
;   #..............#              ################
;   #...##....##...#              ################
;   #...##....##...#              ################
;   #..............#              ################
;   #.....##.......#              ################
;   .#....##......#.              ################
;   .#...........##.              ################
;   ..#........##...              ################
;   ..#.....###.....              .#############..
;   ..#...##........              .###########....
;   ..#####.........              .########.......
;   ................              .#######........
    OS88_ICON16
    dw 0x7FFE                       ; 16 mask rows (white underlay)
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x7FFC
    dw 0x7FF0
    dw 0x7F80
    dw 0x7F00
    dw 0x0FF0                       ; 16 data rows (black pixels)
    dw 0x300C
    dw 0x4302
    dw 0x8301
    dw 0x8001
    dw 0x8C31
    dw 0x8C31
    dw 0x8001
    dw 0x8301
    dw 0x4302
    dw 0x4006
    dw 0x2018
    dw 0x20E0
    dw 0x2300
    dw 0x3E00
    dw 0x0000
    OS88_ICON16_END

; --- the four segments we claim above BB_SEG (see the header) -------------------
PT_CVSEG    equ 0x6600              ; canvas: BMP header + bottom-up rows
PT_UNSEG    equ 0x7600              ; undo image, identical layout
PT_CBSEG    equ 0x8600              ; clipboard, and the load staging buffer
PT_SCSEG    equ 0x9600              ; claim record + flood-fill stack
PT_NEED_KB  equ 620                 ; int 12h floor: PT_SCSEG + 19.5KB of use
                                    ; ends at linear 0x9AC00 = 619.0KB, so a
                                    ; 640KB machine (int 12h says 639 or 640
                                    ; once the BIOS takes its EBDA) clears it
                                    ; and nothing smaller does

PT_MAGIC1   equ 0x3850              ; 'P8' - the claim record's signature...
PT_MAGIC2   equ 0x5A17              ; ...in two words, so stale RAM cannot
                                    ; plausibly pass for a live owner
PT_SC_CLAIM equ 0                   ; claim record: magic1, magic2, win ptr
PT_SC_STACK equ 16                  ; flood-fill span stack starts here
PT_FSTK_MAX equ 1024                ; entries of 8 bytes (y, x1, x2, dy)

; --- canvas geometry -----------------------------------------------------------
; The width is fixed on every adapter: the narrowest screen os8088 drives is
; 640 (SPEC.md 39), and 448 + the palette + the frame fits there. Only the
; HEIGHT varies, because the desktop between the menu bar and the dock does
; (436 / 300 / 152 rows on VGA / Hercules / CGA), and pt_entry derives it
; from the live geometry rather than from a constant.
PT_CW       equ 448                 ; canvas width, pixels
PT_STRIDE   equ PT_CW / 2           ; 224 bytes per row - a multiple of 4, so
                                    ; it is already a legal 4bpp BMP stride
PT_CH_MAX   equ 280                 ; canvas height cap (fits VGA's desktop)
PT_CH_MIN   equ 64                  ; below this we refuse rather than crop
PT_BMPHDR   equ 118                 ; 14 + 40 + 16*4: the DIB in front of row 0

; --- content layout (all content-relative; SPEC.md 11's content rect) ----------
PT_BW       equ 20                  ; tool / swatch button side, pixels
PT_PAL_X0   equ 1                   ; tool palette, left column
PT_PAL_X1   equ 22                  ; ...and right column
PT_PAL_Y0   equ 1                   ; first button row
PT_PAL_DY   equ 21                  ; button pitch
PT_SEPX     equ 43                  ; the black vline between palette and canvas
PT_CV_X     equ 44                  ; canvas left edge
PT_CONT_W   equ PT_CV_X + PT_CW     ; 492 - content width
PT_FRAME_W  equ PT_CONT_W + 2       ; 494 - frame width (1px border each side)
PT_STRIP_H  equ 22                  ; bottom strip: colours, widths, toggles
PT_CHROME_H equ PT_STRIP_H + 1 + 19 ; canvas height -> frame height (the +1 is
                                    ; the separator row, the 19 is TITLE_H+1)
PT_WIN_Y    equ MBAR_H + 4          ; frame top

; strip contents, content-relative x (the strip's own top is [pt_stripy])
PT_TH_X     equ 2                   ; four thickness buttons, pitch 21
PT_TH_DX    equ 21
PT_CUR_X    equ 90                  ; current-colour well
PT_SW_X     equ 116                 ; colour swatches, pitch 21
PT_SW_DX    equ 21
PT_FIL_X    equ 455                 ; filled-shapes toggle (16 wide)
PT_FNT_X    equ 473                 ; font-scale cycle (16 wide)
PT_BTN_W16  equ 16

; --- tools ---------------------------------------------------------------------
PT_T_PENCIL equ 0
PT_T_ERASER equ 1
PT_T_DROP   equ 2
PT_T_RECT   equ 3
PT_T_OVAL   equ 4
PT_T_SEL    equ 5
PT_T_FILL   equ 6
PT_T_TEXT   equ 7
PT_NTOOL    equ 8

; --- modes: anything but PT_M_LIVE draws a notice and eats every input ---------
PT_M_LIVE   equ 0
PT_M_NOMEM  equ 1                   ; int 12h below PT_NEED_KB
PT_M_DUP    equ 2                   ; another Paint owns the canvas
PT_M_SMALL  equ 3                   ; the desktop cannot hold a usable canvas

PT_NAMEMAX  equ 12                  ; 8 + '.' + 3, as SPEC.md 38.6 hands it over

; -----------------------------------------------------------------------------
; pt_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
;
; Order is load-bearing. The geometry probe decides the window size, so it
; runs before wm_create - a template sized here never meets wm_fit's clamp
; (SPEC.md 39.7), and [pt_ch] is still re-derived from the record afterwards
; because the record, not the template, is the truth. The RAM check runs
; before the duplicate check, because the duplicate check READS the claim
; record and there is no claim record on a machine that has no such memory.
; The claim itself waits until wm_create has given us the window pointer it
; has to publish.
;
; The loader shows the window after we return, so nothing here draws.
; -----------------------------------------------------------------------------
pt_entry:
    mov byte [pt_fscale], 1         ; the bss arrives zeroed, and zero is not a
    mov byte [pt_ethick], 1         ; text scale; the eraser starts at 16px
                                    ; against the pencil's 1px, which is what
                                    ; "much thicker by default" means here
    call pt_geom                    ; window size + [pt_ch] + [pt_mono]
    call pt_memchk                  ; int 12h -> PT_M_NOMEM or nothing
    cmp byte [pt_mode], PT_M_LIVE
    jne .make                       ; already refusing: do not read the claim
    call pt_dupchk                  ; CF=1: another live Paint has the canvas
    jnc .make
    mov byte [pt_mode], PT_M_DUP
.make:
    push si
    mov si, pt_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; no window: nothing to flag, nothing to
                                    ; claim - the region stays untouched
    mov [pt_win], bx
    cmp byte [pt_mode], PT_M_LIVE
    jne .menus                      ; a notice window gets the menu bar too,
                                    ; so its name shows and File/Edit are
                                    ; visibly inert rather than absent
    pushf                           ; the CF wm_create owes the loader rides
    call pt_claim                   ; through every one of these
    call pt_canvas_init
    call pt_font_init
    popf
.menus:
    push si
    mov si, pt_menus                ; BX is still the window (SPEC.md 20.3:
    call OSAPI_MENU_SET             ; menu_win_set preserves flags too)
    pop si
.out:
    ret

; -----------------------------------------------------------------------------
; pt_geom - size the window from the live screen (SPEC.md 39.2/39.8)
; in:  nothing
; out: pt_tpl patched, [pt_ch] set, [pt_mono] / [pt_ncol] set,
;      [pt_mode] = PT_M_SMALL if no usable canvas fits; preserves nothing
;
; The canvas height is whatever is left of the desktop after the title bar,
; the separator and the strip. CX from OSAPI_VIDEO is the first row the dock
; owns, so rows PT_WIN_Y..CX-1 are ours. On a 1bpp adapter the palette drops
; to the three colours that survive SPEC.md 39.4's reduction as distinct
; classes - black, the 50% dither, white - because sixteen swatches of which
; fourteen are indistinguishable is a worse lie than three that are honest.
; -----------------------------------------------------------------------------
pt_geom:
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock row, DL=kind, DH=bpp
    mov byte [pt_ncol], 16
    cmp dh, 1
    jne .colour
    mov byte [pt_mono], 1
    mov byte [pt_ncol], 3
.colour:
    ; --- height: CX - PT_WIN_Y is the tallest frame that clears the dock ---
    ; Signed compares throughout: a screen short enough to make this negative
    ; would otherwise read as an enormous unsigned canvas.
    sub cx, PT_WIN_Y + PT_CHROME_H  ; ...and this is the canvas inside it
    cmp cx, PT_CH_MAX
    jle .h_ok
    mov cx, PT_CH_MAX
.h_ok:
    cmp cx, PT_CH_MIN
    jge .h_fits
    mov byte [pt_mode], PT_M_SMALL
    mov cx, PT_CH_MIN               ; still make a window, just an inert one
.h_fits:
    mov [pt_ch], cx
    add cx, PT_CHROME_H
    mov [pt_tpl + WT_H], cx
    mov word [pt_tpl + WT_Y], PT_WIN_Y
    ; --- width is fixed; centre it, and refuse a screen too narrow for it ---
    mov word [pt_tpl + WT_W], PT_FRAME_W
    sub ax, PT_FRAME_W
    jns .w_ok
    mov byte [pt_mode], PT_M_SMALL
    xor ax, ax
.w_ok:
    shr ax, 1
    mov [pt_tpl + WT_X], ax
    ret

; -----------------------------------------------------------------------------
; pt_memchk - int 12h against PT_NEED_KB
; in:  nothing
; out: [pt_mode] = PT_M_NOMEM when short; preserves all registers
;
; int 12h is the same probe bb_init uses for double buffering (SPEC.md 32) and
; the only one available to a package: there is no memory service in the API.
; -----------------------------------------------------------------------------
pt_memchk:
    push ax
    int 0x12                        ; AX = KB of conventional memory
    cmp ax, PT_NEED_KB
    jae .out
    mov byte [pt_mode], PT_M_NOMEM
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_dupchk - is another live Paint holding the canvas? (see the file header)
; in:  nothing (reads the claim record at PT_SCSEG:PT_SC_CLAIM)
; out: CF=1 yes; preserves all registers
;
; The record is only believed when all four tests pass: both magic words, a
; window pointer inside the kernel's own data range, W_FLAGS bit 0 (the slot
; is still in use, SPEC.md 11) and a W_TITLE string equal to ours. A Paint
; that has been closed fails the third or fourth test - its slot is free, or
; some other app has since taken it - which is exactly why a stale claim
; cannot lock the app out of the machine for the rest of the session.
; The pointer range test comes first: without it a garbage word would be
; dereferenced, and although a read of a random kernel word is harmless, a
; read of one below the API table is not obviously so.
; -----------------------------------------------------------------------------
pt_dupchk:
    push ax
    push bx
    push si
    push di
    push es
    mov ax, PT_SCSEG
    mov es, ax
    cmp word [es:PT_SC_CLAIM], PT_MAGIC1
    jne .free
    cmp word [es:PT_SC_CLAIM+2], PT_MAGIC2
    jne .free
    mov bx, [es:PT_SC_CLAIM+4]      ; the claimed window record
    cmp bx, 0x0100                  ; below the API table: not a record
    jb .free
    cmp bx, APP_LOAD_OFF            ; inside the package pool: not a record
    jae .free
    test word [bx + W_FLAGS], 1     ; still a used window slot?
    jz .free
    mov si, [bx + W_TITLE]          ; ...and still called what we call ours?
    mov di, pt_title
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .free
    or al, al
    jz .taken
    inc si
    inc di
    jmp short .cmp
.taken:
    stc
    jmp short .out
.free:
    clc
.out:
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_claim - publish the claim record (magic pair + our window pointer)
; in:  [pt_win]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_claim:
    push ax
    push es
    mov ax, PT_SCSEG
    mov es, ax
    mov word [es:PT_SC_CLAIM], PT_MAGIC1
    mov word [es:PT_SC_CLAIM+2], PT_MAGIC2
    mov ax, [pt_win]
    mov [es:PT_SC_CLAIM+4], ax
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_canvas_init - build the row table and the BMP header, then white the canvas
; in:  [pt_ch]
; out: nothing; preserves all registers
;
; pt_rowtab is the whole of the bottom-up story: row y of the picture lives
; at PT_BMPHDR + (ch-1-y)*PT_STRIDE, and every access in the app is
; `mov di, [pt_rowtab+bx]` with bx = y*2. Nothing else in the file knows the
; rows are upside down.
; -----------------------------------------------------------------------------
pt_canvas_init:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov di, [pt_ch]
    dec di                          ; DI = ch-1-y, counted down to 0
    xor bx, bx                      ; BX = y*2, the table index
.row:
    mov ax, di
    mov cx, PT_STRIDE
    mul cx                          ; AX = (ch-1-y)*stride; DX = 0, since the
    add ax, PT_BMPHDR               ; whole picture is 62,720 bytes at most
    mov [pt_rowtab+bx], ax
    inc bx
    inc bx
    dec di
    jns .row

    call pt_bmp_hdr                 ; the DIB in front of row 0
    mov al, CWHITE
    call pt_wipe                    ; ...and a blank picture behind it
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_wipe - fill the whole canvas with colour AL, without touching undo
; in:  AL = colour 0..15
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_wipe:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bl, al
    mov cl, 4
    shl al, cl
    or al, bl
    mov ah, al                      ; AX = the colour in all four nibbles
    mov bx, PT_CVSEG
    mov es, bx
    mov di, PT_BMPHDR
    push ax
    mov ax, [pt_ch]
    mov cx, PT_STRIDE
    mul cx                          ; AX = pixel bytes (DX = 0, < 64K)
    shr ax, 1                       ; ...as words: PT_STRIDE is even
    mov cx, ax
    pop ax
    cld
    rep stosw
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_bmp_hdr - write the 118-byte DIB header that precedes row 0
; in:  [pt_ch]
; out: nothing; preserves all registers
;
; BITMAPFILEHEADER + BITMAPINFOHEADER + a 16-entry RGBQUAD palette, which is
; the standard EGA/VGA set (pt_pal_rgb) so the file opens with the right
; colours on any host. biHeight is positive: the rows behind this header are
; bottom-up, which is what makes a save a single write of the segment.
; -----------------------------------------------------------------------------
pt_bmp_hdr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, PT_CVSEG
    mov es, ax
    xor di, di
    mov ax, [pt_ch]
    mov dx, PT_STRIDE
    mul dx
    mov cx, ax                      ; CX = pixel bytes
    mov word [es:di], 0x4D42        ; 'BM'
    mov ax, cx
    add ax, PT_BMPHDR
    mov [es:di+2], ax               ; bfSize
    mov word [es:di+4], 0
    mov word [es:di+6], 0
    mov word [es:di+8], 0
    mov word [es:di+10], PT_BMPHDR  ; bfOffBits
    mov word [es:di+12], 0
    mov word [es:di+14], 40         ; biSize
    mov word [es:di+16], 0
    mov word [es:di+18], PT_CW      ; biWidth
    mov word [es:di+20], 0
    mov ax, [pt_ch]
    mov [es:di+22], ax              ; biHeight (positive = bottom-up)
    mov word [es:di+24], 0
    mov word [es:di+26], 1          ; biPlanes
    mov word [es:di+28], 4          ; biBitCount
    mov word [es:di+30], 0          ; biCompression = BI_RGB
    mov word [es:di+32], 0
    mov [es:di+34], cx              ; biSizeImage
    mov word [es:di+36], 0
    mov word [es:di+38], 0          ; biXPelsPerMeter
    mov word [es:di+40], 0
    mov word [es:di+42], 0          ; biYPelsPerMeter
    mov word [es:di+44], 0
    mov word [es:di+46], 16         ; biClrUsed
    mov word [es:di+48], 0
    mov word [es:di+50], 0          ; biClrImportant
    mov word [es:di+52], 0
    mov si, pt_pal_rgb              ; 16 x RGB -> 16 x BGRA
    mov di, 54
    mov cx, 16
.pal:
    mov al, [si+2]
    mov [es:di], al                 ; blue
    mov al, [si+1]
    mov [es:di+1], al               ; green
    mov al, [si]
    mov [es:di+2], al               ; red
    mov byte [es:di+3], 0
    add si, 3
    add di, 4
    loop .pal
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_font_init - copy the ROM 8x8 font into pt_glyphs
; in:  nothing
; out: pt_glyphs = glyphs for 32..126; preserves all registers
;
; The kernel's own font buffer is not in the API table, and the text tool
; has to write glyph pixels into the CANVAS rather than onto the screen, so
; it needs the bitmaps themselves. This is font_init's probe verbatim
; (SPEC.md 6): zero ES:BP, ask int 10h AX=1130h BH=03h, and fall back to the
; IBM ROM 8x8 set at F000:FA6E when a pre-EGA BIOS leaves the pair alone.
; int 10h AH=11h reads a table pointer and touches no adapter register, so it
; is safe long after the mode was set.
; -----------------------------------------------------------------------------
pt_font_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor ax, ax
    mov es, ax
    xor bp, bp
    mov ax, 0x1130
    mov bh, 3
    int 0x10                        ; ES:BP -> 8x8 font, if the BIOS has one
    mov ax, es
    or ax, bp
    jnz .got
    mov ax, 0xF000
    mov es, ax
    mov bp, 0xFA6E
.got:
    mov si, 32 * 8                  ; first glyph we keep is the space
    mov di, pt_glyphs
    mov cx, 95 * 8
.copy:
    mov al, [es:bp+si]
    mov [di], al
    inc si
    inc di
    loop .copy
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_org - latch the content and canvas origins for this callback
; in:  BX = window ptr
; out: [pt_ox]/[pt_oy] = content origin, [pt_cx0]/[pt_cy0] = canvas origin,
;      [pt_stripy] = content y of the bottom strip; preserves all registers
;
; Every callback starts here: the window moves between calls, and the record
; is the truth about where it is (SPEC.md 11).
; -----------------------------------------------------------------------------
pt_org:
    push ax
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [pt_ox], ax
    mov [pt_oy], dx
    add ax, PT_CV_X
    mov [pt_cx0], ax
    mov [pt_cy0], dx
    mov ax, [pt_ch]
    inc ax
    mov [pt_stripy], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_clip - clip the pending rectangle to the canvas
; in:  [pt_rx1],[pt_ry1],[pt_rx2],[pt_ry2] - SIGNED canvas coords, ordered
;      (x1<=x2, y1<=y2; callers normalise)
; out: CF=1 nothing left, else [pt_cx1]..[pt_cy2] = the clipped rectangle;
;      preserves all registers
;
; Signed throughout: a brush dab centred one pixel inside the left edge has a
; negative x1 by design, and so does every stroke the user drags off the
; canvas. Rejecting early is also what makes dragging far outside cheap - the
; step loop still runs, but no gfx call is made.
; -----------------------------------------------------------------------------
pt_clip:
    push ax
    mov ax, [pt_rx1]
    cmp ax, PT_CW
    jge .empty
    or ax, ax
    jns .x1ok
    xor ax, ax
.x1ok:
    mov [pt_cx1], ax
    mov ax, [pt_rx2]
    or ax, ax
    js .empty
    cmp ax, PT_CW
    jl .x2ok
    mov ax, PT_CW - 1
.x2ok:
    mov [pt_cx2], ax
    mov ax, [pt_ry1]
    cmp ax, [pt_ch]
    jge .empty
    or ax, ax
    jns .y1ok
    xor ax, ax
.y1ok:
    mov [pt_cy1], ax
    mov ax, [pt_ry2]
    or ax, ax
    js .empty
    cmp ax, [pt_ch]
    jl .y2ok
    mov ax, [pt_ch]
    dec ax
.y2ok:
    mov [pt_cy2], ax
    mov ax, [pt_cx1]
    cmp ax, [pt_cx2]
    jg .empty
    mov ax, [pt_cy1]
    cmp ax, [pt_cy2]
    jg .empty
    pop ax
    clc
    ret
.empty:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_rect - THE drawing primitive: fill a canvas rectangle and mirror it to
;           the screen with one gfx_fill
; in:  [pt_rx1]..[pt_ry2] = signed, ordered canvas rect; [pt_ink] = colour
;      (the OPERATION's colour - [pt_col] is the user's chosen one, which the
;      eraser and a paste must not disturb); [pt_noscr] non-zero to write
;      pixels only; gfx lock held by the caller unless [pt_noscr] is set
; out: nothing; preserves all registers
;
; Brush dabs, stroke edges, eraser, shape outlines and fills, text pixels,
; selection clears and paste rows all come through here, which is why the
; nibble edge masks are computed once per call and the row loop is a
; `rep stosb` between two read-modify-writes. The screen half is a single
; gfx_fill: the kernel's own clipping and its back-buffer dispatch
; (SPEC.md 32/39.5) then apply, so the same call is correct on all three
; adapters and with double buffering either way.
; -----------------------------------------------------------------------------
pt_rect:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call pt_clip
    jc .out
    mov ax, [pt_cy1]                ; save the rows the undo image still owes
    mov dx, [pt_cy2]
    call pt_umark

    ; --- edge masks and the middle run, once for every row -------------------
    mov al, [pt_ink]
    mov bl, al
    mov cl, 4
    shl bl, cl
    or bl, al                       ; the colour in both nibbles, and it goes
    mov [pt_fbyte], bl              ; to memory: BX becomes the row index below
    mov ax, [pt_cx1]
    mov cx, ax
    shr ax, 1
    mov [pt_lb], ax                 ; left byte index within the row
    mov dx, [pt_cx2]
    mov di, dx
    shr dx, 1
    sub dx, ax
    mov [pt_span], dx               ; right byte - left byte
    mov byte [pt_lmask], 0
    test cl, 1
    jz .lm
    mov byte [pt_lmask], 0xF0       ; x1 is a low nibble: keep the high one
.lm:
    mov byte [pt_rmask], 0
    test di, 1
    jnz .rm
    mov byte [pt_rmask], 0x0F       ; x2 is a high nibble: keep the low one
.rm:
    or dx, dx
    jnz .wide
    mov al, [pt_rmask]              ; one byte holds both edges
    or [pt_lmask], al
.wide:
    mov al, [pt_lmask]
    not al
    and al, bl
    mov [pt_lval], al
    mov al, [pt_rmask]
    not al
    and al, bl
    mov [pt_rval], al

    mov ax, PT_CVSEG
    mov es, ax
    cld
    mov bx, [pt_cy1]
    add bx, bx                      ; BX = y*2, the row-table index
.row:
    mov di, [pt_rowtab+bx]
    add di, [pt_lb]
    mov al, [es:di]                 ; left (or only) byte
    and al, [pt_lmask]
    or al, [pt_lval]
    mov [es:di], al
    mov cx, [pt_span]
    jcxz .rowdone                   ; single byte: the masks were merged
    dec cx                          ; CX = full bytes between the edges
    inc di
    mov al, [pt_fbyte]
    jcxz .right
    rep stosb                       ; DI lands on the right edge byte
.right:
    mov al, [es:di]
    and al, [pt_rmask]
    or al, [pt_rval]
    mov [es:di], al
.rowdone:
    inc bx
    inc bx
    mov ax, [pt_cy2]
    add ax, ax
    cmp bx, ax
    jbe .row

    cmp byte [pt_noscr], 0
    jne .out
    mov al, [pt_ink]
    call OSAPI_SET_COLOR
    mov ax, [pt_cx1]
    add ax, [pt_cx0]
    mov bx, [pt_cy1]
    add bx, [pt_cy0]
    mov cx, [pt_cx2]
    add cx, [pt_cx0]
    mov dx, [pt_cy2]
    add dx, [pt_cy0]
    call OSAPI_GFX_FILL
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_umark - copy rows AX..DX into the undo image, once each
; in:  AX = first row, DX = last row (both already clipped to the canvas)
; out: nothing; preserves all registers
;
; This is what makes undo affordable. A full-canvas snapshot on every
; mouse-down would cost 62,720 bytes of copying before the first pixel
; appears - a visible hitch at the start of every stroke. Instead the
; generation starts with an empty row bitmap (pt_undo_new) and a row is
; copied the first time something touches it, so a small stroke pays for the
; handful of rows it crosses and only a whole-canvas operation pays in full.
;
; Row copies need DS = PT_CVSEG for `rep movsw`, which means the bitmap
; itself is unreachable while DS is switched - hence the switch is inside the
; per-row body, around the string move alone. BP carries the row offset
; because consecutive rows are exactly PT_STRIDE apart (downward: the rows
; are stored bottom-up), so no table lookup is needed once the first row is
; located.
; -----------------------------------------------------------------------------
pt_umark:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [pt_undo_off], 0
    jne .out                        ; we ARE the undo: do not re-snapshot
    mov bx, ax                      ; BX = row counter
    mov si, bx
    add si, si
    mov bp, [pt_rowtab+si]          ; BP = this row's byte offset
    mov ax, PT_UNSEG
    mov es, ax
    cld
.loop:
    mov si, bx
    and si, 7
    mov ah, [pt_bit8+si]            ; AH = this row's bit
    mov si, bx
    shr si, 1
    shr si, 1
    shr si, 1                       ; SI = the bitmap byte
    test [pt_umask+si], ah
    jnz .next
    or [pt_umask+si], ah
    mov si, bp
    mov di, bp
    mov cx, PT_STRIDE / 2
    push ds
    mov ax, PT_CVSEG
    mov ds, ax
    rep movsw
    pop ds
.next:
    sub bp, PT_STRIDE
    inc bx
    cmp bx, dx
    jbe .loop
    mov byte [pt_undo_ok], 1
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_undo_new - open a fresh undo generation (called at the start of every
;               operation that changes pixels)
; in:  nothing
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_undo_new:
    push cx
    push di
    mov di, pt_umask
    mov cx, 36
.z:
    mov byte [di], 0
    inc di
    loop .z
    mov byte [pt_undo_ok], 0
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; pt_undo_swap - Undo, and Redo, and Undo again: EXCHANGE the marked rows
; in:  nothing ([pt_umask], [pt_undo_ok]); gfx lock held
; out: nothing; preserves all registers
;
; Because the two images are swapped rather than copied, one buffer and one
; bitmap give unlimited alternation between the two states - "Undo" and
; "Redo" are the same instruction. The affected span is blitted once at the
; end rather than row by row: a rectangle blit of the union costs one
; coalescing pass over rows that may not have changed, which is far cheaper
; than a gfx call per row would be.
; -----------------------------------------------------------------------------
pt_undo_swap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [pt_undo_ok], 0
    je .out
    mov ax, PT_CVSEG
    mov es, ax
    mov dx, [pt_ch]
    dec dx                          ; DX = last row
    xor bx, bx                      ; BX = row counter
    mov bp, [pt_rowtab]             ; row 0's offset
    mov word [pt_uy1], -1
    mov word [pt_uy2], -1
.loop:
    mov si, bx
    and si, 7
    mov ah, [pt_bit8+si]
    mov si, bx
    shr si, 1
    shr si, 1
    shr si, 1
    test [pt_umask+si], ah
    jz .next
    cmp word [pt_uy1], -1
    jne .have1
    mov [pt_uy1], bx
.have1:
    mov [pt_uy2], bx
    mov si, bp
    mov di, bp
    mov cx, PT_STRIDE / 2
    push ds
    mov ax, PT_UNSEG
    mov ds, ax
.word:
    mov ax, [si]                    ; DS = the undo image
    xchg ax, [es:di]                ; ES = the canvas
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .word
    pop ds
.next:
    sub bp, PT_STRIDE
    inc bx
    cmp bx, dx
    jbe .loop
    cmp word [pt_uy1], -1
    je .out
    mov word [pt_rx1], 0
    mov word [pt_rx2], PT_CW - 1
    mov ax, [pt_uy1]
    mov [pt_ry1], ax
    mov ax, [pt_uy2]
    mov [pt_ry2], ax
    call pt_blit
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_runend - extend a run of one colour along a canvas row
; in:  AL = the run's colour, DX = its first x, BP = the row's byte offset,
;      [pt_cx2] = last x to consider, ES = PT_CVSEG
; out: DX = the run's last x, AL = the colour again
; clobbers: BX, CX, DI, flags
;
; The blit's inner loop, and the reason a repaint is affordable at all: a run
; of colour c is a run of BYTES equal to c|c<<4, so `repe scasb` walks it two
; pixels at a time at 15 clocks a byte instead of a decode per pixel. The odd
; ends are handled by hand - the first pixel when the run starts on a low
; nibble, the last when it ends on a high one.
; -----------------------------------------------------------------------------
pt_runend:
    mov bl, al
    mov cl, 4
    shl bl, cl
    or bl, al                       ; BL = the byte a matching pair holds
    mov cx, dx
    test cl, 1
    jz .even
    inc cx                          ; the run's first pixel is a low nibble:
.even:                              ; pair scanning starts at the next pixel
    mov ax, [pt_cx2]
    sub ax, cx
    js .tail
    inc ax
    shr ax, 1                       ; AX = whole pairs available from CX
    jz .tail
    mov di, cx
    shr di, 1
    add di, bp                      ; ES:DI = the first pair's byte
    mov dx, cx                      ; DX = the first pixel not yet in the run
    mov [pt_scan0], di              ; SI belongs to the caller's row loop, so
    mov cx, ax                      ; the scan's origin is parked in memory
    mov al, bl
    cld
    repe scasb
    mov ax, di                      ; `mov` leaves scasb's ZF alone, and the
    jne .mism                       ; branch has to read it before anything
    sub ax, [pt_scan0]              ; else does: every byte matched
    jmp short .have
.mism:
    dec ax                          ; the mismatch sits at DI-1, so it is not
    sub ax, [pt_scan0]              ; part of the run
.have:
    add ax, ax                      ; bytes matched -> pixels
    add dx, ax                      ; DX = first pixel beyond the pair run
.tail:
    mov al, bl
    and al, 0x0F                    ; the colour, back from the pair byte
    cmp dx, [pt_cx2]
    jg .done
    mov di, dx
    shr di, 1
    add di, bp
    mov ah, [es:di]
    test dl, 1
    jnz .lo
    mov cl, 4
    shr ah, cl
.lo:
    and ah, 0x0F
    cmp ah, al
    jne .done
    inc dx                          ; the odd final nibble matches too
.done:
    dec dx                          ; DX = the last pixel IN the run
    ret

; -----------------------------------------------------------------------------
; pt_blit - put a canvas rectangle on screen, run-coalesced
; in:  [pt_rx1]..[pt_ry2] = canvas rect; gfx lock held
; out: nothing; preserves all registers
;
; The path for everything that cannot know what it changed: W_PAINT, undo,
; paste, a file load, and erasing the text caret. One gfx_hline per run of
; equal pixels, so a flat picture costs a call per row and a dithered one
; costs more - which is the right shape for a paint program, where the
; expensive case is also the rare one.
; -----------------------------------------------------------------------------
pt_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call pt_clip
    jc .out
    mov ax, PT_CVSEG
    mov es, ax
    mov si, [pt_cy1]                ; SI = canvas row
.row:
    mov bx, si
    add bx, bx
    mov bp, [pt_rowtab+bx]
    mov dx, [pt_cx1]
.run:
    mov bx, dx
    shr bx, 1
    add bx, bp
    mov al, [es:bx]
    test dl, 1
    jnz .lo
    mov cl, 4
    shr al, cl
.lo:
    and al, 0x0F                    ; AL = the run's colour
    mov [pt_runx], dx
    call pt_runend                  ; DX = run end, AL = colour
    mov [pt_runy], dx
    call OSAPI_SET_COLOR
    mov ax, [pt_runx]
    add ax, [pt_cx0]
    mov bx, [pt_runy]
    add bx, [pt_cx0]
    mov dx, si
    add dx, [pt_cy0]
    call OSAPI_GFX_HLINE
    mov dx, [pt_runy]
    inc dx
    cmp dx, [pt_cx2]
    jbe .run
    inc si
    cmp si, [pt_cy2]
    jbe .row
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_blit_all - blit the whole canvas
; in:  nothing; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_blit_all:
    push ax
    mov word [pt_rx1], 0
    mov word [pt_ry1], 0
    mov word [pt_rx2], PT_CW - 1
    mov ax, [pt_ch]
    dec ax
    mov [pt_ry2], ax
    call pt_blit
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_getpx - the colour of one canvas pixel
; in:  CX = x, DX = y (both inside the canvas)
; out: AL = colour 0..15; clobbers AX and flags only
; -----------------------------------------------------------------------------
pt_getpx:
    push bx
    push cx
    push es
    mov bx, PT_CVSEG
    mov es, bx
    mov bx, dx
    add bx, bx
    mov bx, [pt_rowtab+bx]
    mov ax, cx
    shr ax, 1
    add bx, ax
    mov al, [es:bx]
    test cl, 1
    jnz .lo
    mov cl, 4
    shr al, cl
.lo:
    and al, 0x0F
    pop es
    pop cx
    pop bx
    ret

; =============================================================================
; Chrome - the tool palette, the bottom strip, and the notice window
;
; All of it is drawn in CONTENT coordinates through the three helpers below,
; which add [pt_ox]/[pt_oy] on the way out. None of it is on a hot path: the
; palette is redrawn when the tool changes and the strip when a colour, a
; width or a toggle does.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_cfill / pt_cframe - solid rect / 1px outline, content coords, [pt_pen]
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content-relative); [pt_pen] = colour
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_cfill:
    push ax
    push bx
    push cx
    push dx
    call pt_cprep
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_cframe:
    push ax
    push bx
    push cx
    push dx
    call pt_cprep
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_cprep - content coords -> screen coords, and load the pen
; in:  AX/BX/CX/DX = content rect
; out: AX/BX/CX/DX = screen rect, [gfx_color] = [pt_pen]
pt_cprep:
    add ax, [pt_ox]
    add bx, [pt_oy]
    add cx, [pt_ox]
    add dx, [pt_oy]
    push ax
    mov al, [pt_pen]
    call OSAPI_SET_COLOR
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_ctext - draw a NUL string in content coordinates
; in:  SI = string, CX = x, DX = y (content-relative); [pt_pen] = colour
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_ctext:
    push ax
    push cx
    push dx
    mov al, [pt_pen]
    call OSAPI_SET_COLOR
    add cx, [pt_ox]
    add dx, [pt_oy]
    call OSAPI_FONT_STR
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_icon16 - draw a 16x16 1bpp glyph in [pt_pen]; clear bits draw nothing
; in:  SI = 16 rows (bit 15 = leftmost), CX = content x, DX = content y
; out: nothing; preserves all registers
;
; Set bits are emitted as horizontal runs, so a typical glyph row costs one
; or two gfx_hline calls instead of sixteen gfx_pixel calls. The kernel's own
; ico_draw is not in the API table (SPEC.md 20.3), which is why this exists.
; -----------------------------------------------------------------------------
pt_icon16:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [pt_ic_x], cx
    mov [pt_ic_y], dx
    mov bp, 16                      ; rows left (a value, never dereferenced)
.row:
    mov ax, [si]
    add si, 2
    or ax, ax
    jz .next
    mov word [pt_ic_rs], -1         ; no run open
    xor di, di                      ; DI = column
    mov cx, 16
.bit:
    shl ax, 1                       ; CF = this pixel, leftmost first
    jc .one
    cmp word [pt_ic_rs], -1
    je .adv
    call pt_ic_flush                ; run ended at DI-1
    jmp short .adv
.one:
    cmp word [pt_ic_rs], -1
    jne .adv
    mov [pt_ic_rs], di              ; run opens here
.adv:
    inc di
    loop .bit
    cmp word [pt_ic_rs], -1
    je .next
    call pt_ic_flush                ; the row ended with a run open
.next:
    inc word [pt_ic_y]
    dec bp
    jnz .row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_ic_flush - emit the open run [pt_ic_rs .. DI-1] on row [pt_ic_y]
; in:  DI = the column that ended the run; out: [pt_ic_rs] = -1
; Preserves every register: it is called from the middle of pt_icon16's bit
; loop, where AX holds the row still being shifted and CX the bits left.
pt_ic_flush:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_ic_rs]
    add ax, [pt_ic_x]
    mov bx, di
    dec bx
    add bx, [pt_ic_x]
    mov dx, [pt_ic_y]
    push ax
    mov al, [pt_pen]
    call OSAPI_SET_COLOR
    pop ax
    add ax, [pt_ox]
    add bx, [pt_ox]
    add dx, [pt_oy]
    call OSAPI_GFX_HLINE
    mov word [pt_ic_rs], -1
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_draw_pal - the eight tool buttons, two columns of four
; in:  nothing; gfx lock held
; out: nothing; preserves all registers
;
; The selected tool is drawn inverted - black well, white glyph - which is the
; one state indication that survives SPEC.md 39.4's colour reduction intact.
; -----------------------------------------------------------------------------
pt_draw_pal:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = tool index
.tool:
    call pt_btn_xy                  ; CX = x, DX = y for button DI
    mov [pt_bx], cx
    mov [pt_by], dx
    mov byte [pt_pen], CWHITE       ; the well: black when this tool is in hand
    xor ax, ax
    mov al, [pt_tool]
    cmp ax, di
    jne .well
    mov byte [pt_pen], CBLACK
.well:
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, PT_BW - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    mov byte [pt_pen], CBLACK       ; ...and the glyph inverts with it
    xor ax, ax
    mov al, [pt_tool]
    cmp ax, di
    jne .glyph
    mov byte [pt_pen], CWHITE
.glyph:
    mov bx, di
    add bx, bx
    mov si, [pt_ic_tab+bx]
    mov cx, [pt_bx]
    add cx, 2
    mov dx, [pt_by]
    add dx, 2
    call pt_icon16
    inc di
    cmp di, PT_NTOOL
    jb .tool
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_btn_xy - top-left content coords of tool button DI
; in:  DI = 0..7
; out: CX = x, DX = y; clobbers AX, flags
; -----------------------------------------------------------------------------
pt_btn_xy:
    mov cx, PT_PAL_X0
    test di, 1
    jz .col
    mov cx, PT_PAL_X1
.col:
    mov ax, di
    shr ax, 1                       ; row = index/2
    mov dx, ax
    add dx, dx                      ; *2
    add dx, dx                      ; *4
    add dx, ax                      ; *5
    add dx, dx                      ; *10... PT_PAL_DY is 21, so do it long-hand
    add dx, dx                      ; *20
    add dx, ax                      ; *21
    add dx, PT_PAL_Y0
    ret

; -----------------------------------------------------------------------------
; pt_swcol - the colour a palette swatch stands for
; in:  AX = swatch index (0..[pt_ncol]-1)
; out: AL = colour index; clobbers AH, flags
;
; On VGA the swatch IS the colour. On a 1bpp adapter only three of the sixteen
; survive as distinct classes (SPEC.md 39.4), so the palette shows exactly
; those three and nothing that would be a lie.
; -----------------------------------------------------------------------------
pt_swcol:
    cmp byte [pt_mono], 0
    je .direct
    push bx
    mov bx, ax
    mov al, [pt_mono_pal+bx]
    pop bx
    ret
.direct:
    ret

; -----------------------------------------------------------------------------
; pt_draw_strip - the bottom strip: widths, current colour, palette, toggles
; in:  nothing; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_draw_strip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- separator, then a clean white bed --------------------------------
    mov byte [pt_pen], CBLACK
    xor ax, ax
    mov bx, [pt_ch]
    mov cx, PT_CONT_W - 1
    mov dx, bx
    call pt_cfill
    mov byte [pt_pen], CWHITE
    xor ax, ax
    mov bx, [pt_stripy]
    mov cx, PT_CONT_W - 1
    mov dx, bx
    add dx, PT_STRIP_H - 1
    call pt_cfill

    ; --- four line widths for the tool in hand ----------------------------
    xor di, di
.width:
    mov ax, di
    mov cx, PT_TH_DX
    mul cx
    add ax, PT_TH_X
    mov [pt_bx], ax                 ; button x
    mov ax, [pt_stripy]
    inc ax
    mov [pt_by], ax
    call pt_thsel                   ; AL = the selected width index
    xor ah, ah
    cmp ax, di
    mov byte [pt_pen], CWHITE
    jne .wbg
    mov byte [pt_pen], CBLACK
.wbg:
    mov ax, [pt_bx]
    mov bx, [pt_by]
    mov cx, ax
    add cx, PT_BW - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    ; the glyph: a centred square whose side is the width itself, capped so
    ; the 32px eraser still fits in a 20px button
    call pt_thsel
    xor ah, ah
    cmp ax, di
    mov byte [pt_pen], CBLACK
    jne .wg
    mov byte [pt_pen], CWHITE
.wg:
    mov bx, di
    call pt_thval                   ; AX = the width in pixels
    cmp ax, 14
    jbe .wcap
    mov ax, 14
.wcap:
    mov cx, ax                      ; CX = side
    mov ax, PT_BW
    sub ax, cx
    shr ax, 1                       ; AX = inset
    mov bx, [pt_by]
    add bx, ax
    add ax, [pt_bx]
    mov dx, bx
    add dx, cx
    dec dx
    add cx, ax
    dec cx
    call pt_cfill
    inc di
    cmp di, 4
    jb .width

    ; --- the current colour, in its own well ------------------------------
    mov al, [pt_col]
    mov [pt_pen], al
    mov ax, PT_CUR_X
    mov bx, [pt_stripy]
    inc bx
    mov cx, ax
    add cx, PT_BW - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe

    ; --- the palette ------------------------------------------------------
    xor di, di
.sw:
    mov ax, di
    mov cx, PT_SW_DX
    mul cx
    add ax, PT_SW_X
    mov [pt_bx], ax
    mov ax, di
    call pt_swcol
    mov [pt_pen], al
    mov ax, [pt_bx]
    mov bx, [pt_stripy]
    inc bx
    mov cx, ax
    add cx, PT_BW - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    mov ax, di                      ; the swatch in use gets a second frame
    call pt_swcol
    cmp al, [pt_col]
    jne .swnext
    mov ax, [pt_bx]
    dec ax
    mov bx, [pt_stripy]
    mov cx, ax
    add cx, PT_BW + 1
    mov dx, bx
    add dx, PT_BW + 1
    call pt_cframe
.swnext:
    inc di
    xor ax, ax
    mov al, [pt_ncol]
    cmp di, ax
    jb .sw

    ; --- filled-shapes toggle, and the font scale -------------------------
    mov byte [pt_pen], CWHITE
    mov ax, PT_FIL_X
    mov bx, [pt_stripy]
    inc bx
    mov cx, ax
    add cx, PT_BTN_W16 - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    mov ax, PT_FIL_X + 4
    mov bx, [pt_stripy]
    add bx, 6
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    cmp byte [pt_filled], 0
    je .hollow
    call pt_cfill
    jmp short .fnt
.hollow:
    call pt_cframe
.fnt:
    mov byte [pt_pen], CWHITE
    mov ax, PT_FNT_X
    mov bx, [pt_stripy]
    inc bx
    mov cx, ax
    add cx, PT_BTN_W16 - 1
    mov dx, bx
    add dx, PT_BW - 1
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    mov al, [pt_fscale]
    add al, '0'
    mov [pt_fdigit], al
    mov si, pt_fdigit
    mov cx, PT_FNT_X + 4
    mov dx, [pt_stripy]
    add dx, 7
    call pt_ctext
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_thsel / pt_thval - the width selector, per tool
; pt_thsel: out AL = the selected index 0..3 for the tool in hand
; pt_thval: in BX = index 0..3; out AX = that index's width in pixels
;
; The eraser has its own set and its own selection, which is how "much thicker
; by default" is expressed: it starts at 16px against the pencil's 1px.
; -----------------------------------------------------------------------------
pt_thsel:
    mov al, [pt_thick]
    cmp byte [pt_tool], PT_T_ERASER
    jne .out
    mov al, [pt_ethick]
.out:
    ret

pt_thval:
    push bx
    cmp byte [pt_tool], PT_T_ERASER
    jne .pen
    add bx, 4
.pen:
    xor ax, ax
    mov al, [pt_thtab+bx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_penw - the line width a shape outline is drawn with
; out: CX = pixels (at least 1); preserves all other registers
;
; Deliberately the PENCIL's selection, not pt_brush's: the eraser's width set
; has nothing to do with a rectangle's border, and the strip shows the pencil
; set whenever a shape tool is in hand.
; -----------------------------------------------------------------------------
pt_penw:
    push ax
    push bx
    xor bx, bx
    mov bl, [pt_thick]
    xor ax, ax
    mov al, [pt_thtab+bx]
    mov cx, ax
    or cx, cx
    jnz .out
    mov cx, 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_brush - the width the tool in hand paints with
; out: AX = pixels; preserves all other registers
; -----------------------------------------------------------------------------
pt_brush:
    push bx
    call pt_thsel
    xor bx, bx
    mov bl, al
    call pt_thval
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_paint - W_PAINT: chrome, then the picture
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call pt_org
    cmp byte [pt_mode], PT_M_LIVE
    jne .notice
    mov byte [pt_msgon], 0          ; the toast does not survive a repaint
    call pt_draw_pal
    call pt_draw_strip
    mov byte [pt_pen], CBLACK
    mov ax, PT_SEPX
    mov bx, 0
    mov cx, ax
    mov dx, [pt_ch]
    dec dx
    call pt_cfill                   ; the palette/canvas divider
    call pt_blit_all
    mov byte [pt_selshown], 0
    call pt_marq                    ; the marquee, if a selection is live
    jmp short .out
.notice:
    xor bx, bx
    mov bl, [pt_mode]
    add bx, bx
    mov si, [pt_notice+bx-2]        ; mode 1 is the first entry
    mov byte [pt_pen], CBLACK
    mov cx, 8
    mov dx, 8
    call pt_ctext
    mov si, pt_s_note2
    mov cx, 8
    mov dx, 24
    call pt_ctext
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Screen-only drawing in canvas coordinates
;
; The caret, the toast and the rubber band are NOT part of the picture, so
; they never go near pt_rect. They are put straight on the glass and taken
; back off by blitting the canvas underneath.
; =============================================================================

; pt_sfill / pt_sframe - as pt_cfill/pt_cframe, but in CANVAS coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (canvas); [pt_pen] = colour
; out: nothing; preserves all registers
pt_sfill:
    push ax
    push bx
    push cx
    push dx
    call pt_sprep
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_sframe:
    push ax
    push bx
    push cx
    push dx
    call pt_sprep
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_sprep:
    add ax, [pt_cx0]
    add bx, [pt_cy0]
    add cx, [pt_cx0]
    add dx, [pt_cy0]
    push ax
    mov al, [pt_pen]
    call OSAPI_SET_COLOR
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_msg_show / pt_msg_hide - the toast, top-left of the canvas
; in:  SI = NUL string (show)
; out: nothing; preserves all registers
;
; The notepad idiom (SPEC.md 27.1): a save or a load has to say something, and
; a paint program has no status line to spare. It sits on the glass, so
; hiding it is a blit of the canvas rows it covered and a repaint erases it
; for free.
; -----------------------------------------------------------------------------
pt_msg_show:
    push ax
    push bx
    push cx
    push dx
    push si
    call pt_msg_hide
    call OSAPI_FONT_WIDTH           ; AX = pixel width of SI
    add ax, 7
    mov [pt_msgw], ax
    mov byte [pt_pen], CWHITE
    mov ax, 2
    mov bx, 2
    mov cx, [pt_msgw]
    add cx, 2
    mov dx, 15
    call pt_sfill
    mov byte [pt_pen], CBLACK
    call pt_sframe
    mov cx, 6
    add cx, [pt_cx0]
    mov dx, 6
    add dx, [pt_cy0]
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call OSAPI_FONT_STR
    mov byte [pt_msgon], 1
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_msg_hide:
    push ax
    cmp byte [pt_msgon], 0
    je .out
    mov byte [pt_msgon], 0
    mov word [pt_rx1], 2
    mov word [pt_ry1], 2
    mov ax, [pt_msgw]
    add ax, 4
    mov [pt_rx2], ax
    mov word [pt_ry2], 15
    call pt_blit
.out:
    pop ax
    ret

; =============================================================================
; Input - the click ladder, and the three tracking loops
; =============================================================================

; -----------------------------------------------------------------------------
; pt_wait / pt_wait_tick - hand the machine back mid-drag
; in:  gfx lock HELD
; out: gfx lock HELD; preserves all registers
;
; The lock is what makes this necessary in both directions. Holding it across
; a whole stroke would keep every other task's painting out (fine) but would
; also postpone gfx_unlock's back-buffer flush until the button came up, so
; with double buffering on (SPEC.md 32) the stroke would be invisible while
; being drawn. Releasing it on every mouse sample instead is what puts the
; ink on the glass, and the yield is what keeps the clock ticking.
;
; pt_wait_tick is the idle form: when the pointer has not moved there is
; nothing to draw, and polling at tick rate costs nothing.
; -----------------------------------------------------------------------------
pt_wait:
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    ret

pt_wait_tick:
    push ax
    push bx
    call OSAPI_GFX_UNLOCK
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    call OSAPI_GFX_LOCK
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_click - W_ONCLICK: route the press to the palette, the strip or a tool
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [pt_mode], PT_M_LIVE
    jne .out
    mov bx, si
    call pt_org
    call pt_msg_hide
    sub cx, [pt_ox]                 ; -> content-relative
    sub dx, [pt_oy]
    mov ax, [pt_ch]                 ; the strip spans the WHOLE content width,
    cmp dx, ax                      ; including the columns under the tool
    jge .strip                      ; palette - so y decides first, or the two
    cmp cx, PT_CV_X                 ; leftmost width buttons are unreachable
    jl .palette
    jmp short .canvas
.palette:
    call pt_pal_click
    jmp short .out
.strip:
    call pt_strip_click
    jmp short .out
.canvas:
    sub cx, PT_CV_X                 ; -> canvas coords
    mov [pt_ax], cx
    mov [pt_ay], dx
    call pt_canvas_click
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_pal_click - a press in the tool column
; in:  CX = content x, DX = content y; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_pal_click:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [pt_hitx], cx               ; pt_btn_xy answers in CX/DX, so the click
    mov [pt_hity], dx               ; point has to move out of the way first
    xor di, di
.tool:
    call pt_btn_xy                  ; CX = button x, DX = button y
    mov ax, [pt_hitx]
    sub ax, cx
    js .next
    cmp ax, PT_BW
    jge .next
    mov ax, [pt_hity]
    sub ax, dx
    js .next
    cmp ax, PT_BW
    jge .next
    ; hit: switch tool
    call pt_text_end
    call pt_sel_drop
    mov ax, di
    mov [pt_tool], al
    call pt_draw_pal
    call pt_draw_strip              ; the width set belongs to the tool
    jmp short .out
.next:
    inc di
    cmp di, PT_NTOOL
    jb .tool
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_strip_click - a press in the bottom strip
; in:  CX = content x, DX = content y; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_strip_click:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, dx
    sub ax, [pt_stripy]
    js .out                         ; the separator row itself
    ; --- widths -----------------------------------------------------------
    mov ax, cx
    sub ax, PT_TH_X
    js .out
    cmp ax, PT_TH_DX * 4
    jge .colours
    mov bx, PT_TH_DX
    xor dx, dx
    div bx                          ; AX = button, DX = offset inside it
    cmp dx, PT_BW
    jge .out
    cmp byte [pt_tool], PT_T_ERASER
    jne .pen
    mov [pt_ethick], al
    jmp short .wdone
.pen:
    mov [pt_thick], al
.wdone:
    call pt_draw_strip
    jmp short .out
.colours:
    mov ax, cx
    sub ax, PT_SW_X
    js .out
    mov bx, PT_SW_DX
    xor dx, dx
    div bx
    xor bx, bx
    mov bl, [pt_ncol]
    cmp ax, bx
    jge .toggles
    cmp dx, PT_BW
    jge .out
    call pt_swcol                   ; AX = swatch index -> AL = colour
    mov [pt_col], al
    call pt_draw_strip
    jmp short .out
.toggles:
    mov ax, cx
    sub ax, PT_FIL_X
    js .out
    cmp ax, PT_BTN_W16
    jge .fontbtn
    xor byte [pt_filled], 1
    call pt_draw_strip
    jmp short .out
.fontbtn:
    mov ax, cx
    sub ax, PT_FNT_X
    js .out
    cmp ax, PT_BTN_W16
    jge .out
    mov al, [pt_gsh]
    inc al                          ; 1x -> 2x -> 4x -> 1x
    cmp al, 3
    jb .fset
    xor al, al
.fset:
    call pt_setscale                ; BOTH words: the digit the strip shows
    call pt_draw_strip              ; and the shift the glyphs are drawn with
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_canvas_click - a press on the picture: run the tool in hand
; in:  [pt_ax],[pt_ay] = canvas coords of the press; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_canvas_click:
    push ax
    push bx
    mov al, [pt_tool]
    cmp al, PT_T_SEL
    je .sel
    cmp al, PT_T_TEXT
    je .text
    call pt_text_end                ; any other tool ends a text run...
    call pt_sel_drop                ; ...and drawing deselects
    mov al, [pt_tool]
    cmp al, PT_T_PENCIL
    je .pencil
    cmp al, PT_T_ERASER
    je .eraser
    cmp al, PT_T_DROP
    je .drop
    cmp al, PT_T_FILL
    je .fill
    cmp al, PT_T_RECT
    je .shape
    cmp al, PT_T_OVAL
    je .shape
    jmp short .out
.pencil:
    mov al, [pt_col]
    mov [pt_ink], al
    call pt_stroke
    jmp short .out
.eraser:
    mov byte [pt_ink], CWHITE
    call pt_stroke
    jmp short .out
.drop:
    call pt_pick
    jmp short .out
.fill:
    call pt_flood
    jmp short .out
.shape:
    call pt_rubber
    call pt_shape_commit
    jmp short .out
.sel:
    call pt_text_end
    call pt_sel_drop
    call pt_rubber
    call pt_sel_take
    jmp short .out
.text:
    call pt_sel_drop
    call pt_text_place
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_pick - the dropper: adopt the colour under the press
; in:  [pt_ax],[pt_ay]; gfx lock held
; out: nothing; preserves all registers
;
; It reads the CANVAS, not the screen - which is the only reason it can be
; exact on a 1bpp adapter, where what is on the glass is a dither pattern and
; what the picture holds is a colour index (SPEC.md 39.4).
; -----------------------------------------------------------------------------
pt_pick:
    push ax
    push cx
    push dx
    mov cx, [pt_ax]
    mov dx, [pt_ay]
    call pt_getpx                   ; AL = colour
    mov [pt_col], al
    call pt_draw_strip
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_stroke - freehand drawing: dab, then follow the pointer until release
; in:  [pt_ax],[pt_ay] = the press point, [pt_ink] = colour; gfx lock held
; out: nothing; preserves all registers
;
; The first dab is placed at the press point rather than at the current mouse
; position: the event was queued (SPEC.md 10), so by the time this runs the
; pointer may already have moved, and a click that draws where the pointer
; ended up instead of where it was pressed feels broken.
; -----------------------------------------------------------------------------
pt_stroke:
    push ax
    push cx
    push dx
    call pt_undo_new
    call pt_brush                   ; AX = brush width
    mov cx, ax
    shr ax, 1
    mov [pt_blo], ax                ; the dab spans x-lo .. x+hi
    dec cx
    sub cx, ax
    mov [pt_bhi], cx
    mov ax, [pt_ax]
    mov [pt_wx], ax
    mov ax, [pt_ay]
    mov [pt_wy], ax
    call pt_dab
.loop:
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .out
    sub cx, [pt_cx0]
    sub dx, [pt_cy0]
    cmp cx, [pt_wx]
    jne .move
    cmp dx, [pt_wy]
    je .idle
.move:
    mov [pt_tox], cx
    mov [pt_toy], dx
    call pt_seg                     ; walks [pt_wx],[pt_wy] to the new point
    call pt_wait
    jmp short .loop
.idle:
    call pt_wait_tick
    jmp short .loop
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_dab - the whole brush square at [pt_wx],[pt_wy]
; in:  [pt_wx],[pt_wy],[pt_blo],[pt_bhi], [pt_col]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_dab:
    push ax
    mov ax, [pt_wx]
    sub ax, [pt_blo]
    mov [pt_rx1], ax
    mov ax, [pt_wx]
    add ax, [pt_bhi]
    mov [pt_rx2], ax
    mov ax, [pt_wy]
    sub ax, [pt_blo]
    mov [pt_ry1], ax
    mov ax, [pt_wy]
    add ax, [pt_bhi]
    mov [pt_ry2], ax
    call pt_rect
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_seg - drag the brush from [pt_wx],[pt_wy] to [pt_tox],[pt_toy]
; in:  as above, plus [pt_blo]/[pt_bhi] and [pt_col]; gfx lock held
; out: [pt_wx],[pt_wy] = the destination; preserves all registers
;
; THE fast path of the whole program. A square brush swept one pixel along
; either axis uncovers exactly one new edge of itself - the leading column
; when x moves, the leading row when y moves - so a step costs one or two
; gfx_fill calls of brush-width size and writes every new pixel exactly once.
; Re-dabbing the full square at each step would write width*width pixels
; instead, which at the eraser's 32px is a thousand-fold waste and visibly
; slower than the mouse.
; -----------------------------------------------------------------------------
pt_seg:
    push ax
    push bx
    push cx
    push dx
    mov word [pt_sx], 1
    mov ax, [pt_tox]
    sub ax, [pt_wx]
    jns .dxpos
    neg ax
    mov word [pt_sx], -1
.dxpos:
    mov [pt_ddx], ax
    mov word [pt_sy], 1
    mov ax, [pt_toy]
    sub ax, [pt_wy]
    jns .dypos
    neg ax
    mov word [pt_sy], -1
.dypos:
    mov [pt_ddy], ax
    mov cx, [pt_ddx]                ; CX = steps = max(|dx|,|dy|)
    cmp cx, ax
    jae .have
    mov cx, ax
.have:
    or cx, cx
    jz .out
    mov ax, cx
    shr ax, 1
    mov [pt_err], ax
    mov bx, [pt_ddx]
    cmp bx, [pt_ddy]
    jb .ymajor
; --- x is the major axis --------------------------------------------------
.xstep:
    mov ax, [pt_wx]
    add ax, [pt_sx]
    mov [pt_wx], ax
    mov ax, [pt_err]
    add ax, [pt_ddy]
    cmp ax, cx
    jb .xnoy
    sub ax, cx
    mov [pt_err], ax
    mov ax, [pt_wy]
    add ax, [pt_sy]
    mov [pt_wy], ax
    call pt_bar_y                   ; y moved too: the leading row
    call pt_bar_x
    loop .xstep
    jmp short .out
.xnoy:
    mov [pt_err], ax
    call pt_bar_x
    loop .xstep
    jmp short .out
; --- y is the major axis --------------------------------------------------
.ymajor:
.ystep:
    mov ax, [pt_wy]
    add ax, [pt_sy]
    mov [pt_wy], ax
    mov ax, [pt_err]
    add ax, [pt_ddx]
    cmp ax, cx
    jb .ynox
    sub ax, cx
    mov [pt_err], ax
    mov ax, [pt_wx]
    add ax, [pt_sx]
    mov [pt_wx], ax
    call pt_bar_x
    call pt_bar_y
    loop .ystep
    jmp short .out
.ynox:
    mov [pt_err], ax
    call pt_bar_y
    loop .ystep
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_bar_x - the brush's leading COLUMN after a step in x
; pt_bar_y - the brush's leading ROW after a step in y
; in:  [pt_wx],[pt_wy],[pt_sx]/[pt_sy],[pt_blo],[pt_bhi]; gfx lock held
; out: nothing; preserves all registers (CX carries pt_seg's step count)
pt_bar_x:
    push ax
    mov ax, [pt_wx]
    cmp word [pt_sx], 0
    jl .left
    add ax, [pt_bhi]
    jmp short .have
.left:
    sub ax, [pt_blo]
.have:
    mov [pt_rx1], ax
    mov [pt_rx2], ax
    mov ax, [pt_wy]
    sub ax, [pt_blo]
    mov [pt_ry1], ax
    mov ax, [pt_wy]
    add ax, [pt_bhi]
    mov [pt_ry2], ax
    call pt_rect
    pop ax
    ret

pt_bar_y:
    push ax
    mov ax, [pt_wy]
    cmp word [pt_sy], 0
    jl .up
    add ax, [pt_bhi]
    jmp short .have
.up:
    sub ax, [pt_blo]
.have:
    mov [pt_ry1], ax
    mov [pt_ry2], ax
    mov ax, [pt_wx]
    sub ax, [pt_blo]
    mov [pt_rx1], ax
    mov ax, [pt_wx]
    add ax, [pt_bhi]
    mov [pt_rx2], ax
    call pt_rect
    pop ax
    ret

; =============================================================================
; The rubber band - shared by the rectangle, the ellipse and the marquee
; =============================================================================

; -----------------------------------------------------------------------------
; pt_clampx / pt_clampy - hold a coordinate inside the canvas
; in:  AX; out: AX clamped; preserves everything else
; -----------------------------------------------------------------------------
pt_clampx:
    or ax, ax
    jns .hi
    xor ax, ax
    ret
.hi:
    cmp ax, PT_CW
    jl .out
    mov ax, PT_CW - 1
.out:
    ret

pt_clampy:
    or ax, ax
    jns .hi
    xor ax, ax
    ret
.hi:
    cmp ax, [pt_ch]
    jl .out
    mov ax, [pt_ch]
    dec ax
.out:
    ret

; -----------------------------------------------------------------------------
; pt_rubber - drag out a rectangle, XOR outline following the pointer
; in:  [pt_ax],[pt_ay] = the press point; gfx lock held
; out: [pt_shx1],[pt_shy1],[pt_shx2],[pt_shy2] = the normalised result, always
;      inside the canvas; preserves all registers
;
; The outline is drawn UNDER the lock and left up while the lock is released
; (pt_wait_tick), which is the opposite of ui_drag's ordering and deliberate:
; ui_drag's XOR goes straight to VRAM and is visible immediately, while a
; package's OSAPI_GFX_XOR_RECT goes through the back buffer when double
; buffering is armed (SPEC.md 32) and only reaches the glass at gfx_unlock's
; flush. The window that ui_drag protects - "nothing may touch the covered
; pixels between draw and erase" - is safe here for a different reason: this
; window is frontmost while it tracks, and every background painter in the
; tree checks wm_obscured before drawing (SPEC.md 7).
; -----------------------------------------------------------------------------
pt_rubber:
    push ax
    push cx
    push dx
    mov ax, [pt_ax]
    call pt_clampx
    mov [pt_anchx], ax
    mov [pt_curx], ax
    mov ax, [pt_ay]
    call pt_clampy
    mov [pt_anchy], ax
    mov [pt_cury], ax
    call pt_rb_norm
    call pt_rb_xor
.loop:
    call pt_wait_tick               ; the outline is up and the lock is down
    call pt_rb_xor                  ; erase before anything else moves
    call OSAPI_MOUSE
    test al, 1
    jz .out
    sub cx, [pt_cx0]
    sub dx, [pt_cy0]
    mov ax, cx
    call pt_clampx
    mov [pt_curx], ax
    mov ax, dx
    call pt_clampy
    mov [pt_cury], ax
    call pt_rb_norm
    call pt_rb_xor
    jmp short .loop
.out:
    pop dx
    pop cx
    pop ax
    ret

; pt_rb_norm - order the anchor and the current point into [pt_shx1..pt_shy2]
; out: nothing; preserves all registers
pt_rb_norm:
    push ax
    push bx
    mov ax, [pt_anchx]
    mov bx, [pt_curx]
    cmp ax, bx
    jle .xok
    xchg ax, bx
.xok:
    mov [pt_shx1], ax
    mov [pt_shx2], bx
    mov ax, [pt_anchy]
    mov bx, [pt_cury]
    cmp ax, bx
    jle .yok
    xchg ax, bx
.yok:
    mov [pt_shy1], ax
    mov [pt_shy2], bx
    pop bx
    pop ax
    ret

; pt_rb_xor - XOR the 1px outline of [pt_shx1..pt_shy2] on screen
; out: nothing; preserves all registers
pt_rb_xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_shx1]
    add ax, [pt_cx0]
    mov bx, [pt_shy1]
    add bx, [pt_cy0]
    mov cx, [pt_shx2]
    add cx, [pt_cx0]
    mov dx, [pt_shy2]
    add dx, [pt_cy0]
    call OSAPI_GFX_XOR_RECT
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_shape_commit - draw the shape the rubber band settled on
; in:  [pt_shx1..pt_shy2], [pt_tool], [pt_filled], [pt_col]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_shape_commit:
    push ax
    call pt_undo_new
    mov al, [pt_col]
    mov [pt_ink], al
    cmp byte [pt_tool], PT_T_OVAL
    je .oval
    cmp byte [pt_filled], 0
    je .frame
    mov ax, [pt_shx1]
    mov [pt_rx1], ax
    mov ax, [pt_shy1]
    mov [pt_ry1], ax
    mov ax, [pt_shx2]
    mov [pt_rx2], ax
    mov ax, [pt_shy2]
    mov [pt_ry2], ax
    call pt_rect
    jmp .out
.frame:
    call pt_penw                    ; CX = the line width the strip shows
    mov ax, [pt_shx1]               ; four bars CX thick, drawn INSIDE the
    mov [pt_rx1], ax                ; box, so the corners meet and the shape
    mov ax, [pt_shx2]               ; never grows past the rubber band
    mov [pt_rx2], ax
    mov ax, [pt_shy1]
    mov [pt_ry1], ax
    add ax, cx
    dec ax
    cmp ax, [pt_shy2]               ; a bar thicker than the box is the box
    jle .t_ok
    mov ax, [pt_shy2]
.t_ok:
    mov [pt_ry2], ax
    call pt_rect
    mov ax, [pt_shy2]
    mov [pt_ry2], ax
    sub ax, cx
    inc ax
    cmp ax, [pt_shy1]
    jge .b_ok
    mov ax, [pt_shy1]
.b_ok:
    mov [pt_ry1], ax
    call pt_rect
    mov ax, [pt_shy1]
    mov [pt_ry1], ax
    mov ax, [pt_shy2]
    mov [pt_ry2], ax
    mov ax, [pt_shx1]
    mov [pt_rx1], ax
    add ax, cx
    dec ax
    cmp ax, [pt_shx2]
    jle .l2_ok
    mov ax, [pt_shx2]
.l2_ok:
    mov [pt_rx2], ax
    call pt_rect
    mov ax, [pt_shx2]
    mov [pt_rx2], ax
    sub ax, cx
    inc ax
    cmp ax, [pt_shx1]
    jge .r2_ok
    mov ax, [pt_shx1]
.r2_ok:
    mov [pt_rx1], ax
    call pt_rect
    jmp short .out
.oval:
    call pt_oval
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_oval - an ellipse inscribed in [pt_shx1..pt_shy2], filled or outlined
; in:  as pt_shape_commit; gfx lock held
; out: nothing; preserves all registers
;
; Row by row from the half-width w = a*sqrt(b^2-dy^2)/b rather than by a
; midpoint iteration, because the midpoint form's decision variable needs
; 32-bit arithmetic at these radii (4*a^2*(1-y) reaches 28 million) while
; this needs none: b^2-dy^2 is at most 19,600, and a*s at most 31,360.
;
; The two-centre trick keeps the shape exactly inside its box whether the box
; is odd or even: rows come off yc1 going up and yc2 going down, columns off
; xc1 going left and xc2 going right, and the pair differ by one pixel when
; the box has an even side.
; -----------------------------------------------------------------------------
pt_oval:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_shx2]
    sub ax, [pt_shx1]
    shr ax, 1
    mov [pt_oa], ax                 ; a = semi-axis in x
    mov bx, [pt_shx1]
    add bx, ax
    mov [pt_oxc1], bx
    mov bx, [pt_shx2]
    sub bx, ax
    mov [pt_oxc2], bx
    mov ax, [pt_shy2]
    sub ax, [pt_shy1]
    shr ax, 1
    mov [pt_ob], ax                 ; b = semi-axis in y
    mov bx, [pt_shy1]
    add bx, ax
    mov [pt_oyc1], bx
    mov bx, [pt_shy2]
    sub bx, ax
    mov [pt_oyc2], bx
    mov ax, [pt_ob]
    mul ax
    mov [pt_ob2], ax                ; b^2 (b <= 140, so no overflow)
    mov ax, [pt_ob]
    mov [pt_ody], ax                ; dy counts down from b to 0
    mov word [pt_owp], -1           ; no previous row yet
.row:
    ; --- w for this row ---------------------------------------------------
    cmp word [pt_ob], 0
    jne .curve
    mov ax, [pt_oa]                 ; a box one row tall is just its width
    jmp short .havew
.curve:
    mov ax, [pt_ody]
    mul ax
    mov bx, [pt_ob2]
    sub bx, ax
    mov ax, bx
    call pt_isqrt                   ; AX = sqrt(b^2 - dy^2)
    mul word [pt_oa]                ; AX = a*sqrt(...) (< 32K)
    mov bx, [pt_ob]
    mov cx, bx
    shr cx, 1
    add ax, cx                      ; round rather than truncate
    xor dx, dx
    div bx
.havew:
    mov [pt_ow], ax
    ; --- the two rows this dy names ---------------------------------------
    mov ax, [pt_oyc1]
    sub ax, [pt_ody]
    mov [pt_or1], ax
    mov ax, [pt_oyc2]
    add ax, [pt_ody]
    mov [pt_or2], ax
    cmp byte [pt_filled], 0
    jne .fill
    call pt_penw                    ; CX = line width (1 for a hairline)
    mov ax, [pt_ob]
    sub ax, cx
    cmp [pt_ody], ax
    jg .fill                        ; inside the cap: the whole span, so the
                                    ; top and bottom of the curve are as thick
                                    ; as its sides
    ; --- outline: the two shoulders between this row and the last one, each
    ; --- widened inwards to the line width --------------------------------
    mov ax, [pt_oxc1]
    sub ax, [pt_ow]
    mov [pt_rx1], ax
    mov ax, [pt_oxc1]
    sub ax, [pt_owp]
    add ax, cx
    dec ax
    cmp ax, [pt_oxc1]
    jle .l_ok
    mov ax, [pt_oxc1]               ; never past the middle
.l_ok:
    mov [pt_rx2], ax
    call pt_orow
    mov ax, [pt_oxc2]
    add ax, [pt_owp]
    sub ax, cx
    inc ax
    cmp ax, [pt_oxc2]
    jge .r_ok
    mov ax, [pt_oxc2]
.r_ok:
    mov [pt_rx1], ax
    mov ax, [pt_oxc2]
    add ax, [pt_ow]
    mov [pt_rx2], ax
    call pt_orow
    jmp short .next
.fill:
    mov ax, [pt_oxc1]
    sub ax, [pt_ow]
    mov [pt_rx1], ax
    mov ax, [pt_oxc2]
    add ax, [pt_ow]
    mov [pt_rx2], ax
    call pt_orow
.next:
    mov ax, [pt_ow]
    mov [pt_owp], ax
    dec word [pt_ody]
    jns .row
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_orow - emit [pt_rx1..pt_rx2] on rows [pt_or1] and [pt_or2]
; in:  as above; gfx lock held; out: nothing; preserves all registers
pt_orow:
    push ax
    mov ax, [pt_or1]
    mov [pt_ry1], ax
    mov [pt_ry2], ax
    call pt_rect
    mov ax, [pt_or2]
    cmp ax, [pt_or1]
    je .out                         ; a one-row box: do not draw it twice
    mov [pt_ry1], ax
    mov [pt_ry2], ax
    call pt_rect
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_isqrt - floor(sqrt(AX))
; in:  AX; out: AX; clobbers BX, CX, DX
; -----------------------------------------------------------------------------
pt_isqrt:
    xor bx, bx                      ; the result so far
    mov cx, 0x4000                  ; the bit under test
.loop:
    mov dx, bx
    add dx, cx
    cmp ax, dx
    jb .down
    sub ax, dx
    shr bx, 1
    add bx, cx
    jmp short .next
.down:
    shr bx, 1
.next:
    shr cx, 1
    shr cx, 1                       ; two single-bit shifts: the 8086 has no
    jnz .loop                       ; shift-by-immediate beyond one
    mov ax, bx
    ret

; =============================================================================
; The selection
; =============================================================================

; pt_sel_take - adopt the rubber band's rectangle as the selection
pt_sel_take:
    push ax
    mov ax, [pt_shx1]
    mov [pt_selx1], ax
    mov ax, [pt_shy1]
    mov [pt_sely1], ax
    mov ax, [pt_shx2]
    mov [pt_selx2], ax
    mov ax, [pt_shy2]
    mov [pt_sely2], ax
    mov byte [pt_selon], 1
    mov byte [pt_selshown], 0
    call pt_marq
    pop ax
    ret

; pt_sel_drop - erase the marquee and forget the selection
pt_sel_drop:
    call pt_marq_hide
    mov byte [pt_selon], 0
    ret

; pt_marq / pt_marq_hide - show / hide the marquee (XOR, screen only)
; out: nothing; preserves all registers
pt_marq:
    cmp byte [pt_selon], 0
    je .out
    cmp byte [pt_selshown], 0
    jne .out
    mov byte [pt_selshown], 1
    call pt_marq_xor
.out:
    ret

pt_marq_hide:
    cmp byte [pt_selon], 0
    je .out
    cmp byte [pt_selshown], 0
    je .out
    mov byte [pt_selshown], 0
    call pt_marq_xor
.out:
    ret

pt_marq_xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_selx1]
    add ax, [pt_cx0]
    mov bx, [pt_sely1]
    add bx, [pt_cy0]
    mov cx, [pt_selx2]
    add cx, [pt_cx0]
    mov dx, [pt_sely2]
    add dx, [pt_cy0]
    call OSAPI_GFX_XOR_RECT
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_sel_rect - copy the selection into the pending-rectangle words
; out: CF=1 when there is no selection; preserves all registers
pt_sel_rect:
    push ax
    cmp byte [pt_selon], 0
    je .none
    mov ax, [pt_selx1]
    mov [pt_rx1], ax
    mov ax, [pt_sely1]
    mov [pt_ry1], ax
    mov ax, [pt_selx2]
    mov [pt_rx2], ax
    mov ax, [pt_sely2]
    mov [pt_ry2], ax
    pop ax
    clc
    ret
.none:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_sel_clear - white out the selection (Delete, and Edit > Clear)
; in:  gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_sel_clear:
    push ax
    call pt_sel_rect
    jc .out
    call pt_marq_hide
    call pt_undo_new
    mov byte [pt_ink], CWHITE
    call pt_rect
    call pt_marq
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_copy - the selection into the clipboard, packed 4bpp, top-down
; in:  gfx lock held; out: nothing; preserves all registers
;
; Layout: width, height, then ceil(w/2) bytes per row, each row starting on a
; HIGH nibble whatever the selection's own parity is - so a paste is a
; straight unpack and never a shifted one. 4bpp keeps the worst case (the
; whole canvas) at 62,724 bytes, inside the one segment the clipboard owns.
; -----------------------------------------------------------------------------
pt_copy:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [pt_selon], 0
    je .out
    mov ax, [pt_selx2]
    sub ax, [pt_selx1]
    inc ax
    mov [pt_cbw], ax
    mov ax, [pt_sely2]
    sub ax, [pt_sely1]
    inc ax
    mov [pt_cbh], ax
    mov ax, PT_CBSEG
    mov es, ax
    mov ax, [pt_cbw]
    mov [es:0], ax
    mov ax, [pt_cbh]
    mov [es:2], ax
    mov di, 4
    mov dx, [pt_sely1]              ; DX = source row
.row:
    xor si, si                      ; SI = column within the selection
.col:
    mov cx, [pt_selx1]
    add cx, si
    call pt_getpx                   ; AL = colour (clobbers AX only)
    test si, 1
    jnz .low
    shl al, 1                       ; even column: the high nibble, held back
    shl al, 1
    shl al, 1
    shl al, 1
    mov [pt_cbbyte], al
    jmp short .next
.low:
    or al, [pt_cbbyte]
    mov [es:di], al
    inc di
.next:
    inc si
    cmp si, [pt_cbw]
    jb .col
    test si, 1
    jz .rowend
    mov al, [pt_cbbyte]             ; an odd width ends on a half byte
    mov [es:di], al
    inc di
.rowend:
    inc dx
    cmp dx, [pt_sely2]
    jbe .row
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_paste - the clipboard back onto the canvas at the selection's corner
; in:  gfx lock held; out: nothing; preserves all registers
;
; Pixels go in with no screen call at all and ONE blit covers the lot: a gfx
; call per pasted pixel would cost hundreds of times the unpacking.
; -----------------------------------------------------------------------------
pt_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [pt_cbw], 0
    je .out
    call pt_marq_hide
    mov ax, [pt_selx1]              ; where: the selection's corner, or the
    mov bx, [pt_sely1]              ; canvas origin when there is none
    cmp byte [pt_selon], 0
    jne .have
    xor ax, ax
    xor bx, bx
.have:
    mov [pt_pdx], ax
    mov [pt_pdy], bx
    call pt_undo_new
    ; every row we are about to touch, into the undo image, before any write
    mov ax, [pt_pdy]
    call pt_clampy
    mov [pt_ptmp], ax
    mov ax, [pt_pdy]
    add ax, [pt_cbh]
    dec ax
    call pt_clampy
    mov dx, ax
    mov ax, [pt_ptmp]
    call pt_umark
    mov ax, [pt_cbw]                ; bytes per clipboard row
    inc ax
    shr ax, 1
    mov [pt_pstr], ax
    mov word [pt_pbase], 4
    mov ax, PT_CBSEG
    mov es, ax
    xor dx, dx                      ; DX = clipboard row
.row:
    xor si, si                      ; SI = clipboard column
.col:
    mov bx, si
    shr bx, 1
    add bx, [pt_pbase]
    mov al, [es:bx]
    test si, 1
    jz .high
    and al, 0x0F
    jmp short .put
.high:
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.put:
    mov cx, si
    add cx, [pt_pdx]
    push dx
    add dx, [pt_pdy]
    call pt_setpx                   ; canvas write at (pdx+col, pdy+row)
    pop dx
    inc si
    cmp si, [pt_cbw]
    jb .col
    mov ax, [pt_pstr]
    add [pt_pbase], ax
    inc dx
    cmp dx, [pt_cbh]
    jb .row
    ; --- one blit over the pasted rectangle --------------------------------
    mov ax, [pt_pdx]
    mov [pt_rx1], ax
    add ax, [pt_cbw]
    dec ax
    mov [pt_rx2], ax
    mov ax, [pt_pdy]
    mov [pt_ry1], ax
    add ax, [pt_cbh]
    dec ax
    mov [pt_ry2], ax
    call pt_blit
    call pt_marq
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_setpx - write one canvas pixel: clipped, no undo, no screen
; in:  AL = colour, CX = x, DX = y (canvas coords, signed)
; out: nothing; preserves all registers (ES included)
;
; The paste and the text-cell restore both go through here; neither wants a
; gfx call per pixel and neither wants an undo snapshot per pixel.
; -----------------------------------------------------------------------------
pt_setpx:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    or dx, dx
    js .out
    cmp dx, [pt_ch]
    jge .out
    or cx, cx
    js .out
    cmp cx, PT_CW
    jge .out
    mov bx, PT_CVSEG
    mov es, bx
    mov bx, dx
    add bx, bx
    mov di, [pt_rowtab+bx]
    mov bx, cx
    shr bx, 1
    add di, bx
    mov ah, [es:di]
    test cl, 1
    jnz .low
    and ah, 0x0F
    mov cl, 4
    shl al, cl
    or al, ah
    mov [es:di], al
    jmp short .out
.low:
    and ah, 0xF0
    or al, ah
    mov [es:di], al
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_upix - one pixel of the UNDO image (the picture as it was when this
;           operation began)
; in:  CX = x, DX = y (inside the canvas)
; out: AL = colour; clobbers AX and flags only
; -----------------------------------------------------------------------------
pt_upix:
    push bx
    push cx
    push es
    mov bx, PT_UNSEG
    mov es, bx
    mov bx, dx
    add bx, bx
    mov bx, [pt_rowtab+bx]
    mov ax, cx
    shr ax, 1
    add bx, ax
    mov al, [es:bx]
    test cl, 1
    jnz .lo
    mov cl, 4
    shr al, cl
.lo:
    and al, 0x0F
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_urestore - put a rectangle back the way this operation found it
; in:  [pt_rx1]..[pt_ry2] = canvas rect; gfx lock held
; out: canvas and screen restored; preserves all registers
;
; This is what makes the text tool's backspace honest. Filling the cell with
; white would erase whatever the text was typed ON TOP of; the undo image
; already holds those pixels, because pt_type marks the cell's rows before it
; draws (blank cells included), so the cell can simply be copied back.
; -----------------------------------------------------------------------------
pt_urestore:
    push ax
    push bx
    push cx
    push dx
    push si
    call pt_clip
    jc .out
    mov si, [pt_cy1]
.row:
    mov cx, [pt_cx1]
.col:
    mov dx, si
    call pt_upix                    ; AL = the pixel as it was
    mov dx, si
    call pt_setpx
    inc cx
    cmp cx, [pt_cx2]
    jbe .col
    inc si
    cmp si, [pt_cy2]
    jbe .row
    call pt_blit                    ; the clipped rect is still in place
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The flood fill
;
; Scanline seed fill (Smith's algorithm) with an explicit span stack in
; PT_SCSEG: 1,024 entries of (row, x1, x2, direction). Recursion is out of the
; question - a package's stack is 1,536 bytes in LOW_SEG (SPEC.md 20.6) - and
; a per-pixel stack would need four bytes for every pixel of the region.
;
; Each run is emitted to the screen the moment it is found, so the fill draws
; progressively instead of after a silent pause, and the emit costs exactly
; one gfx_hline per run because the runs ARE the changed pixels.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_flood - fill from [pt_ax],[pt_ay] until the colour under it changes
; in:  [pt_ax],[pt_ay] = the seed, [pt_col] = the new colour; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_flood:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov cx, [pt_ax]
    mov dx, [pt_ay]
    call pt_getpx
    mov [pt_fold], al
    cmp al, [pt_col]
    je .out                         ; already that colour: nothing to do
    call pt_undo_new
    mov al, [pt_col]
    mov [pt_ink], al
    mov word [pt_fsp], 0
    mov byte [pt_fovf], 0
    mov ax, [pt_ay]                 ; examine the seed row downward...
    mov bx, [pt_ax]
    mov cx, bx
    mov dx, 1
    call pt_fpush
    mov ax, [pt_ay]                 ; ...and the row above it upward, which is
    dec ax                          ; what makes the seed row's own run
    mov bx, [pt_ax]                 ; propagate in both directions
    mov cx, bx
    mov dx, -1
    call pt_fpush
.pop:
    call pt_fpop                    ; AX = row, BX = x1, CX = x2, DX = dir
    jc .out
    mov [pt_fy], ax
    mov [pt_fx1], bx
    mov [pt_fx2], cx
    mov [pt_fdy], dx
    call pt_frow
    jmp short .pop
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_frow - examine one row for runs of [pt_fold] overlapping [x1,x2]
; in:  [pt_fy],[pt_fx1],[pt_fx2],[pt_fdy]; gfx lock held
; out: nothing; preserves all registers
;
; Every run found is filled and then pushes its own continuation one row on in
; the same direction, plus a shoulder back the other way for any part of it
; that reaches outside the parent's span - which is how the fill gets around
; obstacles without ever revisiting a filled pixel (a filled pixel is no
; longer [pt_fold], so a redundant pop finds nothing and pushes nothing).
; -----------------------------------------------------------------------------
pt_frow:
    push ax
    push bx
    push cx
    push dx
    push bp
    push es
    mov ax, PT_CVSEG
    mov es, ax
    mov bx, [pt_fy]
    add bx, bx
    mov bp, [pt_rowtab+bx]          ; BP = the row's byte offset
    mov ax, [pt_fx1]
    mov [pt_fx], ax
.scan:
    mov ax, [pt_fx]
    cmp ax, [pt_fx2]
    jg .out
    call pt_fpix                    ; AL = the pixel there
    cmp al, [pt_fold]
    je .found
    inc word [pt_fx]
    jmp short .scan
.found:
    mov ax, [pt_fx]                 ; --- grow left ---
    mov [pt_fl], ax
.left:
    cmp word [pt_fl], 0
    je .lend
    mov ax, [pt_fl]
    dec ax
    call pt_fpix
    cmp al, [pt_fold]
    jne .lend
    dec word [pt_fl]
    jmp short .left
.lend:
    mov ax, [pt_fx]                 ; --- grow right ---
    mov [pt_fr], ax
.right:
    cmp word [pt_fr], PT_CW - 1
    jae .rend
    mov ax, [pt_fr]
    inc ax
    call pt_fpix
    cmp al, [pt_fold]
    jne .rend
    inc word [pt_fr]
    jmp short .right
.rend:
    mov ax, [pt_fl]                 ; --- fill it, canvas and screen ---
    mov [pt_rx1], ax
    mov ax, [pt_fr]
    mov [pt_rx2], ax
    mov ax, [pt_fy]
    mov [pt_ry1], ax
    mov [pt_ry2], ax
    call pt_rect
    mov ax, PT_CVSEG                ; pt_rect left ES alone, but be explicit
    mov es, ax
    mov ax, [pt_fy]                 ; --- the run's continuation ---
    add ax, [pt_fdy]
    mov bx, [pt_fl]
    mov cx, [pt_fr]
    mov dx, [pt_fdy]
    call pt_fpush
    mov ax, [pt_fl]                 ; --- the left shoulder, going back ---
    cmp ax, [pt_fx1]
    jge .rsh
    mov ax, [pt_fy]
    sub ax, [pt_fdy]
    mov bx, [pt_fl]
    mov cx, [pt_fx1]
    dec cx
    mov dx, [pt_fdy]
    neg dx
    call pt_fpush
.rsh:
    mov ax, [pt_fr]                 ; --- and the right shoulder ---
    cmp ax, [pt_fx2]
    jle .adv
    mov ax, [pt_fy]
    sub ax, [pt_fdy]
    mov bx, [pt_fx2]
    inc bx
    mov cx, [pt_fr]
    mov dx, [pt_fdy]
    neg dx
    call pt_fpush
.adv:
    mov ax, [pt_fr]
    inc ax
    mov [pt_fx], ax
    jmp .scan
.out:
    pop es
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_fpix - the pixel at column AX of the row BP addresses
; in:  AX = x, BP = row offset, ES = PT_CVSEG
; out: AL = colour; clobbers AX, BX, flags
pt_fpix:
    mov bx, ax
    shr bx, 1
    add bx, bp
    mov ah, [es:bx]
    test al, 1
    jnz .lo
    shr ah, 1                       ; even x: the high nibble
    shr ah, 1
    shr ah, 1
    shr ah, 1
.lo:
    mov al, ah
    and al, 0x0F
    ret

; pt_fpush - stack one span; silently drops rows off the canvas, and sets
;            [pt_fovf] instead of overflowing the stack
; in:  AX = row, BX = x1, CX = x2, DX = direction
; out: nothing; preserves all registers
pt_fpush:
    push ax
    push si
    push es
    or ax, ax
    js .out
    cmp ax, [pt_ch]
    jge .out
    cmp bx, cx
    jg .out
    mov si, [pt_fsp]
    cmp si, PT_FSTK_MAX
    jae .ovf
    inc word [pt_fsp]
    shl si, 1
    shl si, 1
    shl si, 1                       ; eight bytes an entry
    add si, PT_SC_STACK
    push ax
    mov ax, PT_SCSEG
    mov es, ax
    pop ax
    mov [es:si], ax
    mov [es:si+2], bx
    mov [es:si+4], cx
    mov [es:si+6], dx
.out:
    pop es
    pop si
    pop ax
    ret
.ovf:
    mov byte [pt_fovf], 1           ; the region is more complex than the
    jmp short .out                  ; stack: the fill stops short rather than
                                    ; scribbling past it

; pt_fpop - unstack one span
; out: CF=1 empty, else AX = row, BX = x1, CX = x2, DX = direction
;      preserves SI/DI/BP/ES
pt_fpop:
    push si
    push es
    cmp word [pt_fsp], 0
    je .empty
    dec word [pt_fsp]
    mov si, [pt_fsp]
    shl si, 1
    shl si, 1
    shl si, 1
    add si, PT_SC_STACK
    mov ax, PT_SCSEG
    mov es, ax
    mov ax, [es:si]
    mov bx, [es:si+2]
    mov cx, [es:si+4]
    mov dx, [es:si+6]
    clc
    pop es
    pop si
    ret
.empty:
    stc
    pop es
    pop si
    ret

; =============================================================================
; The text tool
;
; Glyphs are written INTO the canvas, at 1x, 2x or 4x, from the ROM font
; pt_font_init copied - so text is pixels like everything else, and saving the
; picture saves the words in it. Set bits are emitted as scaled runs through
; pt_rect, one call per run rather than one per pixel.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_text_place - put the caret where the user pressed
; in:  [pt_ax],[pt_ay]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_text_place:
    push ax
    call pt_caret_hide
    call pt_undo_new                ; a text run is ONE undoable operation -
                                    ; and the generation's row snapshots are
                                    ; also what backspace restores from
    mov ax, [pt_ax]
    call pt_clampx
    mov [pt_txtx], ax
    mov [pt_txtx0], ax
    mov ax, [pt_ay]
    call pt_clampy
    mov [pt_txty], ax
    mov byte [pt_txton], 1
    call pt_caret_show
    pop ax
    ret

; pt_text_end - stop a text run (any other tool, Esc, or a repaint)
pt_text_end:
    call pt_caret_hide
    mov byte [pt_txton], 0
    ret

; -----------------------------------------------------------------------------
; pt_caret_show / pt_caret_hide - a 1px bar on the glass, never in the picture
; out: nothing; preserves all registers
;
; Screen-only, so hiding it is a blit of the column it covered. Putting the
; caret in the canvas would mean a stray black column in every saved file.
; -----------------------------------------------------------------------------
pt_caret_show:
    push ax
    push bx
    push cx
    push dx
    cmp byte [pt_txton], 0
    je .out
    cmp byte [pt_careton], 0
    jne .out
    mov ax, [pt_txtx]
    call pt_clampx
    mov [pt_carx], ax
    mov ax, [pt_txty]
    mov [pt_cary], ax
    call pt_fh                      ; AX = glyph height at this scale
    add ax, [pt_cary]
    dec ax
    call pt_clampy
    mov [pt_cary2], ax
    mov byte [pt_careton], 1
    mov al, [pt_col]
    mov [pt_pen], al
    mov ax, [pt_carx]
    mov bx, [pt_cary]
    mov cx, ax
    mov dx, [pt_cary2]
    call pt_sfill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_caret_hide:
    push ax
    cmp byte [pt_careton], 0
    je .out
    mov byte [pt_careton], 0
    mov ax, [pt_carx]
    mov [pt_rx1], ax
    mov [pt_rx2], ax
    mov ax, [pt_cary]
    mov [pt_ry1], ax
    mov ax, [pt_cary2]
    mov [pt_ry2], ax
    call pt_blit
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_setscale - set the text scale from a shift count
; in:  AL = 0, 1 or 2
; out: [pt_gsh] = AL and [pt_fscale] = 1/2/4; preserves all registers
;
; The two are one setting in two forms - the shift the glyph loop multiplies
; with, and the digit the strip prints - and they are written together here
; because keeping them apart is exactly how the strip button came to change
; the label without changing the text.
; -----------------------------------------------------------------------------
pt_setscale:
    push ax
    push cx
    mov [pt_gsh], al
    mov cl, al
    mov al, 1
    shl al, cl
    mov [pt_fscale], al
    pop cx
    pop ax
    ret

; pt_fh - the glyph box at the current scale
; out: AX = 8 << shift; preserves all other registers
pt_fh:
    push cx
    mov cl, [pt_gsh]
    mov ax, 8
    shl ax, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; pt_type - one printable character into the picture
; in:  AL = 32..126; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_type:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [pt_txton], 0
    je .out
    sub al, 32
    jb .out
    cmp al, 95
    jae .out
    mov [pt_gch], al
    call pt_caret_hide
    call pt_fh
    mov [pt_gbox], ax               ; the glyph's box, both ways
    mov ax, [pt_txtx]
    add ax, [pt_gbox]
    cmp ax, PT_CW
    jle .fits
    call pt_crlf                    ; no room on this line: wrap
.fits:
    mov ax, [pt_txty]
    add ax, [pt_gbox]
    cmp ax, [pt_ch]
    jg .done                        ; no room on the canvas at all: stop
    mov ax, [pt_txty]               ; snapshot the cell's rows even if the
    mov dx, ax                      ; glyph is a space: backspace restores
    add dx, [pt_gbox]               ; from the undo image, and an unmarked row
    dec dx                          ; there would hand back a stale one
    call pt_clampy
    push ax
    mov ax, dx
    call pt_clampy
    mov dx, ax
    pop ax
    call pt_umark
    mov al, [pt_col]
    mov [pt_ink], al
    xor bx, bx
    mov bl, [pt_gch]
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, pt_glyphs
    mov si, ax                      ; SI = the eight glyph rows
    xor di, di                      ; DI = row
.row:
    mov ah, [si]
    inc si
    mov word [pt_grs], -1
    xor bx, bx                      ; BX = column
    mov cx, 8
.bit:
    shl ah, 1
    jc .one
    cmp word [pt_grs], -1
    je .adv
    call pt_g_flush
    jmp short .adv
.one:
    cmp word [pt_grs], -1
    jne .adv
    mov [pt_grs], bx
.adv:
    inc bx
    loop .bit
    cmp word [pt_grs], -1
    je .next
    call pt_g_flush
.next:
    inc di
    cmp di, 8
    jb .row
    mov ax, [pt_txtx]
    add ax, [pt_gbox]
    mov [pt_txtx], ax
.done:
    call pt_caret_show
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_g_flush - the open run of glyph row DI, scaled, into the picture
; in:  [pt_grs] = first column, BX = the column that ended it, DI = glyph row
; out: [pt_grs] = -1; preserves all registers
pt_g_flush:
    push ax
    push bx
    push cx
    push dx
    mov cl, [pt_gsh]
    mov ax, [pt_grs]
    shl ax, cl
    add ax, [pt_txtx]
    mov [pt_rx1], ax
    mov ax, bx
    shl ax, cl
    add ax, [pt_txtx]
    dec ax
    mov [pt_rx2], ax
    mov ax, di
    shl ax, cl
    add ax, [pt_txty]
    mov [pt_ry1], ax
    mov dx, ax
    mov ax, 1
    shl ax, cl
    add dx, ax
    dec dx
    mov [pt_ry2], dx
    call pt_rect
    mov word [pt_grs], -1
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_crlf - carriage return + line feed for the text tool
; out: nothing; preserves all registers
pt_crlf:
    push ax
    call pt_caret_hide
    mov ax, [pt_txtx0]
    mov [pt_txtx], ax
    call pt_fh
    add ax, [pt_txty]
    mov [pt_txty], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_bs - backspace: step back one cell and white it out
; in:  gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_bs:
    push ax
    cmp byte [pt_txton], 0
    je .out
    call pt_caret_hide
    call pt_fh
    mov [pt_gbox], ax
    mov ax, [pt_txtx]
    sub ax, [pt_gbox]
    cmp ax, [pt_txtx0]
    jl .show                        ; already at the left margin
    mov [pt_txtx], ax
    mov [pt_rx1], ax
    add ax, [pt_gbox]
    dec ax
    mov [pt_rx2], ax
    mov ax, [pt_txty]
    mov [pt_ry1], ax
    add ax, [pt_gbox]
    dec ax
    mov [pt_ry2], ax
    call pt_urestore                ; the pixels the glyph covered, back - not
                                    ; a white hole punched through the picture
.show:
    call pt_caret_show
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_onkey - W_ONKEY: typing, and the two editing keys
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [pt_mode], PT_M_LIVE
    jne .out
    mov [pt_key], ax
    mov bx, si
    call pt_org
    call pt_msg_hide
    mov ax, [pt_key]
    cmp al, 27
    je .esc
    cmp al, 8
    je .back
    cmp ah, 0x53                    ; grey Delete
    je .del
    cmp al, 13
    je .enter
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call pt_type
    jmp short .out
.esc:
    call pt_text_end
    call pt_sel_drop
    jmp short .out
.enter:
    cmp byte [pt_txton], 0
    je .out
    call pt_crlf
    call pt_caret_show
    jmp short .out
.back:
    cmp byte [pt_txton], 0
    jne .bs
.del:
    call pt_sel_clear
    jmp short .out
.bs:
    call pt_bs
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Menus (SPEC.md 12.2)
;
; Three menus, and every item is something that has no natural place among the
; tool icons: whole-document commands, the clipboard, undo, and the two
; modifiers that also have buttons in the strip. Each handler repaints
; whatever it changed, because the kernel does not repaint after a command
; returns.
; =============================================================================

PT_MF_NEW    equ 0                  ; File
PT_MF_OPEN   equ 1
PT_MF_SAVE   equ 2
PT_MF_SAVEAS equ 3
PT_ME_UNDO   equ 0                  ; Edit
PT_ME_CUT    equ 1
PT_ME_COPY   equ 2
PT_ME_PASTE  equ 3
PT_ME_CLEAR  equ 4
PT_MD_FILL   equ 0                  ; Draw
PT_MD_F1     equ 1
PT_MD_F2     equ 2
PT_MD_F4     equ 3

; -----------------------------------------------------------------------------
; pt_oncmd - AM_ONCMD: run a menu item
; in:  AL = item index, AH = menu index, SI = window ptr, BX = the set;
;      gfx lock held by the caller, UI task
; out: nothing; clobbers AX/BX/CX/DX/DI/ES like any window callback
; -----------------------------------------------------------------------------
pt_oncmd:
    cmp byte [pt_mode], PT_M_LIVE
    jne .out
    mov [pt_key], ax                ; the (menu, item) pair, out of AX's way
    mov bx, si
    call pt_org
    call pt_msg_hide
    mov ax, [pt_key]
    cmp ah, 1
    je .edit
    cmp ah, 2
    je .draw
    or ah, ah
    jnz .out
; --- File ------------------------------------------------------------------
    cmp al, PT_MF_NEW
    je .new
    cmp al, PT_MF_OPEN
    je .open
    cmp al, PT_MF_SAVE
    je .save
    cmp al, PT_MF_SAVEAS
    je .saveas
.out:
    ret
.new:
    call pt_new
    ret
.save:
    cmp byte [pt_name], 0
    je .saveas                      ; never named: ask, do not guess
    mov si, pt_name
    call pt_save
    ret
.open:
    mov al, FDLG_OPEN
    jmp short .dlg
.saveas:
    mov al, FDLG_SAVE
.dlg:
    mov si, [pt_win]
    jmp pt_dlg                      ; tail call, and NO repaint after it: the
                                    ; dialog is on top of us now (SPEC.md 38)
; --- Edit ------------------------------------------------------------------
.edit:
    cmp al, PT_ME_UNDO
    je .undo
    cmp al, PT_ME_CUT
    je .cut
    cmp al, PT_ME_COPY
    je .copy
    cmp al, PT_ME_PASTE
    je .paste
    cmp al, PT_ME_CLEAR
    je .clear
    ret
.undo:
    call pt_marq_hide
    call pt_undo_swap
    call pt_marq
    ret
.cut:
    call pt_copy
    jmp pt_sel_clear
.copy:
    jmp pt_copy
.paste:
    jmp pt_paste
.clear:
    jmp pt_sel_clear
; --- Draw ------------------------------------------------------------------
.draw:
    cmp al, PT_MD_FILL
    jne .font
    xor byte [pt_filled], 1
    jmp pt_draw_strip
.font:
    cmp al, PT_MD_F4
    ja .out
    dec al                          ; item 1/2/3 -> shift 0/1/2 -> scale 1/2/4
    call pt_setscale
    jmp pt_draw_strip

; -----------------------------------------------------------------------------
; pt_new - a blank picture, undoably
; in:  gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_new:
    push ax
    push dx
    call pt_text_end
    call pt_sel_drop
    call pt_undo_new
    xor ax, ax                      ; the whole old picture into the undo
    mov dx, [pt_ch]                 ; image, so New itself can be undone
    dec dx
    call pt_umark
    mov al, CWHITE
    call pt_wipe
    call pt_blit_all
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_repaint - a self-initiated repaint of our whole content
; in:  gfx lock held; out: nothing; preserves all registers
;
; Used after the file dialog goes away: wm_destroy's repaint ran BEFORE the
; completion callback, so the picture on screen is the one from before the
; load (SPEC.md 38.6). Every part of this draws its own background, so no
; white fill is needed first.
; -----------------------------------------------------------------------------
pt_repaint:
    push ax
    push bx
    push cx
    push dx
    call pt_draw_pal
    call pt_draw_strip
    mov byte [pt_pen], CBLACK
    mov ax, PT_SEPX
    xor bx, bx
    mov cx, ax
    mov dx, [pt_ch]
    dec dx
    call pt_cfill
    call pt_blit_all
    mov byte [pt_selshown], 0
    mov byte [pt_msgon], 0
    mov byte [pt_careton], 0
    call pt_marq
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Files - the Standard File dialog, and the BMP reader and writer
;
; The writer is the whole reason the canvas is laid out the way it is: rows
; bottom-up behind a 118-byte DIB header means "save" is one OSAPI_FILE_WRITE
; of PT_CVSEG with no staging pass, and a full 448x280 picture is 62,838
; bytes - inside the file API's 64KB ceiling (SPEC.md 18.4) with room to
; spare. The reader needs a staging buffer for the file it is about to parse
; and uses the clipboard segment for it, which is why Open empties the
; clipboard.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_dlg - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_dlg:
    push bx
    push si
    push di
    mov bx, si
    mov di, pt_ondlg
    mov si, pt_name
    cmp byte [pt_name], 0
    jne .named
    mov si, pt_s_defname
.named:
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_ondlg - the dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, DI = the chosen name;
;      UI task, gfx lock HELD, the dialog window already destroyed
; out: nothing; no register need be preserved
; -----------------------------------------------------------------------------
pt_ondlg:
    mov [pt_dmode], al              ; to MEMORY, not to a register: the name
                                    ; copy below wants AL and pt_org wants BX,
                                    ; so the mode has to outlive both
    mov si, di
    mov di, pt_name
    mov cx, PT_NAMEMAX              ; bounded even though SPEC.md 38.6 promises
.copy:                              ; 12 or fewer: a package that trusts a
    mov al, [si]                    ; promise is a package with an overrun
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    mov bx, [pt_win]
    call pt_org                     ; the window moved while the dialog was up
    mov si, pt_name
    cmp byte [pt_dmode], FDLG_SAVE
    je .save
    call pt_load
    jmp short .draw
.save:
    call pt_save
.draw:
    call pt_repaint
    mov si, [pt_msgp]               ; the outcome, on top of the fresh picture
    or si, si
    jz .out
    call pt_msg_show
.out:
    ret

; -----------------------------------------------------------------------------
; pt_fmt - which format does this name ask for?
; in:  SI = NUL name
; out: AL = 0 BMP, 1 GIF, 2 JPEG; preserves all other registers
;
; The extension decides on the way out. On the way in the file's own first
; bytes decide instead - a name is a wish, a magic number is a fact.
; -----------------------------------------------------------------------------
pt_fmt:
    push bx
    push si
    xor bx, bx                      ; BX = the last '.' seen + 1
.find:
    mov al, [si]
    or al, al
    jz .end
    inc si
    cmp al, '.'
    jne .find
    mov bx, si
    jmp short .find
.end:
    xor al, al
    or bx, bx
    jz .out
    mov si, bx
    call pt_upcase3                 ; AX = 3 upper-cased chars, AL first
    cmp al, 'G'
    jne .jpg
    cmp ah, 'I'
    jne .out
    mov al, 1
    jmp short .out
.jpg:
    cmp al, 'J'
    jne .bmp
    mov al, 2
    jmp short .out
.bmp:
    xor al, al
.out:
    pop si
    pop bx
    ret

; pt_upcase3 - the first two extension characters, upper-cased
; in:  SI = extension; out: AL, AH; clobbers flags
pt_upcase3:
    mov al, [si]
    mov ah, [si+1]
    cmp al, 'a'
    jb .a
    cmp al, 'z'
    ja .a
    sub al, 32
.a:
    cmp ah, 'a'
    jb .b
    cmp ah, 'z'
    ja .b
    sub ah, 32
.b:
    ret

; -----------------------------------------------------------------------------
; pt_save - write the picture out under the name in SI
; in:  SI = NUL 8.3 name; UI task, gfx lock held
; out: [pt_msgp] = the toast to show; preserves all registers
; -----------------------------------------------------------------------------
pt_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov word [pt_msgp], pt_s_wrote
    call pt_fmt
    or al, al
    jz .bmp
    mov word [pt_msgp], pt_s_nofmt  ; GIF and JPEG: see docs/PAINT-NOTES.md
    jmp short .out
.bmp:
    call pt_bmp_hdr                 ; re-stamp it: [pt_ch] is the truth
    mov ax, PT_CVSEG
    mov es, ax
    xor bx, bx                      ; ES:BX = the DIB, header and all
    mov ax, [pt_ch]
    mov cx, PT_STRIDE
    mul cx
    add ax, PT_BMPHDR
    mov cx, ax
    call OSAPI_FILE_WRITE           ; SI is still the name
    jnc .out
    call pt_ferr
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_load - read a picture in under the name in SI
; in:  SI = NUL 8.3 name; UI task, gfx lock held
; out: [pt_msgp] = the toast; the canvas replaced on success;
;      preserves all registers
; -----------------------------------------------------------------------------
pt_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [pt_msgp], pt_s_opened
    mov ax, PT_CBSEG
    mov es, ax
    xor bx, bx
    mov cx, 0xFFFF                  ; the staging segment holds any legal file
    call OSAPI_FILE_READ            ; (the API caps a file at 64KB anyway)
    jnc .got
    call pt_ferr
    jmp short .out
.got:
    mov word [pt_cbw], 0            ; the clipboard IS the staging buffer
    cmp ax, 64
    jb .bad
    cmp word [es:0], 0x4D42         ; 'BM'
    je .bmp
    cmp word [es:0], 0x4947         ; 'GI'
    je .nofmt
    cmp word [es:0], 0xD8FF         ; JPEG SOI
    je .nofmt
.bad:
    mov word [pt_msgp], pt_s_badpic
    jmp short .out
.nofmt:
    mov word [pt_msgp], pt_s_nofmt
    jmp short .out
.bmp:
    call pt_bmp_in                  ; AX = the byte count read
    jnc .out
    mov [pt_msgp], si               ; SI = why not
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_ferr - a FERR_* code in AX into [pt_msgp]
; out: nothing; preserves all registers
pt_ferr:
    push ax
    push bx
    cmp ax, 10
    jbe .known
    mov ax, 2                       ; anything unexpected reads as an I/O error
.known:
    mov bx, ax
    add bx, bx
    mov ax, [pt_ferrtab+bx]
    mov [pt_msgp], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_bmp_in - decode the staged BMP into the canvas
; in:  ES = PT_CBSEG, AX = bytes read; gfx lock held
; out: CF=0 done, CF=1 with SI = the reason; preserves all other registers
;
; Everything about the header is checked before anything is believed: the
; magic, both halves of every dword that must be zero, the bit depth, the
; compression, and above all that the pixel data the header describes fits
; inside the bytes actually read. The file came off a floppy someone else
; wrote (SPEC.md 19: every byte is hostile), and the destination is a segment
; nobody else can see.
;
; The image is placed at the top-left corner: a bigger picture is cropped, a
; smaller one leaves white, and the canvas geometry never changes - the window
; is sized to the screen, not to the file.
; -----------------------------------------------------------------------------
pt_bmp_in:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [pt_fsz], ax
    mov ax, [es:10]
    mov [pt_boff], ax               ; bfOffBits...
    cmp word [es:12], 0             ; ...which must be a 16-bit quantity
    jne .bad
    cmp word [es:16], 0
    jne .bad
    mov ax, [es:14]                 ; biSize: BITMAPINFOHEADER or later
    cmp ax, 40
    jb .bad
    mov [pt_bhsz], ax
    cmp word [es:20], 0
    jne .bad
    mov ax, [es:18]                 ; biWidth
    or ax, ax
    jz .bad
    mov [pt_bw], ax
    mov ax, [es:22]                 ; biHeight, signed: negative = top-down
    mov dx, [es:24]
    mov byte [pt_btd], 0
    or dx, dx
    jz .hpos
    cmp dx, 0xFFFF
    jne .bad
    neg ax
    mov byte [pt_btd], 1
.hpos:
    or ax, ax
    jz .bad
    mov [pt_bh], ax
    cmp word [es:30], 0             ; biCompression must be BI_RGB: no RLE4,
    jne .nocomp                     ; no RLE8, no bitfields
    cmp word [es:32], 0
    jne .nocomp
    mov ax, [es:28]                 ; biBitCount
    mov [pt_bpp], ax
    cmp ax, 1
    je .depth
    cmp ax, 4
    je .depth
    cmp ax, 8
    je .depth
    cmp ax, 24
    je .depth
    jmp .nodepth
.depth:
    ; --- source stride: ((w*bpp + 31) / 32) * 4 ---------------------------
    mov ax, [pt_bw]
    mul word [pt_bpp]
    or dx, dx
    jnz .big
    add ax, 31
    jc .big
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shl ax, 1
    shl ax, 1
    mov [pt_bstr], ax
    ; --- and the pixel data must be inside what we read -------------------
    mov ax, [pt_bh]
    mul word [pt_bstr]
    or dx, dx
    jnz .big
    add ax, [pt_boff]
    jc .big
    cmp ax, [pt_fsz]
    ja .big
    ; --- the palette, mapped to our sixteen -------------------------------
    cmp word [pt_bpp], 24
    je .rows
    call pt_bmp_pal
    ; --- undo, then the rows ----------------------------------------------
.rows:
    call pt_text_end
    call pt_sel_drop
    call pt_undo_new
    xor ax, ax
    mov dx, [pt_ch]
    dec dx
    call pt_umark                   ; the whole old picture: Open is undoable
    mov ax, [pt_bw]                 ; columns we will actually take
    cmp ax, PT_CW
    jbe .cols
    mov ax, PT_CW
.cols:
    mov [pt_cols], ax
    mov al, CWHITE                  ; anything the file does not cover
    call pt_wipe
    xor di, di                      ; DI = destination row
.row:
    cmp di, [pt_ch]
    jae .done
    cmp di, [pt_bh]
    jae .done
    mov ax, di                      ; source row: bottom-up unless biHeight
    cmp byte [pt_btd], 0            ; said otherwise
    jne .top
    mov ax, [pt_bh]
    dec ax
    sub ax, di
.top:
    mul word [pt_bstr]
    add ax, [pt_boff]
    mov [pt_srow], ax
    call pt_bmp_row                 ; -> pt_line, one colour index per column
    call pt_line_put                ; -> the canvas row DI
    inc di
    jmp short .row
.done:
    call pt_blit_all
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    mov si, pt_s_badpic
    jmp short .fail
.nocomp:
    mov si, pt_s_nocomp
    jmp short .fail
.nodepth:
    mov si, pt_s_nodepth
    jmp short .fail
.big:
    mov si, pt_s_badpic
.fail:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_bmp_pal - build pt_pmap: the file's palette entries -> our sixteen
; in:  ES = PT_CBSEG, [pt_bhsz], [pt_bpp]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_bmp_pal:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [es:46]                 ; biClrUsed, or the depth's full table
    cmp word [es:48], 0
    jne .full
    or ax, ax
    jnz .have
.full:
    mov cl, [pt_bpp]                ; no biClrUsed: the depth's full table
    mov ax, 1
    shl ax, cl                      ; (bpp is 1, 4 or 8 on this path)
.have:
    cmp ax, 256
    jbe .cap
    mov ax, 256
.cap:
    mov cx, ax
    mov si, 14
    add si, [pt_bhsz]               ; the table follows the info header
    xor di, di
.ent:
    push cx                         ; CX is the entry counter AND half of
                                    ; pt_map16's argument (CH = green): load
                                    ; the colour only with the count stacked,
                                    ; or the loop runs 65,000 times and walks
                                    ; pt_pmap straight out of the package
    mov dl, [es:si]                 ; RGBQUAD: blue, green, red, reserved
    mov ch, [es:si+1]
    mov cl, [es:si+2]
    call pt_map16
    mov [pt_pmap+di], al
    pop cx
    add si, 4
    inc di
    dec cx
    jnz .ent
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_map16 - the nearest of the sixteen EGA colours
; in:  CL = red, CH = green, DL = blue
; out: AL = colour index; preserves all other registers
;
; Weighted city-block distance (2R + 3G + B), which fits a word at every step
; and orders sixteen widely-spaced colours the same way a sum of squares
; would - without the 32-bit accumulator one would need.
; -----------------------------------------------------------------------------
pt_map16:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, pt_pal_rgb
    mov word [pt_mbest], 0xFFFF
    mov byte [pt_mbi], 0
    xor bx, bx
.cand:
    mov al, cl
    sub al, [si]
    jnc .r
    neg al
.r:
    xor ah, ah
    mov di, ax
    add di, ax                      ; 2 * |dr|
    mov al, ch
    sub al, [si+1]
    jnc .g
    neg al
.g:
    xor ah, ah
    add di, ax
    add di, ax
    add di, ax                      ; + 3 * |dg|
    mov al, dl
    sub al, [si+2]
    jnc .b
    neg al
.b:
    xor ah, ah
    add di, ax                      ; + |db|
    cmp di, [pt_mbest]
    jae .next
    mov [pt_mbest], di
    mov [pt_mbi], bl
.next:
    add si, 3
    inc bx
    cmp bx, 16
    jb .cand
    mov al, [pt_mbi]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_bmp_row - one source row into pt_line as colour indices
; in:  ES = PT_CBSEG, [pt_srow], [pt_cols], [pt_bpp]
; out: pt_line filled; preserves all registers
;
; A line buffer rather than a direct canvas write, because the source is in ES
; and the destination is in another segment entirely; DS has to stay on the
; kernel segment for pt_pmap and pt_rowtab. pt_line_put then packs it.
; -----------------------------------------------------------------------------
pt_bmp_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = column
    cmp word [pt_bpp], 24
    je .rgb
    cmp word [pt_bpp], 8
    je .idx8
    cmp word [pt_bpp], 4
    je .idx4
; --- 1bpp -------------------------------------------------------------------
.idx1:
    mov bx, di
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, [pt_srow]
    mov ah, [es:bx]
    mov cx, di
    and cl, 7
    xor cl, 7                       ; bit 7 is the leftmost pixel
    shr ah, cl
    mov al, ah
    and al, 1
    jmp short .store
; --- 4bpp -------------------------------------------------------------------
.idx4:
    mov bx, di
    shr bx, 1
    add bx, [pt_srow]
    mov al, [es:bx]
    test di, 1
    jnz .lo4
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo4:
    and al, 0x0F
    jmp short .store
; --- 8bpp -------------------------------------------------------------------
.idx8:
    mov bx, di
    add bx, [pt_srow]
    mov al, [es:bx]
    jmp short .store
; --- 24bpp ------------------------------------------------------------------
.rgb:
    mov ax, di
    mov bx, 3
    mul bx
    add ax, [pt_srow]
    mov bx, ax
    mov dl, [es:bx]                 ; blue, green, red
    mov ch, [es:bx+1]
    mov al, [es:bx+2]
    mov cl, al
    call pt_map16                   ; AL = our nearest colour: no pt_pmap
    mov [pt_line+di], al            ; indirection for a true-colour source
    jmp short .next
.store:
    xor bx, bx
    mov bl, al
    mov al, [pt_pmap+bx]
    mov [pt_line+di], al
.next:
    inc di
    cmp di, [pt_cols]
    jae .out
    cmp word [pt_bpp], 24
    je .rgb
    cmp word [pt_bpp], 8
    je .idx8
    cmp word [pt_bpp], 4
    je .idx4
    jmp .idx1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_line_put - pack pt_line into canvas row DI (canvas only, no screen)
; in:  DI = canvas row, [pt_cols] = pixels in pt_line
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_line_put:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, PT_CVSEG
    mov es, ax
    mov bx, di
    add bx, bx
    mov bx, [pt_rowtab+bx]          ; BX = the row's byte offset
    xor si, si                      ; SI = column
.col:
    mov al, [pt_line+si]
    test si, 1
    jnz .lo
    mov cl, 4
    shl al, cl
    mov dl, al                      ; hold the high nibble back
    mov ax, si
    inc ax
    cmp ax, [pt_cols]
    jb .next                        ; ...unless this is the last pixel
    mov di, si
    shr di, 1
    add di, bx
    mov al, [es:di]
    and al, 0x0F
    or al, dl
    mov [es:di], al
    jmp short .next
.lo:
    and al, 0x0F
    or al, dl
    mov di, si
    shr di, 1
    add di, bx
    mov [es:di], al
.next:
    inc si
    cmp si, [pt_cols]
    jb .col
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================

pt_title:    db 'Paint', 0          ; also the duplicate check's fingerprint
pt_appname:  db 'Paint', 0          ; the menu bar's app label (SPEC.md 12.2)
pt_s_defname: db 'PICTURE.BMP', 0

; --- the window template (SPEC.md 11; x/y/w/h patched by pt_geom) -------------
pt_tpl:
    dw 0                            ; WT_X
    dw PT_WIN_Y                     ; WT_Y
    dw PT_FRAME_W                   ; WT_W
    dw PT_CH_MAX + PT_CHROME_H      ; WT_H
    dw pt_title                     ; WT_TITLE
    dw pt_paint                     ; WT_PAINT
    dw pt_onkey                     ; WT_ONKEY
    dw pt_click                     ; WT_ONCLICK

; --- menus (SPEC.md 12.2) ----------------------------------------------------
    OS88_MENUSET pt_menus, pt_appname, pt_oncmd
        OS88_MENU pt_s_file, pt_it_file, 4
        OS88_MENU pt_s_edit, pt_it_edit, 5
        OS88_MENU pt_s_draw, pt_it_draw, 4
    OS88_MENUSET_END pt_menus

pt_s_file:   db 'File', 0
pt_s_edit:   db 'Edit', 0
pt_s_draw:   db 'Draw', 0
pt_it_file:  dw pt_i_new, pt_i_open, pt_i_save, pt_i_saveas
pt_it_edit:  dw pt_i_undo, pt_i_cut, pt_i_copy, pt_i_paste, pt_i_clear
pt_it_draw:  dw pt_i_fill, pt_i_f1, pt_i_f2, pt_i_f4
pt_i_new:    db 'New', 0
pt_i_open:   db 'Open...', 0
pt_i_save:   db 'Save', 0
pt_i_saveas: db 'Save As...', 0
pt_i_undo:   db 'Undo / Redo', 0
pt_i_cut:    db 'Cut', 0
pt_i_copy:   db 'Copy', 0
pt_i_paste:  db 'Paste', 0
pt_i_clear:  db 'Clear', 0
pt_i_fill:   db 'Filled Shapes', 0
pt_i_f1:     db 'Text Size 1x', 0
pt_i_f2:     db 'Text Size 2x', 0
pt_i_f4:     db 'Text Size 4x', 0

; --- the notice windows (indexed by [pt_mode] - 1) ---------------------------
pt_notice:   dw pt_s_nomem, pt_s_dup, pt_s_small
pt_s_nomem:  db 'Not enough memory.', 0
pt_s_dup:    db 'Paint is already running.', 0
pt_s_small:  db 'The screen is too small.', 0
pt_s_note2:  db 'Close this window.', 0

; --- toasts ------------------------------------------------------------------
pt_s_wrote:   db 'Saved', 0
pt_s_opened:  db 'Opened', 0
pt_s_nofmt:   db 'Only BMP is supported', 0
pt_s_badpic:  db 'Not a picture we can read', 0
pt_s_nocomp:  db 'Compressed BMP not supported', 0
pt_s_nodepth: db 'Need a 1, 4, 8 or 24-bit BMP', 0

; FERR_* -> toast, indexed by the code (SPEC.md 18.4)
pt_ferrtab:  dw pt_s_wrote, pt_fe_nodisk, pt_fe_io, pt_fe_name, pt_fe_noent
             dw pt_fe_exist, pt_fe_full, pt_fe_dirfull, pt_fe_prot, pt_fe_wprot
             dw pt_fe_big
pt_fe_nodisk:  db 'No data disk', 0
pt_fe_io:      db 'Disk error', 0
pt_fe_name:    db 'Bad file name', 0
pt_fe_noent:   db 'No such file', 0
pt_fe_exist:   db 'Name already taken', 0
pt_fe_full:    db 'Disk full', 0
pt_fe_dirfull: db 'Directory full', 0
pt_fe_prot:    db 'File is protected', 0
pt_fe_wprot:   db 'Disk is write-protected', 0
pt_fe_big:     db 'File too large', 0

; --- brush widths: the pencil's four, then the eraser's ----------------------
; The eraser's set starts where the pencil's ends, which is the whole of "a
; default thickness much greater than the drawing tool's".
pt_thtab:    db 1, 2, 4, 8
             db 8, 16, 24, 32

; --- bit masks for the undo row bitmap --------------------------------------
pt_bit8:     db 1, 2, 4, 8, 16, 32, 64, 128

; --- the three colours a 1bpp adapter can really show (SPEC.md 39.4) --------
; black, the 50% dither class, white
pt_mono_pal: db CBLACK, CLGRAY, CWHITE

; --- the standard EGA/VGA palette, R,G,B per entry --------------------------
; Written into every BMP we save, and the target of pt_map16 for every one we
; load, so a file round-trips through a host paint program unchanged.
pt_pal_rgb:
    db 0x00,0x00,0x00               ; 0  black
    db 0x00,0x00,0xAA               ; 1  blue
    db 0x00,0xAA,0x00               ; 2  green
    db 0x00,0xAA,0xAA               ; 3  cyan
    db 0xAA,0x00,0x00               ; 4  red
    db 0xAA,0x00,0xAA               ; 5  magenta
    db 0xAA,0x55,0x00               ; 6  brown
    db 0xAA,0xAA,0xAA               ; 7  light grey
    db 0x55,0x55,0x55               ; 8  dark grey
    db 0x55,0x55,0xFF               ; 9  light blue
    db 0x55,0xFF,0x55               ; 10 light green
    db 0x55,0xFF,0xFF               ; 11 light cyan
    db 0xFF,0x55,0x55               ; 12 light red
    db 0xFF,0x55,0xFF               ; 13 light magenta
    db 0xFF,0xFF,0x55               ; 14 yellow
    db 0xFF,0xFF,0xFF               ; 15 white

; --- the eight tool glyphs, 16x16, bit 15 = leftmost ------------------------
pt_ic_tab:   dw pt_ic_pencil, pt_ic_eraser, pt_ic_drop, pt_ic_rect
             dw pt_ic_oval, pt_ic_sel, pt_ic_fill, pt_ic_text

; a pencil, tip down-left
pt_ic_pencil:
    dw 0x0000, 0x001C, 0x003E, 0x007C, 0x00F8, 0x01F0, 0x03E0, 0x07C0
    dw 0x0F80, 0x1F00, 0x3E00, 0x7C00, 0x7800, 0x7000, 0x4000, 0x0000
; an eraser block above the swept line
pt_ic_eraser:
    dw 0x0000, 0x01F0, 0x03F8, 0x07FC, 0x0F1E, 0x1E3C, 0x3C78, 0x78F0
    dw 0x79E0, 0x3FC0, 0x1F80, 0x0000, 0x3FFC, 0x3FFC, 0x0000, 0x0000
; an eyedropper: bulb, barrel, tip - upright, so it cannot be mistaken for
; the pencil, which the diagonal version of this glyph was
pt_ic_drop:
    dw 0x01E0, 0x0330, 0x0330, 0x0330, 0x03F0, 0x01E0, 0x01E0, 0x01E0
    dw 0x01E0, 0x01E0, 0x00C0, 0x00C0, 0x00C0, 0x0040, 0x0000, 0x0000
; a hollow rectangle
pt_ic_rect:
    dw 0x0000, 0x0000, 0x3FFC, 0x3FFC, 0x300C, 0x300C, 0x300C, 0x300C
    dw 0x300C, 0x300C, 0x3FFC, 0x3FFC, 0x0000, 0x0000, 0x0000, 0x0000
; a hollow ellipse
pt_ic_oval:
    dw 0x0000, 0x07E0, 0x1FF8, 0x3C3C, 0x700E, 0x6006, 0x6006, 0x6006
    dw 0x6006, 0x700E, 0x3C3C, 0x1FF8, 0x07E0, 0x0000, 0x0000, 0x0000
; a dashed marquee
pt_ic_sel:
    dw 0x0000, 0x39CE, 0x2004, 0x2004, 0x0002, 0x2002, 0x2002, 0x0002
    dw 0x2002, 0x2002, 0x0002, 0x2006, 0x2004, 0x39CE, 0x0000, 0x0000
; a tipping paint can
pt_ic_fill:
    dw 0x0000, 0x0060, 0x00F0, 0x0198, 0x030C, 0x0606, 0x0C0C, 0x1818
    dw 0x3030, 0x1860, 0x0CC0, 0x0780, 0x0300, 0x0780, 0x0780, 0x0300
; a capital A
pt_ic_text:
    dw 0x0000, 0x07E0, 0x07E0, 0x0E70, 0x0E70, 0x1C38, 0x1C38, 0x1FF8
    dw 0x1FF8, 0x381C, 0x381C, 0x700E, 0x700E, 0x0000, 0x0000, 0x0000

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes PT_BSS_TOTAL bytes after the image and
; every name below is an offset from os88_image_end)
;
; The offsets are assigned by the three macros rather than written out, because
; there are ninety of them and a hand-maintained running total is a defect
; waiting to happen. PT_BSS_TOTAL falls out of the same counter.
; =============================================================================

%assign PT_BSS 0
%macro PTWORD 1                     ; one word
%1 equ os88_image_end + PT_BSS
%assign PT_BSS PT_BSS + 2
%endmacro
%macro PTBYTE 1                     ; one byte
%1 equ os88_image_end + PT_BSS
%assign PT_BSS PT_BSS + 1
%endmacro
%macro PTBUF 2                      ; %2 bytes
%1 equ os88_image_end + PT_BSS
%assign PT_BSS PT_BSS + (%2)
%endmacro

    PTWORD pt_win                   ; our window record
    PTWORD pt_ch                    ; canvas height, decided by pt_geom
    PTWORD pt_ox                    ; content origin, screen coords
    PTWORD pt_oy
    PTWORD pt_cx0                   ; canvas origin, screen coords
    PTWORD pt_cy0
    PTWORD pt_stripy                ; content y of the bottom strip

    ; the pending rectangle (in) and its clipped form (out) - pt_rect/pt_blit
    PTWORD pt_rx1
    PTWORD pt_ry1
    PTWORD pt_rx2
    PTWORD pt_ry2
    PTWORD pt_cx1
    PTWORD pt_cy1
    PTWORD pt_cx2
    PTWORD pt_cy2
    PTWORD pt_lb                    ; left byte index within a row
    PTWORD pt_span                  ; right byte - left byte
    PTWORD pt_scan0                 ; pt_runend: where the pair scan began
    PTWORD pt_runx                  ; pt_blit: the run's first and last column
    PTWORD pt_runy

    PTWORD pt_ax                    ; the press point, canvas coords
    PTWORD pt_ay
    PTWORD pt_bx                    ; chrome scratch: a button's origin
    PTWORD pt_by
    PTWORD pt_hitx                  ; the click, while pt_btn_xy uses CX/DX
    PTWORD pt_hity
    PTWORD pt_key                   ; the key, or the (menu, item) pair

    ; the stroke engine
    PTWORD pt_wx                    ; the brush's live position
    PTWORD pt_wy
    PTWORD pt_tox                   ; ...and where this segment ends
    PTWORD pt_toy
    PTWORD pt_ddx
    PTWORD pt_ddy
    PTWORD pt_sx                    ; step direction, +1 or -1
    PTWORD pt_sy
    PTWORD pt_err
    PTWORD pt_blo                   ; the dab spans x-blo .. x+bhi
    PTWORD pt_bhi

    ; the rubber band and the selection
    PTWORD pt_anchx
    PTWORD pt_anchy
    PTWORD pt_curx
    PTWORD pt_cury
    PTWORD pt_shx1
    PTWORD pt_shy1
    PTWORD pt_shx2
    PTWORD pt_shy2
    PTWORD pt_selx1
    PTWORD pt_sely1
    PTWORD pt_selx2
    PTWORD pt_sely2

    ; the ellipse
    PTWORD pt_oa
    PTWORD pt_ob
    PTWORD pt_ob2
    PTWORD pt_ody
    PTWORD pt_ow
    PTWORD pt_owp                   ; the previous row's half-width
    PTWORD pt_oxc1
    PTWORD pt_oxc2
    PTWORD pt_oyc1
    PTWORD pt_oyc2
    PTWORD pt_or1
    PTWORD pt_or2

    ; the flood fill
    PTWORD pt_fsp                   ; span-stack depth
    PTWORD pt_fy
    PTWORD pt_fx1
    PTWORD pt_fx2
    PTWORD pt_fdy
    PTWORD pt_fx
    PTWORD pt_fl
    PTWORD pt_fr

    ; the clipboard and the paste
    PTWORD pt_cbw
    PTWORD pt_cbh
    PTWORD pt_pdx
    PTWORD pt_pdy
    PTWORD pt_pbase
    PTWORD pt_pstr
    PTWORD pt_ptmp

    ; the text tool
    PTWORD pt_txtx
    PTWORD pt_txty
    PTWORD pt_txtx0                 ; the left margin a wrap returns to
    PTWORD pt_carx
    PTWORD pt_cary
    PTWORD pt_cary2
    PTWORD pt_gbox                  ; the glyph box at the live scale
    PTWORD pt_grs                   ; the open run in a glyph row

    ; icon drawing, the toast, undo bookkeeping
    PTWORD pt_ic_x
    PTWORD pt_ic_y
    PTWORD pt_ic_rs
    PTWORD pt_msgw
    PTWORD pt_msgp                  ; the toast a file operation asked for
    PTWORD pt_uy1                   ; the rows an undo swap touched
    PTWORD pt_uy2
    PTWORD pt_mbest                 ; pt_map16's best distance so far

    ; the BMP reader
    PTWORD pt_fsz                   ; bytes actually read
    PTWORD pt_boff                  ; bfOffBits
    PTWORD pt_bhsz                  ; biSize
    PTWORD pt_bw                    ; biWidth
    PTWORD pt_bh                    ; |biHeight|
    PTWORD pt_bpp                   ; biBitCount
    PTWORD pt_bstr                  ; source row stride
    PTWORD pt_srow                  ; the source row being read
    PTWORD pt_cols                  ; columns we take from it

    PTBYTE pt_mode                  ; PT_M_*
    PTBYTE pt_tool                  ; PT_T_*
    PTBYTE pt_col                   ; the colour the user picked
    PTBYTE pt_ink                   ; the colour the operation in hand paints
    PTBYTE pt_thick                 ; pencil width index
    PTBYTE pt_ethick                ; eraser width index
    PTBYTE pt_filled                ; shapes: filled or outlined
    PTBYTE pt_fscale                ; text scale, 1/2/4 (what the strip shows)
    PTBYTE pt_gsh                   ; ...as a shift count, 0/1/2 (what it uses)
    PTBYTE pt_ncol                  ; palette entries this adapter shows
    PTBYTE pt_mono                  ; 1bpp adapter
    PTBYTE pt_selon                 ; a selection exists
    PTBYTE pt_selshown              ; ...and its marquee is on the glass
    PTBYTE pt_txton                 ; a text run is open
    PTBYTE pt_careton               ; ...and its caret is on the glass
    PTBYTE pt_msgon                 ; the toast is on the glass
    PTBYTE pt_undo_ok               ; the undo image holds something
    PTBYTE pt_undo_off              ; we ARE the undo: do not re-snapshot
    PTBYTE pt_noscr                 ; pt_rect: pixels only, no gfx call
    PTBYTE pt_pen                   ; chrome colour
    PTBYTE pt_fbyte                 ; pt_rect: the fill colour in both nibbles
                                    ; (in memory because the row loop needs BX)
    PTBYTE pt_lmask                 ; pt_rect's nibble edge masks and values
    PTBYTE pt_lval
    PTBYTE pt_rmask
    PTBYTE pt_rval
    PTBYTE pt_fold                  ; the colour a flood fill replaces
    PTBYTE pt_fovf                  ; the span stack ran out
    PTBYTE pt_cbbyte                ; pt_copy's half-packed byte
    PTBYTE pt_gch                   ; the glyph being drawn
    PTBYTE pt_mbi                   ; pt_map16's best index so far
    PTBYTE pt_btd                   ; the BMP is top-down
    PTBYTE pt_dmode                 ; the mode the file dialog ran in
    PTBUF  pt_fdigit, 2             ; the strip's scale digit, NUL-terminated

    PTBUF  pt_umask, 36             ; one bit per canvas row (280 -> 35 bytes)
    PTBUF  pt_rowtab, PT_CH_MAX * 2 ; canvas row -> byte offset (the whole of
                                    ; the bottom-up story)
    PTBUF  pt_glyphs, 95 * 8        ; the ROM 8x8 font, chars 32..126
    PTBUF  pt_line, PT_CW           ; one decoded source pixel per column
    PTBUF  pt_pmap, 256             ; a loaded palette -> our sixteen
    PTBUF  pt_name, 14              ; the current document

    OS88_BSS PT_BSS
    OS88_IMAGE_END
