; =============================================================================
; os8088 - viddet.inc
;
; Video adapter detection, runtime screen geometry, and the two addressing
; helpers every drawing path in the kernel goes through (SPEC.md 39).
;
; One kernel binary drives three adapters, chosen at boot in this order:
;
;   VID_VGA    640x480, 16 colours, 4 planes at A000:0000  (mode 12h)
;   VID_HERC   720x348, monochrome, 4 banks at B000:0000   (no BIOS mode)
;   VID_CGA    640x200, monochrome, 2 banks at B800:0000   (mode 6)
;
; The two fallbacks are both 1 bit per pixel with a BANKED framebuffer -
; consecutive scan lines do not sit consecutively in memory. That is the
; only structural difference between them, so ONE parameterized driver
; serves both: the bank count, the bank shift, the row stride and the
; segment are the four numbers that differ, and they live in vid_tab.
;
; The mono renderer itself is not in this file. It is vgabb.inc, which has
; always been a latch-free, port-free SOFTWARE renderer over the coordinate
; core in vga12.inc - written for a RAM back buffer, but nothing in it cares
; that the target is RAM. Point it at the real framebuffer with a plane count
; of 1 and it IS the 1bpp driver (SPEC.md 39.3). Hence [bb_on] means "route
; drawing through the software renderer", which is permanently 1 on mono,
; and the new [bb_dbl] carries the old "double buffering is armed" meaning.
;
; This module is included BEFORE splash.inc because the boot splash calls
; vid_detect/vid_apply/vid_setmode on its first tick, while the rest of the
; kernel is still coming off the floppy. Everything here therefore obeys the
; splash's rules: it must be resident inside the first SPL_RESIDENT sectors,
; and ALL its data lives in .text - never .bss, which is not yet cleared and
; still has the .fartext blob sitting on it (SPEC.md 15/33).
; =============================================================================

; A build-time override for the probe, so the fallbacks can be exercised on
; hardware that does not have the card: `make test VIDEO=cga` assembles
; VID_FORCE=3 and the kernel drives a real VGA as a CGA through int 10h mode
; 6, whose framebuffer is byte-for-byte a CGA's. 0 = auto-detect, which is
; what every shipped image is built with (SPEC.md 39.1).
%ifndef VID_FORCE
%define VID_FORCE 0
%endif

; Where the Hercules framebuffer lives. Overridable ONLY so the Hercules
; renderer can be verified without a Hercules card: no emulator this project
; uses outside 86Box has one, and B0000 is simply unmapped under QEMU, so
; every write vanishes and a wrong stride looks exactly like a right one.
; Point it at spare RAM instead and the whole 32KB can be read back out and
; reconstructed on the host (SPEC.md 39.9):
;   make test VIDEO=herc HERCSEG=0x7000
;   python3 tools/hercshot.py build/qmp.sock 0x70000 out.png
%ifndef VID_HERC_SEG
%define VID_HERC_SEG 0xB000
%endif

VID_VGA     equ 0
VID_HERC    equ 1
VID_CGA     equ 2

VID_REC     equ 18              ; bytes per vid_tab record
VID_LIVE_W  equ 9               ; words in the live block below

; -----------------------------------------------------------------------------
; The live geometry block (SPEC.md 39.2). NINE CONTIGUOUS WORDS, in vid_tab's
; column order - vid_apply rep movsw's a record straight over them, so do not
; reorder one without the other. Initialized to the VGA row so that anything
; reading them before vid_apply runs sees the historical constants.
; -----------------------------------------------------------------------------
vid_seg     dw VGA_SEG          ; framebuffer segment
vid_w       dw SCREEN_W         ; visible width, px
vid_h       dw SCREEN_H         ; visible height, px
vid_stride  dw ROW_BYTES        ; bytes from a row to the row 1 bank down
vid_bmask   dw 0                ; y & this = the bank number (0 on VGA)
vid_bshift  dw 0                ; y >> this = the row within the bank
vid_rowadd  dw ROW_BYTES        ; gfx_nextrow's unconditional step
vid_wrapbit dw 0                ; ...and the bit that says it overflowed the
vid_wrapfix dw 0                ; last bank; then add this to land on the
                                ; next row of bank 0. Both 0 on VGA, which
                                ; makes gfx_nextrow degenerate to `add di,80`
                                ; with no adapter test - see the routine.

