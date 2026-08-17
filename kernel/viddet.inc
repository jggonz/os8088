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
; The mono renderer itself is not in this file. It is softgfx.inc, a latch-free,
; port-free SOFTWARE renderer over the coordinate core in vga12.inc: it does
; the VGA ALU's work with plain CPU instructions, so pointing it at the real
; framebuffer with a plane count of 1 IS the 1bpp driver (SPEC.md 39.3).
; [vid_mono] is what routes drawing through it.
;
; This module is included BEFORE splash.inc because the boot splash calls
; vid_detect/vid_apply/vid_setmode on its first tick, while the rest of the
; kernel is still coming off the floppy. Everything here therefore obeys the
; splash's rules: it must be resident inside the first SPL_RESIDENT sectors,
; and ALL its data lives in .text - never .bss, which is not yet cleared and
; holds whatever the machine left there (SPEC.md 15).
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
; vid_tab - one 18-byte record per adapter, in VID_* order.
;
; wrapfix is stride - nbanks*0x2000 taken as a 16-bit add, and wrapbit is the
; bit that gfx_nextrow's unconditional `add` carries into once it steps off
; the last bank. Both rely on the whole intra-bank extent staying under
; 0x2000, which the assertion at the bottom of this file proves.
; -----------------------------------------------------------------------------
; THE WIDTH AND HEIGHT ARE LAST, and that is not how this table used to read.
; They moved to the end so the nine columns land on nine words that a DISPLAY
; owns (SPEC.md 39.12): with w and h in the middle, the per-display run was
; broken by the two words that describe the DESKTOP. Here they are vid_cw and
; vid_ch - the active display's clip extent - and the desktop's own vid_w /
; vid_h are derived below, out of the run.
vid_tab:
    ;    seg     stride bmask bshift rowadd  wrapbit wrapfix    cw   ch
    dw VGA_SEG,      80,    0,     0,     80,       0,      0, 640, 480
    dw VID_HERC_SEG, 90,    3,     2, 0x2000,  0x8000, 0x805A, 720, 348
    dw  0xB800,      80,    1,     1, 0x2000,  0x4000, 0xC050, 640, 200

; -----------------------------------------------------------------------------
; The live geometry block (SPEC.md 39.2). NINE CONTIGUOUS WORDS, in vid_tab's
; column order - vid_apply rep movsw's a record straight over them, so do not
; reorder one without the other. Initialized to the VGA row so that anything
; reading them before vid_apply runs sees the historical constants.
;
; IT IS THE FIRST HALF OF A LONGER CONTIGUOUS RUN, and that is now load-bearing
; rather than incidental. `vid_seg` through `vid_rend` - the nine live words and
; the nine derived ones below them - are the eighteen words A DISPLAY OWNS, and
; SPEC.md 39.12's per-display context copies exactly that span with one
; `rep movsw`. Everything from `vid_dock_y0` down is the DESKTOP's and must stay
; out of it. `vid_tab` used to sit between the two halves, which is why it has
; moved up here: it is a static table and it was splitting a run that wants to
; be one.
; -----------------------------------------------------------------------------
vid_seg     dw VGA_SEG          ; framebuffer segment
vid_stride  dw ROW_BYTES        ; bytes from a row to the row 1 bank down
vid_bmask   dw 0                ; y & this = the bank number (0 on VGA)
vid_bshift  dw 0                ; y >> this = the row within the bank
vid_rowadd  dw ROW_BYTES        ; gfx_nextrow's unconditional step
vid_wrapbit dw 0                ; ...and the bit that says it overflowed the
vid_wrapfix dw 0                ; last bank; then add this to land on the
                                ; next row of bank 0. Both 0 on VGA, which
                                ; makes gfx_nextrow degenerate to `add di,80`
                                ; with no adapter test - see the routine.
vid_cw      dw SCREEN_W         ; THE ACTIVE DISPLAY's extent, and what every
vid_ch      dw SCREEN_H         ; PRIMITIVE clips to (SPEC.md 39.2.1). Not the
                                ; desktop's - see vid_w / vid_h below.

; --- derived from the live block by vid_apply (SPEC.md 39.2) -----------------
; Precomputed because the sites that want them are inner loops or single
; instructions that have no register to spare for the arithmetic.
vid_bankmask dw 0              ; gfx_prevrow: DI's bank bits, (nbanks-1)<<13
vid_strm1   dw ROW_BYTES-1
vid_rseg    dw 0                ; the software renderer's target segment:
                                ; 0 on VGA, where nothing routes through the
                                ; software renderer; the FRAMEBUFFER itself
                                ; on mono
vid_rpara   dw SW_PLANE_PARA    ; paragraphs from one plane to the next.
                                ; MUST be nonzero even at one plane -
                                ; sw_xfer's loop terminates on a segment
                                ; compare, and a step of 0 never terminates.
vid_rend    dw 0                ; that loop's exclusive end
vid_cwm1    dw SCREEN_W-1       ; the display's last legal x and y, and the
vid_chm1    dw SCREEN_H-1       ; whole-cell limits font_char and ico_core
vid_cwm8    dw SCREEN_W-8       ; clip against. THE LAST WORD OF THE RUN a
vid_chm8    dw SCREEN_H-8       ; display owns - vidsel.inc asserts it.

; --- THE VIRTUAL DESKTOP (SPEC.md 39.2.1) ------------------------------------
; What the WINDOW SYSTEM lays out in, and on a one-display machine identical to
; the four words above it. The distinction only becomes visible when a desktop
; spans two cards, and it is drawn HERE - outside the per-display run - because
; the alternative was renaming these six at sixty-odd call sites across wm.inc,
; ui.inc, menu.inc, desk.inc and dock.inc. The renderer's side of the same
; question is twenty-two sites and enumerable, so the renderer is what moved.
vid_w       dw SCREEN_W         ; desktop width, px
vid_h       dw SCREEN_H         ; desktop height, px
vid_wm1     dw SCREEN_W-1       ; last legal x
vid_hm1     dw SCREEN_H-1       ; last legal y
vid_wm8     dw SCREEN_W-8       ; the menu bar's right margin
vid_hm8     dw SCREEN_H-8
; --- THE CHROME's extent (SPEC.md 39.16) -------------------------------------
; The menu bar, the dock and the drive column are full-width strips on the
; PRIMARY display, not across the desktop - spanning them over a union would
; put the clock and half the dock on the other monitor. So they measure
; themselves against these four rather than against vid_w/vid_h, which are the
; desktop's. On a machine with one display the two sets are the same numbers.
vid_pw      dw SCREEN_W
vid_ph      dw SCREEN_H
vid_pwm1    dw SCREEN_W-1
vid_phm1    dw SCREEN_H-1

vid_dock_y0 dw SCREEN_H-24      ; dock strip top row (DOCK_H, dock.inc)
vid_dock_ty0 dw SCREEN_H-24+3   ; dock tile top row
vid_clk_hx  dw (SCREEN_W-206) & 0FFF8h  ; menu bar clock cell: hit band left
                                ; edge, and the left edge of the clock's own
                                ; SEGMENT (SPEC.md 12.1) - on a GLYPH CELL
                                ; boundary, which is what lets the bar's three
                                ; segments be whole numbers of 8px cells and
                                ; font_run's single-pass path reach them
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
vid_mono    db 0                ; 1bpp adapter? This is what routes every
                                ; draw through the software renderer (32/39.5)
vid_planes  db 4                ; 4 or 1
vid_planes_w dw 4               ; the same, where a word is wanted
vid_force   db VID_FORCE        ; 0 = probe; 1/2/3 = VGA/Herc/CGA

; Hercules has no BIOS mode, so vid_setmode programs the 6845 itself.
; R0..R11 for 720x348: 45 char clocks of 16 px across, 87 rows of 4 scan
; lines down.
vid_6845    db 0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02
            db 0x57, 0x57, 0x02, 0x03, 0x00, 0x00