; -----------------------------------------------------------------------------
; vid_tab - one 18-byte record per adapter, in VID_* order.
;
; wrapfix is stride - nbanks*0x2000 taken as a 16-bit add, and wrapbit is the
; bit that gfx_nextrow's unconditional `add` carries into once it steps off
; the last bank. Both rely on the whole intra-bank extent staying under
; 0x2000, which the assertion at the bottom of this file proves.
; -----------------------------------------------------------------------------
vid_tab:
    ;    seg     w    h  stride bmask bshift rowadd  wrapbit wrapfix
    dw VGA_SEG, 640, 480,   80,    0,     0,     80,       0,      0
    dw VID_HERC_SEG, 720, 348, 90,   3,     2, 0x2000,  0x8000, 0x805A
    dw  0xB800, 640, 200,   80,    1,     1, 0x2000,  0x4000, 0xC050

; --- derived from the live block by vid_apply (SPEC.md 39.2) -----------------
; Precomputed because the sites that want them are inner loops or single
; instructions that have no register to spare for the arithmetic.
vid_wm1     dw SCREEN_W-1       ; last legal x
vid_hm1     dw SCREEN_H-1       ; last legal y
vid_wm8     dw SCREEN_W-8       ; font_char's whole-cell clip, and the menu
                                ; bar's right margin
vid_hm8     dw SCREEN_H-8
vid_strm1   dw ROW_BYTES-1
vid_rseg    dw BB_SEG           ; the software renderer's target segment:
                                ; the back buffer on VGA, the FRAMEBUFFER
                                ; itself on mono
vid_rpara   dw BB_PLANE_PARA    ; paragraphs from one plane to the next.
                                ; MUST be nonzero even at one plane -
                                ; bb_xfer's loop terminates on a segment
                                ; compare, and a step of 0 never terminates.
vid_rend    dw BB_SEG + 4*BB_PLANE_PARA     ; that loop's exclusive end
vid_dock_y0 dw SCREEN_H-24      ; dock strip top row (DOCK_H, dock.inc)
vid_dock_ty0 dw SCREEN_H-24+3   ; dock tile top row
vid_clk_hx  dw SCREEN_W-206     ; menu bar clock cell: hit band left edge
vid_ymax    dw SCREEN_H-18      ; lowest y a title bar may be dragged to
vid_popmax  dw 16               ; pull-down items that fit under the bar
vid_desk_zx dw SCREEN_W-56      ; desktop drive column: hit zone left, and
vid_desk_zl dw SCREEN_W-58      ; the redraw rect's left (zx - 2) and
vid_desk_zr dw SCREEN_W-6       ; and its EXCLUSIVE right, zx + 50: the
                                ; 48px zone, the label's 2px overhang and a
                                ; window's 1px drop shadow. The fills use
                                ; zr - 1 as their inclusive right (633 at
                                ; 640 wide, which is what they open-coded).

vid_kind    db VID_VGA          ; VID_VGA / VID_HERC / VID_CGA
vid_mono    db 0                ; 1bpp adapter? Not [bb_mono], which means
                                ; "all four planes hold identical bytes"
vid_planes  db 4                ; 4 or 1
vid_planes_w dw 4               ; the same, where a word is wanted
vid_force   db VID_FORCE        ; 0 = probe; 1/2/3 = VGA/Herc/CGA

; Hercules has no BIOS mode, so vid_setmode programs the 6845 itself.
; R0..R11 for 720x348: 45 char clocks of 16 px across, 87 rows of 4 scan
; lines down.
vid_6845    db 0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02
            db 0x57, 0x57, 0x02, 0x03, 0x00, 0x00