; ...and R0..R11 for MDA 80x25 TEXT, which vid_mono_text needs for the same
; reason: on a machine whose int 10h belongs to a VGA there is no BIOS call
; that will program THIS card (SPEC.md 39.20). IBM's own mode 7 values, with
; one deliberate difference - R10 = 0x20 is bit 5, CURSOR OFF, because
; neither caller wants one. The fsx bracket hides it with int 10h and on this
; class of machine that int 10h talks to the other card; the reboot path is
; only ever taken when a VGA is present, so nothing will print here.
vid_6845t   db 0x61, 0x50, 0x52, 0x0F, 0x19, 0x06
            db 0x19, 0x19, 0x02, 0x0D, 0x20, 0x00

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

; The bank number's byte offset, indexed by the bank itself. `shl bx, 13` is
; 8 clocks plus 4 per bit on an 8088 - 60 for a shift of a value that has
; four possible answers, and gfx_rowbase is on the fixed cost of EVERY
; drawing call (PERFORMANCE.md Part 2). Four banks is the maximum the 0x2000
; window assertion at the bottom of this file allows.
vid_banktab:
    dw 0x0000, 0x2000, 0x4000, 0x6000

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
; out:      the whole geometry block; [vid_mono]; the cursor homed to the
;           centre of the new screen
; clobbers: flags
;
; Homing the cursor is not a nicety. [mouse_x]/[mouse_y] are initialized data
; sized for 640x480, and cur_draw computes its bottom clip as an UNSIGNED
; [vid_h] - y: left at 240 on a 200-row screen that subtraction wraps to
; ~65500, the row count stays 16, and the very first cursor draw writes past
; the end of the framebuffer.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; vid_depth_set - publish how deep adapter AL's framebuffer is
;
; in:       AL = a VID_* kind
; out:      [vid_mono], [vid_planes], [vid_planes_w]
; clobbers: flags
;
; THESE THREE ARE NOT IN THE PER-DISPLAY RUN AND HAVE TO FOLLOW A SWAP ANYWAY,
; which is the whole reason this is a routine rather than eleven lines inside
; vid_apply. SPEC.md 39.12's context is the eighteen contiguous words from
; vid_seg to vid_chm8; [vid_mono] and the two plane counts sit above that run,
; are DERIVED from the kind rather than being vid_tab columns, and are what
; every primitive dispatches on. So vid_ctx_act has to publish them as well,
; and if it did so from its own copy of the rule the two could disagree.
;
; IT WAS UNREACHABLE UNTIL THE TWO DISPLAYS COULD DIFFER IN DEPTH. A Hercules
; and a CGA are both 1bpp, so every swap SPEC.md 39.12-39.19 shipped moved
; geometry between two displays that already agreed about all three bytes.
; A VGA beside a Hercules is the first pairing where they do not
; (docs/DUAL-DISPLAY-VGA.md), and the failure is not subtle: [vid_rseg] IS in
; the run, so activating the VGA display while [vid_mono] still says 1 points
; the SOFTWARE renderer at the VGA's rseg, which vid_apply sets to 0 because
; nothing on a VGA routes through it. A white fill then writes 0xFF through
; SEGMENT ZERO - measured, as a wide 0xFF splatter across kernel .text on the
; first primary swap of a VGA+Hercules machine, with [vid_ndisp] among the
; casualties.
; -----------------------------------------------------------------------------
vid_depth_set:
    cmp al, VID_VGA
    je .colour
    mov byte [vid_mono], 1      ; 1bpp: the software renderer IS the direct
    mov byte [vid_planes], 1    ; path (SPEC.md 39.5)
    mov word [vid_planes_w], 1
    ret
.colour:
    mov byte [vid_mono], 0
    mov byte [vid_planes], 4
    mov word [vid_planes_w], 4
    ret

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

    ; --- the DISPLAY's extent first, then the DESKTOP's from it (SPEC.md
    ; 39.2.1). On one display they are the same numbers, and vid_tab has just
    ; loaded vid_cw/vid_ch straight out of the adapter's row; a desktop that
    ; spans two cards computes vid_w/vid_h from the union instead, and only
    ; these six stores change.
    mov ax, [vid_cw]
    mov [vid_w], ax
    dec ax
    mov [vid_cwm1], ax
    sub ax, 7
    mov [vid_cwm8], ax
    mov ax, [vid_ch]
    mov [vid_h], ax
    dec ax
    mov [vid_chm1], ax
    sub ax, 7
    mov [vid_chm8], ax

    mov ax, [vid_w]             ; --- the CHROME's extent (SPEC.md 39.16): the
    mov [vid_pw], ax            ; display this call is publishing, which for
    dec ax                      ; the primary is what every strip measures
    mov [vid_pwm1], ax          ; itself against. vid_disp_init applies the
    mov ax, [vid_h]             ; PRIMARY last, so these end up its numbers,
    mov [vid_ph], ax            ; and vid_desk_union then grows vid_w/vid_h
    dec ax                      ; past them without touching these
    mov [vid_phm1], ax

    mov ax, [vid_w]             ; --- widths
    dec ax
    mov [vid_wm1], ax           ; these two are the DESKTOP's, and right to be:
    sub ax, 7                   ; the pointer clamps across the whole of it
    mov [vid_wm8], ax
    mov ax, [vid_pw]            ; ...AND THE CLOCK IS THE PRIMARY'S, because the
    dec ax                      ; MENU BAR is (SPEC.md 39.2.2). This was taken
    sub ax, 7                   ; from [vid_w] like the two above it, which is
    sub ax, 198                 ; w - 8 - MENU_CLK_W - 6, floored to a glyph
    and ax, 0FFF8h              ; cell: the bar's segments are cell ranges
    mov [vid_clk_hx], ax        ; the union once a second display is extended -
                                ; 1152 on a 1360px desktop, which is CELL 144
                                ; of a menu_bcell holding MENU_BCMAX = 62. The
                                ; overrun wrecks whatever follows it in .bss
                                ; and the machine dies somewhere else entirely,
                                ; with a stack pointer outside .lowbss. The
                                ; primary's 640 gives cell 54, inside the
                                ; buffer, which is the number this always meant

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

    mov ax, [vid_bmask]         ; gfx_prevrow's test: the bank number lives in
    mov cl, 13                  ; DI's bits 13..14, so the address mask is the
    shl ax, cl                  ; y mask shifted up to meet it. 0 on VGA,
    mov [vid_bankmask], ax      ; which is what makes prevrow a bare subtract

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

    mov al, [vid_kind]          ; --- the renderer's DEPTH, in one place
    call vid_depth_set          ; (see there: a display swap needs it too)
    xor ax, ax                  ; VGA: the renderer has no target until
    mov bx, SW_PLANE_PARA       ; unreachable: [vid_rseg] is 0 here
    xor dx, dx
    cmp byte [vid_kind], VID_VGA
    je .rend
    mov ax, [vid_seg]           ; the software renderer draws straight into
    mov bx, 1                   ; the framebuffer - one "plane", and a step
    mov dx, ax                  ; of 1 paragraph purely so sw_xfer's segment
    inc dx                      ; compare can terminate
.rend:
    mov [vid_rseg], ax
    mov [vid_rpara], bx
    mov [vid_rend], dx

    mov ax, [vid_w]             ; --- home the cursor; see the header
    shr ax, 1
    mov [mouse_x], ax
    mov ax, [vid_h]
    shr ax, 1
    mov [mouse_y], ax
%ifdef KERN_BIG
    mov byte [cur_disp], 0      ; ...onto display 0, which is where the middle
                                ; of the desktop is. Saying so matters: a stale
                                ; [cur_disp] sends cur_move's jump path to
                                ; ERASE a cell on a display the arrow was never
                                ; drawn on, and a save-under put back in the
                                ; wrong place smears (SPEC.md 39.15.2/7.1.2).
                                ; A store to a label in mouse.inc, never a call
                                ; into it - this routine runs from the splash
                                ; while the rest of the kernel is still coming
                                ; off the floppy, which is what the header
                                ; above says about cur_unlazy