; Colour -> ink for the 1bpp adapters (SPEC.md 39.4). 00h = black, FFh =
; white, 01h = the 50% dither class, which reuses the machinery gfx_fill_gray
; already has. The mapping is chosen so that every distinction the shipped UI
; relies on survives: Minesweeper's covered CLGRAY against its open CWHITE,
; its exploded CLRED mine against the CLGRAY ones, Piano's pressed CLBLUE key
; against an idle CWHITE one, and Recorder's CDGRAY disabled text on white.
; Accepted loss: light gray and dark gray both become the dither.
gfx_inktab:
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    db 0x01, 0x01, 0x01, 0x01, 0xFF, 0x01, 0xFF, 0xFF

; -----------------------------------------------------------------------------
; vid_detect - decide which adapter is installed (SPEC.md 39.1)
;
; in:       -
; out:      [vid_kind]
; clobbers: flags
;
; Order is binding. The equipment word is consulted LAST because an EGA or
; VGA driving a monochrome monitor also reports bits 5:4 = 11b, and we want
; those machines on the mode 12h path, not the Hercules one.
;
; Documented scope cut: no Hercules-versus-plain-MDA discrimination (the
; 0x3BA vertical-sync toggle test). A plain MDA is text-only, so there is no
; better action available; driving it as a Hercules is strictly less wrong
; than driving it as a CGA at B800.
; -----------------------------------------------------------------------------
vid_detect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [vid_force]
    or al, al
    jz .probe
    dec al                      ; 1/2/3 -> VID_VGA/VID_HERC/VID_CGA
    jmp short .set
.probe:
    mov ax, 0x1A00              ; VGA/MCGA: read Display Combination Code.
    int 0x10                    ; A pre-EGA BIOS ignores AH=1Ah and leaves
    cmp al, 0x1A                ; AL as we set it, which is 00h.
    je .vga
    mov ah, 0x12                ; EGA: "get EGA info" overwrites BL with the
    mov bl, 0x10                ; card's memory size. An XT BIOS leaves 10h
    int 0x10                    ; alone. An EGA does mode 12h, so it counts.
    cmp bl, 0x10
    jne .vga
    int 0x11                    ; equipment word, bits 5:4
    and al, 0x30
    cmp al, 0x30                ; 11b = a monochrome card at B000
    mov al, VID_CGA
    jne .set
    mov al, VID_HERC
    jmp short .set
.vga:
    mov al, VID_VGA
.set:
    mov [vid_kind], al
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
; vid_apply - publish [vid_kind]'s geometry into the live and derived blocks
;
; in:       [vid_kind]
; out:      the whole geometry block; [bb_on]; the cursor homed to the centre
;           of the new screen
; clobbers: flags
;
; Homing the cursor is not a nicety. [mouse_x]/[mouse_y] are initialized data
; sized for 640x480, and cur_draw computes its bottom clip as an UNSIGNED
; [vid_h] - y: left at 240 on a 200-row screen that subtraction wraps to
; ~65500, the row count stays 16, and the very first cursor draw writes past
; the end of the framebuffer.
; -----------------------------------------------------------------------------
vid_apply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, KERNEL_SEG          ; rep movsw writes through ES, and we may be
    mov es, ax                  ; called from the splash, where ES is the
                                ; boot sector's

    mov al, [vid_kind]          ; AL * 18 -> the record
    mov bl, VID_REC
    mul bl
    mov si, vid_tab
    add si, ax
    mov di, vid_seg
    mov cx, VID_LIVE_W
    cld
    rep movsw

    mov ax, [vid_w]             ; --- widths
    dec ax
    mov [vid_wm1], ax
    sub ax, 7
    mov [vid_wm8], ax
    sub ax, 198                 ; w - 8 - MENU_CLK_W - 6
    mov [vid_clk_hx], ax

    mov ax, [vid_h]             ; --- heights
    dec ax
    mov [vid_hm1], ax
    sub ax, 7
    mov [vid_hm8], ax
    mov ax, [vid_h]
    sub ax, TITLE_H
    mov [vid_ymax], ax
    sub ax, DOCK_H - TITLE_H
    mov [vid_dock_y0], ax
    add ax, 3
    mov [vid_dock_ty0], ax

    mov ax, [vid_stride]
    dec ax
    mov [vid_strm1], ax

    mov ax, [vid_w]             ; --- the desktop's drive column, right-hung
    sub ax, 56
    mov [vid_desk_zx], ax
    sub ax, 2
    mov [vid_desk_zl], ax
    add ax, 52                  ; zl + 52 = zx + 50
    mov [vid_desk_zr], ax

    mov ax, [vid_h]             ; --- pull-down items that fit under the bar
    sub ax, MBAR_H + 2
    mov cl, 4
    shr ax, cl
    cmp ax, MENU_POPMAX
    jbe .pop
    mov ax, MENU_POPMAX