%endif

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_6845_prog - write R0..R11 of the mono card's CRTC from a 12-byte table
;
; in:       SI = the table, R0 first
; out:      -
; clobbers: AX, DX, SI, flags - DOCUMENTED rather than preserved, because both
;           callers are mode sets that reload everything they use immediately
;           afterwards, and this is an out-loop on a 4.77MHz machine.
;
; Factored out of vid_setmode when vid_mono_text needed the identical loop with
; a different table (SPEC.md 39.20). Two tables and one loop rather than two of
; each, for gfxbench's reason: the second copy is the one that does not get the
; fix. It leaves DX = 0x3B4, which neither caller relies on.
; -----------------------------------------------------------------------------
vid_6845_prog:
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
; is ours to do; the first desktop paint assumes a
; cleared framebuffer. The explicit `cld` matters: on the splash path this
; runs on the boot sector's flags, and spl_tick never issues one.
;
; THE HERCULES SEQUENCE BLANKS FIRST AND UNBLANKS LAST, and that ordering is
; the whole point of the routine rather than a tidiness (SPEC.md 39.6). The
; obvious order - configure, program the 6845, enable video, then clear - has
; the card DISPLAYING for the whole length of the clear, and what it displays
; is 32KB nobody has written: the BIOS's MDA text page reinterpreted as a
; bitmap, and above it the card's own DRAM as it powered up. That is a bright,
; roughly half-lit 720x348 field, and the clear is a `rep stosw` of 16,384
; words on a 4.77MHz 8088 - tens of milliseconds, several whole frames at
; 50Hz. On an IBM 5151 the step from a mostly-black text page to a half-lit
; screen and back is a beam-current swing the flyback answers with an audible
; pop, and the eye reads as a garbled flash immediately before the progress
; bar. Blanked, the same 32KB is cleared with the tube dark and the first
; thing the monitor is ever shown in graphics mode is black.
;
; Clearing 3B8h bit 3 gates the VIDEO signal only - the 6845 keeps running and
; both syncs keep coming out - so blanking costs the monitor nothing. The
; sync transient while R0..R11 are half-written is unavoidable on a 6845 (its
; registers are not double-buffered; every mode set in the machine's history
; has one), but it is a single mistimed frame with the drive never stopping,
; and behind the blank it is invisible.
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
    call vid_cga_equip          ; EITHER COLOUR CARD, NOT JUST THE CGA (SPEC.md
                                ; 39.19.4). The XT ROM's mode set is
                                ; EQUIPMENT-driven: with 40:10 saying mono it
                                ; forces mode 7 and the 3B4h CRTC whatever mode
                                ; was asked for, so `int 10h AX=0012h` on a
                                ; HERCULES-PRIMARY machine programmed the
                                ; Hercules into 80x25 text over its own graphics
                                ; framebuffer and never touched the VGA at all.
                                ; This call was below the VID_CGA test, so only
                                ; the CGA arm got the treatment - and see there:
                                ; it is a no-op unless the flag says mono, which
                                ; is why every colour-primary machine is
                                ; unaffected by moving it up
    mov ax, 0x0012              ; VGA: 640x480x16 planar
    cmp byte [vid_kind], VID_CGA
    jne .bios

    ; --- ZERO B800 WHILE THE CARD IS STILL IN TEXT MODE ---------------------
    ; The Hercules path below blanks the tube for its whole mode change and
    ; this one cannot: int 10h owns the CGA's mode control register, so there
    ; is no window we can hold the card dark across. What there IS is the
    ; buffer, and it is the same buffer either way - B8000 is the text screen
    ; now and the bitmap in a moment.
    ;
    ; So the garbage is removed rather than hidden. Whatever the ROM's order
    ; is - and every BIOS has its own - the bytes it switches the card onto
    ; are already zero, so there is no frame in which the previous TEXT screen
    ; is displayed as a 640x200 bitmap. That frame is what "the graphics
    ; memory shows garbage just before the progress bar" is: mode 3's clear
    ; leaves 0x0720 per cell, which as pixels is a regular field of dots, and
    ; a hard-disk boot leaves the POST's own text, which is not regular at
    ; all. NO EMULATOR HERE CAN SHOW IT - they clear instantly - and on a
    ; 4.77MHz machine the ROM's own 16KB clear is tens of milliseconds.
    mov ax, 0xB800
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 8192
    cld
    rep stosw

    mov ax, 0x0006              ; CGA: 640x200 monochrome
.bios:
    int 0x10
    jmp short .done