.pop:
    mov [vid_popmax], ax

    mov byte [vid_mono], 0      ; --- the renderer's target
    mov byte [vid_planes], 4
    mov word [vid_planes_w], 4
    mov ax, BB_SEG
    mov bx, BB_PLANE_PARA
    mov dx, BB_SEG + 4*BB_PLANE_PARA
    cmp byte [vid_kind], VID_VGA
    je .rend
    mov byte [vid_mono], 1
    mov byte [vid_planes], 1
    mov word [vid_planes_w], 1
    mov ax, [vid_seg]           ; the software renderer draws straight into
    mov bx, 1                   ; the framebuffer - one "plane", and a step
    mov dx, ax                  ; of 1 paragraph purely so bb_xfer's segment
    inc dx                      ; compare can terminate
.rend:
    mov [vid_rseg], ax
    mov [vid_rpara], bx
    mov [vid_rend], dx
    mov al, [vid_mono]          ; mono routes every draw through the software
    mov [bb_on], al             ; renderer, permanently (SPEC.md 39.5)

    mov ax, [vid_w]             ; --- home the cursor; see the header
    shr ax, 1
    mov [mouse_x], ax
    mov ax, [vid_h]
    shr ax, 1
    mov [mouse_y], ax

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_setmode - put the card into its graphics mode (SPEC.md 39.6)
;
; in:       [vid_kind]
; out:      the framebuffer is cleared
; clobbers: flags
;
; VGA and CGA get their mode - and their clear - from the BIOS. Hercules has
; no BIOS mode at all, so the 6845 is programmed directly and the 32KB clear
; is ours to do; bb_init's ordering and the first desktop paint both assume a
; cleared framebuffer. The explicit `cld` matters: on the splash path this
; runs on the boot sector's flags, and spl_tick never issues one.
; -----------------------------------------------------------------------------
vid_setmode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [vid_kind], VID_HERC
    je .herc
    mov ax, 0x0012              ; VGA: 640x480x16 planar
    cmp byte [vid_kind], VID_CGA
    jne .bios
    mov ax, 0x0006              ; CGA: 640x200 monochrome
.bios:
    int 0x10
    jmp short .done
.herc:
    mov dx, 0x3BF               ; configuration: graphics allowed, both pages
    mov al, 3
    out dx, al
    mov si, vid_6845
    mov dx, 0x3B4
    xor ah, ah                  ; AH = CRTC register index
.crtc:
    mov al, ah
    out dx, al                  ; index port
    inc dx
    lodsb
    out dx, al                  ; data port
    dec dx
    inc ah
    cmp ah, 12
    jb .crtc
    mov dx, 0x3B8               ; graphics, video enabled, display page 0
    mov al, 0x0A
    out dx, al
    mov ax, [vid_seg]           ; 32KB clear - nobody else will do it
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 0x4000
    cld
    rep stosw