.herc:
    mov dx, 0x3BF               ; configuration: graphics allowed. Bit 1 - the
    mov al, 1                   ; second 32KB page - is deliberately NOT set:
    out dx, al                  ; that page is at B8000, where a CGA in the
                                ; same machine lives, and this 5150 has both
                                ; cards in it. We only ever address page 0
                                ; (max offset 7E96h), so enabling the second
                                ; decode buys nothing and puts two cards on
                                ; the bus for the same addresses.
    mov dx, 0x3B8               ; graphics + page 0, video DISABLED (bit 3
    mov al, 0x02                ; clear). From here to the last write of this
    out dx, al                  ; routine the tube is dark - see the header.
    mov si, vid_6845
    call vid_6845_prog
    mov ax, [vid_seg]           ; 32KB clear - nobody else will do it, and it
    mov es, ax                  ; happens BEHIND the blank, so the powered-up
    xor di, di                  ; contents of the card's DRAM are never shown
    xor ax, ax
    mov cx, 0x4000
    cld
    rep stosw
    mov dx, 0x3B8               ; graphics, page 0, video ON - last, onto a
    mov al, 0x0A                ; framebuffer that is already black
    out dx, al
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
; vid_cga_equip - tell the BIOS this is a colour machine, if it thinks it is not
;
; in:       -
; out:      -
; clobbers: flags
;
; THE PC/XT BIOS's MODE SET IS EQUIPMENT-DRIVEN, NOT MODE-DRIVEN. It reads bits
; 5:4 of the equipment flag at 40:10 and, when they say 11b (a monochrome card
; at B000), forces mode 7 and the 3B4h CRTC whatever mode was asked for. So on
; a machine with BOTH cards and its motherboard switches set to mono - which is
; every machine a `VIDEO=cga` build exists for, since the reason to force CGA is
; that the probe found the Hercules first - `int 10h AX=0006h` programs the MDA
; into 80x25 TEXT and never touches the CGA at all.
;
; The symptom is exact and was field-reported: the mono monitor shows a cleared
; text page with a blinking cursor (that IS the mode 7 set), the colour monitor
; shows nothing at all (its card was never programmed, so it has no sync), and
; the machine is otherwise perfectly alive - it finishes the boot read and
; answers Ctrl-Alt-Del. os8088 then draws into a B8000 nobody initialised.
;
; Setting the flag to colour first is what DOS's MODE CO80 does and is the only
; way to reach the second card. Only when it says mono: a machine that already
; knows it has a CGA is left exactly as it was, which is every auto-detected
; machine by construction - vid_detect only answers VID_CGA when these two bits
; are NOT 11b, so on the probe path this routine reads them and returns.
;
; The flag is left colour afterwards on purpose. From here on the CGA is the
; machine's display, and CMD_REBOOT's vid_text (int 10h AX=0003h) has to land
; on the same card the user has been looking at.
; -----------------------------------------------------------------------------
vid_cga_equip:
    push ax
    push ds
    mov ax, 0x0040              ; the BIOS data area
    mov ds, ax
    mov al, [0x0010]            ; equipment flag, bits 5:4 = the display
    and al, 0x30
    cmp al, 0x30                ; 11b = monochrome at B000
    jne .out
    and byte [0x0010], 0xCF
    or byte [0x0010], 0x20      ; 10b = 80x25 colour
.out:
    pop ds
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
; it - but the MODE SET is now skipped while the splash is still on screen.
; It used to run unconditionally, and re-setting a mode that is already set
; is what wiped the loading screen: kmain then spent several more seconds
; mounting A:, reading SYSTEM.CFG and painting the desktop with the user
; looking at a blank screen and a progress bar that had been erased at 100%.
; The splash owns the mode from its first tick until spl_finish, which is the
; line immediately before the first wm_paint_all (SPEC.md 15.3/39.1).
;
; The probe and the publish still re-run, and must: everything between here
; and that paint reads [vid_w]/[vid_h]/[vid_stride] out of them.
; -----------------------------------------------------------------------------
vid_init:
    call vid_detect
    call vid_apply
    cmp byte [spl_live], 0      ; the splash set THIS mode on its first tick
    jne .keep                   ; and is still drawing on it
    call vid_setmode
.keep:
    ret

; -----------------------------------------------------------------------------
; vid_text - back to a text mode (CMD_REBOOT and fsx's FSXM_TEXT80)
; in:       -
; out:      -
; clobbers: flags
;
; IT NAMES THE CARD NOW, AND int 10h CANNOT (SPEC.md 39.20). Mode 7 is "mono
; text" and on a machine with one mono card that is the same statement as
; "program the MDA" - which is every machine this had until a VGA could sit
; beside a Hercules. A VGA's option ROM takes int 10h over at POST and its
; mode 7 is the VGA's OWN mono text: aperture at B0000, CRTC at 3B4, both
; directly on top of the real Hercules. Two cards driving one address space is
; exactly what vid_setmode leaves 3BFh bit 1 clear to avoid, arrived at from
; the other end.
;
; vid_equip is the fix for the same shape one machine class down and it does
; not reach this one, which is worth reading its header for: on Hercules+CGA
; the flag at 40:10 names the card because an XT ROM's mode set is
; equipment-driven. A VGA BIOS keeps its own state and is not talked out of
; itself by a flag - so the only way to program the mono card on such a
; machine is to program the mono card.
;
; vid_mono_text is that, and it answers CF = 1 for "done, do not call int 10h
; at all". The blank and the graphics lock-out below stay in front of both
; paths: they are this card's registers either way, and the ROM's mode 7
; reprograms the 6845 with video live otherwise.
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
    xor al, al                  ; blank before anything else moves, for the
    mov dx, 0x3B8               ; same reason vid_setmode does: the BIOS mode
    out dx, al                  ; 7 set below reprograms the 6845 with video
    mov dx, 0x3BF               ; live otherwise. Then lock graphics back out
    out dx, al                  ; (AL is still 0) and let mode 7 re-enable.
    call vid_mono_text          ; ...unless int 10h would land on a VGA, in
    jc .out                     ; which case that call has just done it here
    mov ax, 0x0007
    jmp short .go
.colour:
    mov ax, 0x0003
.go:
    int 0x10
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
; gfx_rowbase - byte offset of the first byte of a scan line (SPEC.md 39.3)
;
; in:       AX = y
; out:      AX = offset into the framebuffer
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
    shl bx, 1
    mov bx, [cs:bx + vid_banktab]   ; BX = bank * 0x2000, without the 13-bit
    mov cl, [cs:vid_bshift]         ; shift an 8088 charges 60 clocks for
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
; else entirely: sw_xfer's save path sets DS to the framebuffer segment for
; its movsb, and its restore path sets DS to the caller's buffer (SAVE_SEG
; for a menu's save-under). Reading the step through DS there fetches
; framebuffer bytes as a scan-line stride. CS is KERNEL_SEG for everything
; in .text - every byte of kernel code, since SPEC.md 33 - so the
; override is always right and costs one byte per reference.
; -----------------------------------------------------------------------------
gfx_nextrow:
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .r
    add di, [cs:vid_wrapfix]
.r:
    ret

; -----------------------------------------------------------------------------
; gfx_prevrow - the mirror: DI on row y becomes DI on row y-1
;
; in/out/clobbers: exactly gfx_nextrow's, DI and flags and nothing else.
;
; It exists for SPEC.md 5.6.7's resumable walk, which runs in the CALLER's
; direction rather than gfx_line's normalised downward one - a missile trail
; is drawn from the launcher outward and an ABM launches upward, so half of
; them step y the other way. Everything else in this kernel walks down.
;
; The test is on the bank field BEFORE the subtract, which is where this
; differs from its mirror: nextrow can see its own overflow in the result,
; and a borrow out of bank 0 is only visible in the operand. bankmask is
; (nbanks-1) << 13, so it is 0 on VGA and `test di,0` sets ZF - the wrapfix
; there is 0 too, so the whole thing is `sub di,stride` with no adapter test,
; exactly as nextrow is an unconditional add.
; -----------------------------------------------------------------------------
gfx_prevrow:
    test di, [cs:vid_bankmask]  ; already in bank 0? then this step borrows
    jnz .p
    sub di, [cs:vid_wrapfix]    ; ...so undo the wrap nextrow would have added
.p:
    sub di, [cs:vid_rowadd]
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