.done:
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
; vid_init - the whole boot-time video bring-up: probe, publish, set the mode
;
; in:       -
; out:      as the three below
; clobbers: flags
;
; Idempotent, and deliberately re-run by kmain after the splash already did
; it - exactly as kmain's mode set has always re-run the splash's own,
; which is what wipes the loading screen.
; -----------------------------------------------------------------------------
vid_init:
    call vid_detect
    call vid_apply
    call vid_setmode
    ret

; -----------------------------------------------------------------------------
; vid_text - back to a text mode (used by CMD_REBOOT)
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
vid_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [vid_kind], VID_HERC
    jne .colour
    mov dx, 0x3BF               ; lock graphics back out, then 80x25 mono
    xor al, al
    out dx, al
    mov ax, 0x0007
    jmp short .go
.colour:
    mov ax, 0x0003
.go:
    int 0x10
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
; gfx_rowbase - byte offset of the first byte of a scan line (SPEC.md 39.3)
;
; in:       AX = y
; out:      AX = offset into the framebuffer / back buffer
; clobbers: AX, CX, DX, flags. PRESERVES BX - ico_core depends on it.
;
; On VGA bmask and bshift are 0 and this reduces to exactly y * 80, which is
; what every planar path computed inline before.
;
; Every geometry word is read through CS, not DS - see gfx_nextrow.
; -----------------------------------------------------------------------------
gfx_rowbase:
    push bx
    mov bx, ax
    and bx, [cs:vid_bmask]      ; BX = bank number
    mov cl, 13
    shl bx, cl                  ; BX = bank * 0x2000
    mov cl, [cs:vid_bshift]
    shr ax, cl                  ; AX = row within the bank
    mul word [cs:vid_stride]    ; DX:AX; DX is always 0 (86*90 at worst)
    add ax, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_nextrow - advance a framebuffer pointer to the next scan line
;
; in:       DI = offset of a byte on row y
; out:      DI = the same column on row y+1
; clobbers: DI and flags, NOTHING ELSE. Several callers are inner loops with
;           no spare register, and one is inside the IRQ4 cursor path.
;
; The bank number lives in DI's own high bits, so stepping a bank is a plain
; add and the overflow out of the last bank shows up as a single bit. On VGA
; wrapbit is 0, `test di,0` sets ZF, and the whole thing is `add di,stride`
; with no adapter test on the path at all.
;
; CS OVERRIDES, NOT DS. Two of the callers run with DS pointed somewhere
; else entirely: bb_xfer's save path sets DS to the framebuffer segment for
; its movsb, and its restore path sets DS to the caller's buffer (SAVE_SEG
; for a menu's save-under). Reading the step through DS there fetches
; framebuffer bytes as a scan-line stride. CS is KERNEL_SEG for everything
; in .text, and .fartext reaches this only through the .text shims, so the
; override is always right and costs one byte per reference.
; -----------------------------------------------------------------------------
gfx_nextrow:
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .r
    add di, [cs:vid_wrapfix]
.r:
    ret

; The banked addressing above only works while a bank's rows cannot reach
; into the next bank's 0x2000 window. Hercules uses 87*90 = 7830 of 8192 and
; CGA 100*80 = 8000, so both hold - but not by much, and a change of stride
; or height would break it silently.
%if (348/4)*90 > 0x2000 || (200/2)*80 > 0x2000
%error "viddet: intra-bank extent exceeds 0x2000 - gfx_nextrow needs a row counter"
%endif

; -----------------------------------------------------------------------------
; gfx_ink - reduce a 16-colour index to a 1bpp ink (SPEC.md 39.4)
; in:       AL = colour 0..15
; out:      AL = 0x00 black, 0xFF white, 0x01 the 50% dither class
; clobbers: AL, flags
; -----------------------------------------------------------------------------
gfx_ink:
    push bx
    mov bl, al
    and bl, 15
    xor bh, bh
    mov al, [bx + gfx_inktab]
    pop bx
    ret
