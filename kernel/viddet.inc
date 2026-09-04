; =============================================================================
; os8088 - viddet.inc
;
; Video adapter detection, runtime screen geometry, and the two addressing
; helpers every drawing path in the kernel goes through (SPEC.md 39).
;
; One kernel binary drives four adapters, chosen at boot in this order:
;
;   VID_VGA    640x480, 16 colours, 4 planes at A000:0000  (mode 12h)
;   VID_HERC   720x348, monochrome, 4 banks at B000:0000   (no BIOS mode)
;   VID_CGA    640x200, monochrome, 2 banks at B800:0000   (mode 6)
;   VID_EGA    640x350, 16 colours, 4 planes at A000:0000  (mode 10h)
;
; VID_EGA is VID_VGA's row of vid_tab with a shorter height and a different
; BIOS mode number - the planar framebuffer is byte-for-byte the same shape.
; Everything in this file that is not detection or the mode set treats it
; exactly as VGA, because [vid_mono] is 0 for both (SPEC.md 39.24,
; docs/EGA-PLAN.md).
;
; The two mono fallbacks are both 1 bit per pixel with a BANKED framebuffer -
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

; ...AND `VIDEO=vga` IS A BUILD ERROR ON kern_small, not a runtime surprise.
; That build carries no VGA renderer at all (docs/MONO-RECLAIM-PLAN.md 2), so
; forcing VID_VGA would set [vid_kind] to a kind vid_setmode no longer has an
; arm for: the card would be programmed to BIOS mode 6 by the CGA arm below
; while vid_apply loaded the VGA row's geometry over it - 640x480 at A000 on a
; card showing 640x200 at B800. Every pixel would land somewhere else, and
; nothing would say so. The AUTO-detected case is fine and is the whole design
; (2.4): a VGA machine falls through the gated probes to the equipment word
; and comes up as a CGA, geometry and all. It is only the FORCE that lies.
; ...and VIDEO=ega is the same lie one kind along: the EGA is the other
; 4-plane kind (SPEC.md 39.24), driven by the same renderer, and forcing it
; here set mode 10h, loaded the A000 row and then took every primitive's
; no-VGA arm through a planar [vid_rseg] of 0 - the IVT.
%if VID_FORCE == 1 || VID_FORCE == 4
 %ifndef GFX_VGA
%error "VIDEO=vga/ega needs a build with a VGA renderer in it - kern_small has none (docs/MONO-RECLAIM-PLAN.md 2). Auto-detect drives a VGA or EGA card as a CGA there; VIDEO=cga forces the same thing."
 %endif
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
VID_EGA     equ 3              ; VGA's planar path at 640x350 (SPEC.md 39.24,
                               ; docs/EGA-PLAN.md). NOT a fourth renderer:
                               ; mode 10h is mode 12h's framebuffer, shorter -
                               ; [vid_mono] = 0, vga12.inc reused unchanged.

EGA_H       equ 350            ; mode 10h's active rows. Only vid_setmode's own
                               ; framebuffer clear needs it as a constant (the
                               ; live height is [vid_ch], 350, from vid_tab);
                               ; that clear runs from the splash, gated on
                               ; SPL_RESIDENT, so it stays on a constant like
                               ; the VGA arm's SCREEN_H (SPEC.md 39.23).

VID_H_MIN   equ 200            ; ...and the SHORTEST row of vid_tab, which is
                               ; the CGA's. EGA_H names the tallest and this
                               ; names the other end, because two build-time
                               ; guards ask "does everything still fit the
                               ; smallest screen?" and both used to ask it of
                               ; a LITERAL 200 sitting next to the table
                               ; rather than of the table: a fifth row shorter
                               ; than this one would leave them silent. The
                               ; guards are the intra-bank assertion at the
                               ; foot of this file and the MENU_POPMAX one in
                               ; menu.inc, and vid_tab's CGA row below is
                               ; written from it.

VID_KIND_MAX equ VID_EGA        ; the highest kind there is - vid_avail_test's
                                ; whole range check is one compare against it,
                                ; and a fourth adapter is what raises it.
                                ; **EGA IS THAT FOURTH ADAPTER**, so this line
                                ; moved from VID_CGA when 39.24 landed: left
                                ; at 2 the range check refuses kind 3 and an
                                ; EGA is unselectable, with nothing failing to
                                ; assemble to say so.

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
    dw  0xB800,      80,    1,     1, 0x2000,  0x4000, 0xC050, 640, VID_H_MIN
    dw VGA_SEG,      80,    0,     0,     80,       0,      0, 640, EGA_H
    ; ^ VID_EGA: byte-for-byte the VGA row but 350 tall. bmask/bshift/wrapbit
    ;   all 0, so gfx_rowbase is y*80 and gfx_nextrow is `add di,80` with no
    ;   adapter test, exactly as on VGA (SPEC.md 39.3/39.24). 349*80 = 27,920,
    ;   well inside one 0x8000 plane, so nothing here needs the intra-bank
    ;   assertion the two mono rows do.

; -----------------------------------------------------------------------------
; The live geometry block (SPEC.md 39.2). NINE CONTIGUOUS WORDS, in vid_tab's
; column order - vid_apply rep movsw's a record straight over them, so do not
; reorder one without the other. Initialized to the VGA row so that anything
; reading them before vid_apply runs sees the historical constants.
;
; IT IS THE FIRST HALF OF A LONGER CONTIGUOUS RUN, and that is now load-bearing
; rather than incidental. `vid_seg` through `vid_tseg` - the nine live words,
; the six derived ones below them and font_run's composed-row target - are the
; SIXTEEN words A DISPLAY OWNS, and SPEC.md 39.12's per-display context copies
; exactly that span with one `rep movsw`. Everything from `vid_dock_y0` down is
; the DESKTOP's and must stay out of it. `vid_tab` used to sit between the two
; halves, which is why it has moved up here: it is a static table and it was
; splitting a run that wants to be one.
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
vid_rseg    dw 0                ; the software renderer's target segment:
                                ; 0 on VGA, where nothing routes through the
                                ; software renderer; the FRAMEBUFFER itself
                                ; on mono
vid_cwm1    dw SCREEN_W-1       ; the display's last legal x and y, and the
vid_chm1    dw SCREEN_H-1       ; whole-cell limits font_char and ico_core
vid_cwm8    dw SCREEN_W-8       ; clip against. THE LAST WORD OF THE RUN a
vid_chm8    dw SCREEN_H-8       ; display owns - vidsel.inc asserts it.
vid_tseg    dw VGA_SEG          ; THE SEGMENT A COMPOSED ROW IS WRITTEN TO
                                ; (SPEC.md 6.1.10): [vid_rseg] on the mono
                                ; adapters, where the software renderer's
                                ; framebuffer IS the screen, and [vid_seg] on
                                ; VGA, where vid_rseg is 0 and the planar
                                ; aperture is the target. font_run's prologue
                                ; is charged to every string this system
                                ; draws, so the choice is made ONCE per
                                ; display and not once per run.
                                ;
                                ; IT IS THE LAST WORD OF THE PER-DISPLAY RUN,
                                ; and it EXTENDED that run rather than joining
                                ; it in the middle: vidsel.inc's VID_CTX_W and
                                ; its %error are the second opinion, so a
                                ; seventeenth word put anywhere above this
                                ; fails the build rather than being silently
                                ; left out of the run that gets copied.

; --- THE VIRTUAL DESKTOP (SPEC.md 39.2.1) ------------------------------------
; What the WINDOW SYSTEM lays out in, and on a one-display machine identical to
; the four words above it. The distinction only becomes visible when a desktop
; spans two cards, and it is drawn HERE - outside the per-display run - because
; the alternative was renaming these five at sixty-odd call sites across wm.inc,
; ui.inc, menu.inc, desk.inc and dock.inc. The renderer's side of the same
; question is twenty-two sites and enumerable, so the renderer is what moved.
vid_w       dw SCREEN_W         ; desktop width, px
vid_h       dw SCREEN_H         ; desktop height, px
vid_wm1     dw SCREEN_W-1       ; last legal x
vid_hm1     dw SCREEN_H-1       ; last legal y
vid_wm8     dw SCREEN_W-8       ; the menu bar's right margin. Its twin
                                ; vid_hm8 had no reader in the tree and
                                ; has gone; nothing measures a whole cell
                                ; against the DESKTOP's height

; vid_apply COPIES the display's three derived words onto the desktop's three
; with one `rep movsw`, so the two runs' ORDER is load-bearing and nothing
; else in the tree asserts it. A swapped `wm1`/`hm1` would show on the glass
; at once (w != h on all four adapters); a swapped -8 pair would not - one is
; the menu bar's right margin and the other has no reader at all - so these
; two lines are the only possible gate. font.inc's font_zero run is the
; precedent.
%if vid_chm1-vid_cwm1 != 2 || vid_cwm8-vid_cwm1 != 4 || vid_chm8-vid_cwm1 != 6
  %error "the display's m1/m8 run has moved - vid_apply copies it wholesale"
%endif
%if vid_hm1-vid_wm1 != 2 || vid_wm8-vid_wm1 != 4
  %error "the desktop's m1/m8 run has moved - see the copy in vid_apply. It is THREE words: a fourth writes ch-8 into vid_pw on every vid_apply"
%endif
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
vid_clk_hx  dw (SCREEN_W-(MENU_CLK_W+14)) & 0FFF8h
                                ; menu bar clock cell: the hit band's left
                                ; edge, and the left edge of the clock's own
                                ; SEGMENT (SPEC.md 12.1) - on a GLYPH CELL
                                ; boundary, which is what lets the bar's three
                                ; segments be whole numbers of 8px cells and
                                ; font_run's single-pass path reach them
vid_ymax    dw SCREEN_H-18      ; lowest y a title bar may be dragged to
vid_popmax  dw MENU_POPMAX      ; pull-down items that fit under the bar, and
                                ; a CONSTANT: vid_apply used to compute
                                ; min((h-22)>>4, MENU_POPMAX) and every
                                ; adapter in vid_tab clamped. It was 16 here
                                ; and 16 is a value the machine cannot
                                ; survive - menu.inc's own save-under guard
                                ; proves 16 items need 28,896 bytes of a
                                ; 20,480-byte claim - which only never fired
                                ; because vid_apply overwrote it on the
                                ; splash's first tick, long before a menu.
                                ; The %error beside MENU_POPMAX is the guard
vid_desk_zx dw SCREEN_W-56      ; desktop drive column: the hit zone's left
                                ; edge, and the ONLY word of it that is
                                ; published. The drawn rect either side of it
                                ; is desk_zone_rect's arithmetic over
                                ; DESK_ZW/DESK_ZOVER (SPEC.md 26.4) - two
                                ; further cells carrying zx-2 and zx+50 were
                                ; written here every vid_apply and read by
                                ; nothing at all.

vid_kind    db VID_VGA          ; VID_VGA / VID_HERC / VID_CGA / VID_EGA
vid_mono    db 0                ; 1bpp adapter? This is what routes every
                                ; draw through the software renderer (32/39.5)
vid_planes  db 4                ; 4 or 1
vid_planes_w dw 4               ; the same, where a word is wanted

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

; SPEC.md 39.3.1 - gfx_rowbase's table. 348 rows, WHICH IS EVERY ROW OF A
; HERCULES and therefore every row of both 1bpp adapters: a CGA is 200 and a
; Hercules 348, so on the target machine this table never misses.
;
; It was 128 - what 256 bytes of the image rung would carry - and PERFORMANCE.md
; Set 106 is what moved it. 128 rows is the top 64% of a CGA and the top 37% of
; a Hercules, and the profiles said what that costs where an application
; actually draws: the screen saver spent 2.08% of the machine in
; gfx_rowbase_calc on CGA and 6.39% on VGA, and Missile Command 1.40% and 1.68%,
; because a game's window is in the bottom third of the screen. At 348 the CGA
; miss path is 0.00% - the saver 4.69% -> 3.14% of the machine, Missile Command
; 1.66% -> 0.69%.
;
; 200 AND 348 COST THE SAME 512-BYTE RUNG, which is why this is not 200: the
; low rung rounds either to 9,728 bytes, so covering a Hercules as well as a
; CGA is free once the step is taken.
;
; VGA IS 480 AND STILL MISSES, deliberately. 348 of 480 is 72.5% and takes the
; saver from 8.18% to 4.22%; the remaining 132 rows are a SECOND rung and buy
; VGA alone (4.22% -> 3.13%). The target machine is an IBM PC/XT (PERFORMANCE.md
; Part 1), so the first rung is spent where the target is and the second is
; priced in Set 106 and not taken. A row past the end takes the arithmetic,
; which is why the cap is a number here and not a contract anywhere else.
;
; ...AND IT IS kern_big's ALONE, which is WM_ANIM's and OS88_THEME's shape in
; kernel.asm and for their reason. The step is one 512-byte rung of the low
; rung either way - 200 rows crosses it too - so a smaller table would not
; rescue the small build, and kern_small's is the figure that has to be
; DEFENDED (it is the 128KB machine's). It keeps the 128-row table and its
; budget; the feature is the fast path being wider, not the fast path existing.
;
; SPEC.md 39.27's RECLAIM DOES NOT CHANGE THIS, and the temptation is worth
; naming because it is obvious and wrong: taking the VGA renderer out of
; kern_small returned four 512-byte rungs, so widening this table there looks
; free. It is not. Those bytes are not slack to spend - they are the first
; instalment of a much larger cut that build is under (SPEC.md 39.27.4), and a
; reclaim spent the moment it lands is a reclaim that never happened. This was
; built, measured at exactly the predicted +512, and REVERTED.
%ifdef KERN_BIG
VID_ROWTAB  equ 348
%else
VID_ROWTAB  equ 128
%endif

; ...and HOW MANY of its rows are filled: 0 until vid_apply has built it, and
; VID_ROWTAB after. One word, so gfx_rowbase's "is there a table" and "is this
; row in it" are the same compare.
;
; IT IS ALSO THE SWITCH THAT TURNS THE TABLE OFF ON THE SECOND DISPLAY. Neither
; this word nor the table below is inside SPEC.md 39.12's sixteen-word run -
; one is .text and the other .lowbss - while the stride, the bank mask and the
; bank shift it was built from all are. So vid_ctx_act zeroes this when it makes
; current a display whose adapter is not [vid_kind]'s and puts VID_ROWTAB back
; when that display returns; see there for why the alternative - rebuilding, at
; 23.6 ms - is not affordable under a drawing primitive.
;
; .text with a REAL initialiser, not .bss: -f bin zeroes nothing, and
; gfx_rowbase is called by the SPLASH - which draws while vid_apply has not run
; and the table is whatever the last boot left in that RAM. This zero is the
; only thing between that and a progress bar drawn at a random offset.
vid_rowmax  dw 0

section .lowbss                 ; LOW_SEG (SPEC.md 2.1), and not .bss - 696
vid_rowtab  resw VID_ROWTAB     ; bytes in the image rung would cross a step

section .text                   ; AND spend KERN_CODE_MAX, which is the guard
                                ; that cannot be raised. Here it costs one
                                ; 512-byte rung of KERN_BUDGET and nothing of
                                ; the segment. It costs nothing to REACH
                                ; either: SS IS LOW_SEG for all kernel code
                                ; (SPEC.md 1), so `[ss:bx + tab]` is the same
                                ; one override byte `[cs:...]` would have been.
                                ; font_glyphs is read exactly this way and for
                                ; exactly this reason.

; -----------------------------------------------------------------------------
; vid_detect - decide which adapter is installed (SPEC.md 39.1)
;
; in:       -
; out:      [vid_kind]
; clobbers: flags
;
; Order is binding.
;
; Step 1 (int 10h AX=1A00h, the Display Combination Code) answers for every
; VGA and MCGA - the DCC call is part of the VGA BIOS - so a card that
; answers step 2 (int 10h AH=12h "get EGA info") and NOT step 1 is an EGA.
; An IBM EGA has no mode 12h, only mode 10h (640x350), so it gets its own
; kind, VID_EGA, which is VGA's planar path at that height (SPEC.md 39.24).
;
; The equipment word is consulted LAST because an EGA or VGA driving a
; monochrome monitor also reports bits 5:4 = 11b. Those machines belong on
; the planar path: a VGA on step 1, an EGA on step 2, and only a machine that
; answers neither is a real mono card.
;
; Documented scope cut: no Hercules-versus-plain-MDA discrimination (the
; 0x3BA vertical-sync toggle test). A plain MDA is text-only, so there is no
; better action available; driving it as a Hercules is strictly less wrong
; than driving it as a CGA at B800.
;
; Documented scope cut: a VGA whose BIOS does not implement the DCC call is
; classed VID_EGA (640x350) rather than VID_VGA. Such a card is vanishingly
; rare - the DCC is in the 1987 VGA BIOS spec - and VID_FORCE=1 is the
; escape hatch. The target is period XT hardware, where a real EGA is the
; common case this trades for.
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
%if VID_FORCE != 0              ; SPEC.md 39.1: the override is applied at
    mov al, VID_FORCE - 1       ; ASSEMBLY time, so a shipped kernel carries
    jmp short .set              ; neither the test nor a byte to test. The
%endif                          ; 1/2/3/4 the Makefile passes are
.probe:                         ; VID_VGA/VID_HERC/VID_CGA/VID_EGA
%ifdef GFX_VGA
    mov ax, 0x1A00              ; VGA/MCGA: read Display Combination Code.
    int 0x10                    ; A pre-VGA BIOS ignores AH=1Ah and leaves
    cmp al, 0x1A                ; AL as we set it, which is 00h.
    je .vga
    mov ah, 0x12                ; "get EGA info" overwrites BL with the card's
    mov bl, 0x10                ; memory size. An XT BIOS leaves 10h alone.
    int 0x10                    ; Step 1 already claimed every VGA, so a card
    cmp bl, 0x10                ; that answers HERE is an EGA - and an EGA has
    je .equip                   ; no mode 12h, only mode 10h (SPEC.md 39.24).
    mov al, VID_EGA             ; ...BUT THE SAME CALL SAYS WHICH DISPLAY IT
    or bh, bh                   ; DRIVES, and it is free: BH = 0 colour (3Dx),
    jz .set                     ; 1 monochrome (3Bx). AL is set BEFORE the
                                ; test - `or bh,bh` does not touch it - which
                                ; deletes the two-instruction .ega island the
                                ; taken branch used to land on; on the
                                ; fall-through .equip's own `int 0x11`
                                ; overwrites AL, which is what the code below
                                ; already relies on. An EGA on a 5151 has
                                ; modes 7 and 0Fh and NOT 10h, so claiming it
                                ; would set a mode the card does not have and
                                ; then paint A000 - a BLANK TUBE, not a
                                ; degraded one. It falls through to the
                                ; equipment word instead (SPEC.md 39.1's
                                ; third scope cut).
%endif                          ; WITHOUT A VGA IN THE BUILD there is nothing
                                ; to ask - and that now covers the EGA probe
                                ; too, EGA being the same planar path: a card
                                ; this kernel cannot drive is not a card it
                                ; wants to find. The walk goes straight to the
                                ; equipment word, which a VGA or an EGA in
                                ; colour mode answers 10b - so the machine
                                ; comes up as a CGA and runs the card in BIOS
                                ; mode 6, which every VGA and EGA BIOS serves
                                ; (docs/MONO-RECLAIM-PLAN.md 2.4)
.equip:
    int 0x11                    ; equipment word, bits 5:4
    and al, 0x30
    cmp al, 0x30                ; 11b = a monochrome card at B000
    mov al, VID_CGA
    jne .set
    mov al, VID_HERC
%ifdef GFX_VGA
    jmp short .set
.vga:
    mov al, VID_VGA
%endif
.set:
    mov [vid_kind], al
    jmp vid_pop8                ; the shared epilogue - and so this routine's
                                ; push list is vid_setmode's. See vid_pop8.

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
; vid_apply. SPEC.md 39.12's context is the sixteen contiguous words from
; vid_seg to vid_tseg; [vid_mono] and the two plane counts sit above that run,
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
; THE THREE ARE ONE FACT AND ARE KEPT IN ONE PLACE. vid_depth_set is their
; only writer (tests/unit/t_invariants.py checks that from the host) and sets
; all three on both arms, because SPEC.md 39.26 flattened sw_rect_pl,
; sw_fill_pat, ico_pass_bb and font_char_bb on mono and planes AGREEING. Their
; layout is pinned as well as their writer: menu_save_kb's header already
; records what a word read of [vid_planes] picks up when [vid_planes_w] is the
; byte after it - 0x0404, a thousand planes, and a save-under claim bigger
; than the fixed one it replaced.
%if (vid_planes - vid_mono) != 1
  %error "[vid_mono] and [vid_planes] must stay adjacent, mono first: they are set together by vid_depth_set and read as one fact by the four bodies SPEC.md 39.26 flattened"
%endif
%if (vid_planes_w - vid_planes) != 1
  %error "[vid_planes_w] must follow [vid_planes]: menu_save_kb documents the 0x0404 a word read of the byte picks up, and that sentence is only true while the _w twin is the byte after it"
%endif
vid_depth_set:
    push ax                     ; the header says "clobbers: flags", and AL is
                                ; the argument two callers read back after the
                                ; call (vid_ctx_act's `cmp al, [vid_kind]` is
                                ; the tight one). Two bytes could be had by
                                ; dropping this pair; they are a contract
                                ; change and are not taken.
    cmp al, VID_VGA
    je .colour                  ; **ZF IS THE ANSWER AND IT SURVIVES TO THE
%ifdef GFX_VGA                  ; `jne` BELOW**, and without a VGA renderer
    cmp al, VID_EGA             ; the first compare is the whole answer.
%endif                          ; EGA is the same four-plane
.colour:                        ; 16-colour depth as VGA - mode 10h, just
                                ; shorter (SPEC.md 39.24) - so `colour` is
                                ; `al == VID_VGA || al == VID_EGA`, and the
                                ; two `mov`s between here and the test set no
                                ; flags. Do not put a flag-setting instruction
                                ; in this gap.
    mov ax, 0x0101              ; 1bpp: AL -> [vid_mono], AH -> [vid_planes].
    jne .st                     ; The software renderer IS the direct path
    mov ax, 0x0400              ; there (SPEC.md 39.5); a VGA is 0 and 4.
.st:
    mov [vid_mono], ax          ; ONE word store for the pair the %if above
                                ; pins side by side - so an ISR can no longer
                                ; observe mono=1 with planes=4 between two
                                ; byte writes, which the three stores it
                                ; replaces could show it
    mov al, ah
    cbw                         ; 1 or 4, so the sign bit is clear and AH
                                ; becomes 0. `cbw` is 8086.
    mov [vid_planes_w], ax
    pop ax
    ret

vid_apply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    push cs                     ; rep movsw writes through ES, and we may be
    pop es                      ; called from the splash, where ES is the
                                ; boot sector's

    mov al, [vid_kind]          ; AL * 18 -> the record. AH is 0 after the
    mov bl, VID_REC             ; mul (3 * 18 = 54 at worst), so the two
    mul bl                      ; accumulator forms below are the whole
    add ax, vid_tab             ; index: `add ax, imm16` is 3 bytes where
    xchg ax, si                 ; `add si, ax` is 2 but needs `mov si, imm16`
    mov di, vid_seg             ; in front of it, and `xchg ax, r16` is 1
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

    ; --- the DESKTOP's derived words, and they are the DISPLAY's own. This
    ; is a COPY for the same reason the chrome block below is: the display
    ; block immediately above stored [vid_w] from [vid_cw] and [vid_h] from
    ; [vid_ch], so `w-1` IS `cw-1` and `w-8` IS `cw-8` - identically, on every
    ; adapter, not approximately. `vid_cwm1, vid_chm1, vid_cwm8` (declared
    ; together above) is exactly what `vid_wm1, vid_hm1, vid_wm8` is about to
    ; become, and the %if beside the declarations is what pins that: a
    ; reordered pair would be invisible otherwise. THREE and not four -
    ; vid_hm8 had no reader and is gone, and a fourth word here would write
    ; ch-8 into vid_pw, the chrome width, on every call. ES = DS from the
    ; prologue, DF is clear (the vid_tab copy's own `cld`), and SI, DI and CX
    ; are all dead here.
    mov si, vid_cwm1
    mov di, vid_wm1
    mov cx, 3
    rep movsw

    xor ax, ax                  ; SPEC.md 39.3.1: from here to the table build
    mov [vid_rowmax], ax        ; at the bottom of this routine the geometry
                                ; and the table disagree, so nothing may read
                                ; it - gfx_rowbase computes instead. AX is
                                ; dead across the copy above and reloaded
                                ; immediately below, so the accumulator pair
                                ; is a byte cheaper than `mov word [m], 0`

    mov ax, [vid_h]             ; --- the heights that are NOT the display's
    sub ax, TITLE_H
    mov [vid_ymax], ax
    sub ax, DOCK_H - TITLE_H
    mov [vid_dock_y0], ax
    add ax, 3
    mov [vid_dock_ty0], ax

    ; --- the CHROME's extent (SPEC.md 39.16): the display this call is
    ; publishing, which for the primary is what every strip measures itself
    ; against. vid_disp_init applies the PRIMARY last, so these end up its
    ; numbers, and vid_desk_union then grows vid_w/vid_h past them without
    ; touching these.
    ;
    ; It is a COPY and not eight instructions because by this point the two
    ; runs are the same four numbers in the same order: `vid_w, vid_h,
    ; vid_wm1, vid_hm1` (declared together above) is exactly what `vid_pw,
    ; vid_ph, vid_pwm1, vid_phm1` is about to become.  That is why the block
    ; sits HERE and not where it used to, three screens up: vid_wm1 and
    ; vid_hm1 are the DESKTOP copy's above, so that copy has to have run.
    ; Nothing between the old position and this one reads any of the
    ; four - the only reader in the routine is the [vid_clk_hx] computation
    ; below, which moved down with it.  ES = DS from the prologue, DF is
    ; clear (the vid_tab copy's own `cld`, and nothing sets it after), and
    ; SI, DI and CX are all dead here.
    mov si, vid_w
    mov di, vid_pw
    mov cx, 4
    rep movsw

    mov ax, [vid_pw]            ; ...AND THE CLOCK IS THE PRIMARY'S, because the
    sub ax, MENU_CLK_W + 14     ; MENU BAR is (SPEC.md 39.2.1.2). One subtract:
                                ; w - 8 - MENU_CLK_W - 6, floored to a glyph
    and ax, 0FFF8h              ; cell: the bar's segments are cell ranges
    mov [vid_clk_hx], ax        ; the union once a second display is extended -
                                ; 1152 on a 1360px desktop, which is CELL 144
                                ; of a menu_bcell holding MENU_BCMAX = 62. The
                                ; overrun wrecks whatever follows it in .bss
                                ; and the machine dies somewhere else entirely,
                                ; with a stack pointer outside .lowbss. The
                                ; primary's 640 gives cell 54, inside the
                                ; buffer, which is the number this always meant

    mov ax, [vid_bmask]         ; gfx_prevrow's test: the bank number lives in
    mov cl, 13                  ; DI's bits 13..14, so the address mask is the
    shl ax, cl                  ; y mask shifted up to meet it. 0 on VGA,
    mov [vid_bankmask], ax      ; which is what makes prevrow a bare subtract

    mov ax, [vid_w]             ; --- the desktop's drive column, right-hung.
    sub ax, 56                  ; ONE word: desk_zone_rect derives the drawn
    mov [vid_desk_zx], ax       ; rect from it (SPEC.md 26.4)

    ; --- ...AND [vid_popmax] IS NOT COMPUTED HERE ANY MORE. Nine instructions
    ; worked out min((h - MBAR_H - 2) >> 4, MENU_POPMAX) from a [vid_h] that
    ; is always a vid_tab height - the display block above has just stored it
    ; from [vid_ch], and vid_desk_union runs AFTER this routine, never inside
    ; it - and vid_tab's four heights are 480, 348, VID_H_MIN and EGA_H, so
    ; the answers were 28, 20, 11 and 20 against a clamp of 11. The clamp
    ; fired on every adapter there is and the CGA landed exactly on it, so
    ; those nine instructions always computed the initialiser. It is the
    ; DECLARATION's resting value now, and the %error beside MENU_POPMAX in
    ; menu.inc watches the three facts that could stop that being true.

    mov al, [vid_kind]          ; --- the renderer's DEPTH, in one place
    call vid_depth_set          ; (see there: a display swap needs it too)
    xor ax, ax                  ; PLANAR (VGA or EGA): the software renderer
    cmp byte [vid_mono], 0      ; has no target - 0, because nothing routes
    je .rend                    ; through it (SPEC.md 39.5/39.24). Keyed on
    mov ax, [vid_seg]           ; the DEPTH vid_depth_set has just set and not
.rend:                          ; on the kind - EGA is planar and 0 here too.
    mov [vid_rseg], ax          ; MONO: the renderer draws straight into the
                                ; framebuffer, so its target IS the screen.
                                ; This set a plane STEP and an exclusive END
                                ; beside it until both were found to have no
                                ; reader anywhere (SPEC.md 39.26)

    ; --- font_run's target (SPEC.md 6.1.10), and it belongs HERE and not one
    ; line above the block it reads. It IS [vid_seg] on every adapter there
    ; is - [vid_rseg] three lines up is `mono ? [vid_seg] : 0`, so the
    ; conditional this used to be read `mono ? [vid_seg] : [vid_seg]` - and
    ; the ordering trap the block below records is why it still reads the
    ; LIVE word rather than a copy taken earlier. A copy taken earlier in
    ; vid_apply is the PREVIOUS display's, which on a one-card machine is the
    ; same number every time and on a two-card machine is the other monitor's
    ; framebuffer. That is not a theoretical ordering: it shipped for the
    ; length of one test run and tests/dispcheck.py caught it as a 249-pixel
    ; solid run on a secondary that should have been nothing but 50% dither.
    ; (It was NOT always [vid_seg]: SPEC.md 32's renderer had an off-screen
    ; target and vidsel.inc's sw_set wrote [vid_rseg] from inside the run.
    ; That renderer is gone, and the cell stays published because font.inc
    ; reads it once per composed row.)
    mov ax, [vid_seg]
    mov [vid_tseg], ax

    ; --- gfx_rowbase's answer, laid down instead of computed (SPEC.md 39.3.1)
    ; Every input to a row's base address - the stride, the bank mask and the
    ; bank shift - is decided by the lines above and cannot change again until
    ; this routine runs. So the answers go in a table here rather than being
    ; recomputed on every drawing call in the machine.
    ;
    ; VID_ROWTAB rows and no more - 348, every row of both 1bpp adapters, and
    ; 72.5% of a VGA's 480. A row past it takes the arithmetic path exactly as
    ; before, so this is a fast path and not a new contract.
    ;
    ; IT IS BUILT BY ITERATING THE ANSWER, not by computing each one.
    ; gfx_nextrow IS the step form of gfx_rowbase_calc - SPEC.md 39.3 requires
    ; the two to agree, one being the other's increment - so the table can be
    ; walked with three instructions a row instead of a bank AND, a table
    ; index, a shift and a `mul`. It used to be ~324 cycles a row, 113,000
    ; cycles and 23.6 ms of every vid_apply; the step form is roughly a fifth
    ; of that. Checked against the table's own numbers on all four adapters
    ; rather than assumed: Hercules (rowadd 0x2000, wrapbit 0x8000, wrapfix
    ; 0x805A) gives 0, 0x2000, 0x4000, 0x6000, then 0x8000 trips the wrap bit
    ; and +0x805A truncates to 0x005A = 90 = bank 0 row 1; CGA (0x2000 /
    ; 0x4000 / 0xC050) gives 0, 0x2000, then 0x0050 = 80; VGA and EGA have
    ; wrapbit 0, so `test di,0` sets ZF and it is `add di,80`, with 347*80 =
    ; 27,760 nowhere near a wrap. Row 0 is 0 on all four, which is what lets
    ; the walk start from a bare `xor`.
    ;
    ; ...AND ON CGA THE TWO FORMS PART COMPANY AT ROW 207, which is measured
    ; and not a risk: the whole 348-entry table was read out of a booted guest
    ; on both builders and diffed. Hercules, VGA and EGA are byte-identical
    ; over all 348; CGA differs on 71 entries, every one of them row 207 or
    ; above. The reason is the assertion at the foot of this file - it bounds
    ; the intra-bank extent for VID_H_MIN rows, `(200/2)*80 = 8,000` inside
    ; 0x2000, and row 207 is `(207>>1)*80 = 8,240`, past it. So above row 206
    ; a CGA table entry has been meaningless in EVERY build this OS has ever
    ; had; the two builders merely disagree about which meaningless number to
    ; store, and where they do, `gfx_rowbase_calc`'s 16,432 is 48 bytes past
    ; the end of the card's 16KB aperture while `gfx_nextrow`'s 128 is inside
    ; it.
    ;
    ; NOTHING CAN INDEX THEM. Every primitive clips y to [vid_ch] (SPEC.md
    ; 39.2.1), which is 200 on a CGA; and when a CGA is the SECONDARY of a
    ; two-card desktop, vid_ctx_act clears [vid_rowmax] outright rather than
    ; letting the other adapter's table be read (SPEC.md 39.3.1). The desktop
    ; union grows [vid_h] and never [vid_ch], which is exactly the distinction
    ; 39.2.1 exists for.
    ;
    ; AND ONLY WHEN SS IS LOW_SEG. The SPLASH calls this routine, and it calls
    ; it on the boot sector's stack - boot/boot.asm's `.moved` points SS at its
    ; own relocated segment and stage 2 never changes it, so `.lowbss` is not
    ; reachable until kmain's `mov ss, ax`. Built there, the table is 696 bytes
    ; written over whatever is under the boot stack and [vid_rowmax] is left
    ; ARMED across the switch, so every gfx_rowbase after it indexes a table
    ; LOW_SEG never had. The miss path is exactly what the machine did before
    ; the table existed: [vid_rowmax] is 0 from the top of this routine, so
    ; gfx_rowbase computes, and vid_init's own vid_apply builds it for real.
    mov ax, ss                  ; AX, BX, CX, DX and DI are all dead from
    cmp ax, LOW_SEG             ; here to vid_apply's own epilogue, which is
                                ; what banks them for the caller - so this
                                ; block used to open and close on five
                                ; push/pop pairs that protected nothing
    jne .norowtab
    mov si, vid_rowtab
    xor di, di                  ; DI = rowbase(0), which is 0 on every adapter
    mov cx, VID_ROWTAB          ; ...and CX may be the counter now: it is
.rowtab:                        ; gfx_rowbase_calc that clobbers CL, and this
    mov [ss:si], di             ; loop no longer calls it. LOW_SEG, like every
    inc si                      ; other reach for the table
    inc si
    call gfx_nextrow            ; the INCREMENTAL form - see the wrap
    loop .rowtab                ; arithmetic above. DI and flags, nothing else
    mov word [vid_rowmax], VID_ROWTAB   ; ...and only now may anything read it
.norowtab:
    mov di, mouse_x             ; --- home the cursor; see the header.
    mov ax, [vid_w]             ; mouse_x and mouse_y are an adjacent pair
    shr ax, 1                   ; (mouse.inc pins it with an %if), so the two
    stosw                       ; stores are one walk - ES = DS and DF = 0
    mov ax, [vid_h]             ; from this routine's own prologue, and DI is
    shr ax, 1                   ; dead on both arms above
    stosw
    mov al, 1                   ; ...and the pointer MOVED (SPEC.md 7.2.1.1) -
    mov [cur_shchk], al         ; the geometry under it certainly did - and
    mov [ui_post], al           ; SAY SO once (SPEC.md 13.13). The accumulator
                                ; store is 3 bytes where `mov byte [m],imm8`
                                ; is 5
%ifdef KERN_BIG
    dec al
    mov [cur_disp], al          ; ...onto display 0, which is where the middle
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

    pop es                      ; ...and the other seven are vid_pop8's own run
    jmp vid_pop7                ; one label in - this routine has no BP in its
                                ; frame. See vid_pop8's header

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
    inc byte [spl_busy]         ; SPEC.md 15.3.8.3's lock, and the one entry on
                                ; that list which is not splash code: for part
                                ; of what follows [vid_seg] does not describe
                                ; the mode the card is in, so a loading-screen
                                ; frame arriving from IRQ0 would write its box
                                ; at an address that is not the framebuffer.
                                ; It is a COUNTER because this nests - spl_tick
                                ; reaches here through spl_chrome
    mov al, [vid_kind]          ; ONE load answers all three questions below:
    cmp al, VID_HERC            ; AL is idle until `mov ax, 0x0010`, and
    je .herc                    ; `cmp reg,imm8` is 2 bytes where
%ifdef GFX_VGA                  ; `cmp byte [mem],imm8` is 5
    cmp al, VID_EGA             ; A build with no VGA renderer never detects
    jne .colorbios              ; an EGA either (2.4: the gated probes fall
    mov ax, 0x0010             ; through to the equipment word), so the arm
    jmp short .bios            ; is dead there and the VIDEO= guard above
                              ; refuses the one way of reaching it.
                              ; EGA: mode 10h, 640x350x16 planar. NO
                              ; vid_cga_equip - that call is the XT ROM's
                              ; CGA-equipment quirk (see below), and an EGA
                              ; BIOS is not equipment-driven that way; it
                              ; sets mode 10h whatever 40:10 says.
.colorbios:
%endif
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
%ifdef GFX_VGA
    cmp al, VID_CGA             ; BEFORE the mov - `mov` sets no flags, and the
    mov ax, 0x0012              ; mov is what destroys AL. AL survives
    jne .bios                   ; vid_cga_equip above: that routine opens on
%endif                          ; `push ax` and closes on `pop ax`, which is
                                ; also why those two are not a saving of their
                                ; own. VGA: 640x480x16 planar

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
    mov cx, 8192
    call vid_wipe

    mov ax, 0x0006              ; CGA: 640x200 monochrome
.bios:
    int 0x10
%ifdef GFX_VGA
    ; THE TWO PLANAR ARMS CLEAR A0000 THEMSELVES (SPEC.md 39.23) - the ROM's
    ; clear is not to be trusted on either; CGA and Hercules did their own
    ; above.  ONE compare answers both of the questions this arm asks, "is it
    ; planar?" and "is it the SHORT planar one?", because `dec al` maps
    ; VGA -> FF, HERC -> 0, CGA -> 1, EGA -> 2: planar is `al >= 2` unsigned
    ; and EGA is `al == 2`, and the same `cmp` sets the flags for both.  The
    ; row count is therefore decided ONCE, above the VGA_DIRTY arm, which is
    ; what makes it structurally impossible for the dirty arm and the clean
    ; arm to disagree about how many rows they cover - the one thing the
    ; comment on that arm says it must not do.
    mov al, [vid_kind]
    dec al
    cmp al, 2
    jb .nowipe
    mov cx, SCREEN_H * ROW_BYTES / 2
    jne .wipego                     ; ZF is still the cmp's - `mov cx, imm16`
    mov cx, EGA_H * ROW_BYTES / 2   ; sets no flags.  EGA: 350 rows, not 480.
.wipego:                            ; Mode 10h on a 128KB card is 32KB per
                                    ; plane and 480*80 would run 5,632 bytes
                                    ; off it (SPEC.md 39.23/39.24). Stays a
                                    ; constant like SCREEN_H - this runs from
                                    ; the splash.

%ifdef VGA_DIRTY
    ; --- make VGADIRTY=1: BE the BIOS that did not clear (SPEC.md 39.23) ----
    ; The window between the ROM's mode set and ours is the only place a
    ; machine can be given the field's framebuffer, and every emulator in this
    ; tree has a BIOS that clears properly - so the failure this arm exists
    ; for cannot otherwise be shown. DIRTYRAM's shape, one device along.
    ;
    ; 0xDB is not arbitrary: it is a stripe rather than a solid, so a clear
    ; that reached only SOME of the four planes leaves a different colour
    ; instead of the same black, and tests/vgadirty.py can tell the two apart.
    mov dx, VGA_SEQ
    mov ax, 0x0F02
    out dx, ax
    mov ax, VGA_SEG
    mov es, ax
    xor di, di
    mov ax, 0xDBDB
    push cx                         ; ...and THE SAME row count the clear below
    cld                             ; uses, because it is now the same CX: the
    rep stosw                       ; decision was taken once, above .wipego.
    pop cx                          ; This arm must dirty exactly what that one
%endif                              ; is asked to clean, and neither may run
                                    ; off a 128KB EGA's 32KB plane (SPEC.md
                                    ; 39.23/39.24)

    ; --- ...AND CLEAR A0000 OURSELVES (SPEC.md 39.23) -----------------------
    ; THE OTHER TWO ARMS OF THIS ROUTINE ALREADY DO. Hercules clears its 32KB
    ; behind its own blank; the CGA arm zeroes B800 before the mode set, with
    ; the reasoning three screens up. This arm trusted `int 10h AX=0012h` to
    ; do it - and two places in the kernel state as fact that "vid_setmode
    ; clears the framebuffer" (spl_reset, vid_switch), so the VGA was the one
    ; adapter on which that was a hope.
    ;
    ; It is not always true. A field report on 86Box showed the loading screen
    ; drawn over a full field of banded dashes - the mode 3 CHARACTER
    ; GENERATOR, which lives in plane 2 and becomes bitmap the moment the card
    ; is in 12h - with the BIOS's own title text speckled by it, because
    ; teletype into a graphics mode is transparent. A ROM that clears only
    ; plane 0, or clears B8000 instead, leaves exactly that.
    ;
    ; This is the CGA arm's bug on the other card and the same answer: remove
    ; the garbage rather than hope. NO EMULATOR HERE SHOWS IT either, which is
    ; why tests/vgadirty.py dirties the framebuffer on purpose.
    ;
    ; The five register writes are not decoration - the Map Mask is what
    ; decides how many planes one CPU byte reaches, and a BIOS that left it at
    ; plane 0 is one of the ways the garbage survives. They are inline rather
    ; than vga_gc_reset's because THIS RUNS FROM THE SPLASH, whose first tick
    ; is gated on SPL_RESIDENT sectors: vga12.inc is thousands of bytes past
    ; that window and is not resident yet. EGA shares every one of these
    ; writes - the GC and Sequencer are byte-identical on it - and differs
    ; only in the row count below.
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2 Map Mask: all four planes
    out dx, ax
    mov dx, VGA_GC
    mov ax, 0x0001              ; GC1  Enable Set/Reset: none, so the CPU byte
    out dx, ax                  ;      is the data
    mov ax, 0x0003              ; GC3  Data Rotate/Function: replace, no rotate
    out dx, ax
    mov ax, 0x0005              ; GC5  Mode: write mode 0
    out dx, ax
    mov ax, 0xFF08              ; GC8  Bit Mask: every bit ours
    out dx, ax
    mov ax, VGA_SEG             ; ...and CX is the count .wipego settled
    call vid_wipe
                                ; ~110 ms on a 4.77MHz 8088, ONCE: vid_init
                                ; skips the mode set while [spl_live] is up,
                                ; and every later caller owes a full repaint
%endif                          ; ...end of the mode 12h arm and its clear
.nowipe:
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
    mov cx, 0x4000              ; happens BEHIND the blank, so the powered-up
    call vid_wipe               ; contents of the card's DRAM are never shown
    mov dx, 0x3B8               ; graphics, page 0, video ON - last, onto a
    mov al, 0x0A                ; framebuffer that is already black
    out dx, al
.done:
    dec byte [spl_busy]         ; ...and the card describes itself again

; vid_pop8 - the eight-register epilogue THREE routines in this file share,
; and vid_pop7 one label in is a FOURTH with a shorter frame.
; vid_detect and vid_text jump here rather than
; carrying their own copy, so **their prologues are now part of THIS routine's
; contract**: all three push `ax bx cx dx si di bp es` in that order, and a
; `push` added to any one of them without the other two returns the machine to
; a wrong address at boot.  vid_apply pushes the same run WITHOUT `bp`, pops
; its own `es`, and enters at `vid_pop7` - so it is bound by the SEVEN and not
; by the eight, and a `push` added to the other three must not be added to it.
; Nothing in the tree checks either; these comments and the ones at the three
; jump sites are the whole of the guard.  The label is BELOW
; `dec byte [spl_busy]`, so the splash counter is decremented once.
;
; IT KEEPS ITS OWN POP RUN, and SPEC.md 15.1.2's ladder may never have it.
; `jmp kret_es` here BOOTS NOTHING: vid_setmode, vid_detect and vid_text all
; run from the splash, whose first tick is gated on SPL_RESIDENT sectors
; (splash.inc, 9 = 4,608 bytes), and the ladder is at the far end of `.text` -
; ~50,700 bytes in, eleven times past the window the boot sector has read.
; The jump lands in RAM the disk has not filled yet, the machine executes it,
; and every adapter comes up dead with no message.  It is the same rule
; vid_wipe's header states two routines down - "a helper further down the
; image is not resident yet" - and the ladder is exactly such a helper.
; tests/unit/t_asmrules.py's resident-window check is the mechanical guard.
vid_pop8:
    pop es
    pop bp
vid_pop7:                       ; ...AND A FOURTH ENTRY, ONE LABEL IN, WITH A
    pop di                      ; DIFFERENT FRAME. vid_apply pushes the same
                                ; run WITHOUT bp and pops its own `es`, so it
                                ; joins here; it is the one arrival that is
                                ; not bound by the eight-push contract above,
                                ; and it is bound by the seven instead
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_wipe - zero CX words at AX:0000, the three framebuffer clears above
; in:       AX = segment, CX = words
; out:      that block is zero
; clobbers: AX, CX, DI, ES, flags - all four are pushed by vid_setmode
;
; It lives HERE rather than beside a general filler because it has to stay
; inside viddet.inc: vid_setmode runs from the splash, whose first tick is
; gated on SPL_RESIDENT sectors, and a helper further down the image is not
; resident yet.  `cld` is inside it, which is what keeps vid_setmode's own
; "we run on the boot sector's flags" requirement satisfied.
;
; NOT for the `VGADIRTY=1` arm: that one fills 0xDBDB, and zero is the whole
; of this routine's economy (`xor ax,ax` is free where a fill value is not).
; -----------------------------------------------------------------------------
vid_wipe:
    mov es, ax
    xor di, di
    xor ax, ax
    cld
    rep stosw
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
    and byte [0x0010], 0xEF     ; 11b -> 10b is "clear bit 4, leave bit 5", and
                                ; ONE and does it. Correct only because the
                                ; `cmp al, 0x30 / jne .out` above has already
                                ; proved the pair IS 11b - relax that guard and
                                ; this line stops being equivalent to the
                                ; clear-then-set it replaced. Every other bit
                                ; of 40:10 is untouched, as before.
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
    jmp vid_pop8                ; the shared epilogue - and so this routine's
                                ; push list is vid_setmode's. See vid_pop8.

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
; MEASURED, entry to the caller's return address on MartyPC's cycle counter,
; AX = 100 so the TABLE path is taken, fifteen samples a kernel: the `xchg`
; below took it from 136 to 127 cycles on CGA, 132 to 130 on Hercules and 132
; to 128 on VGA - one byte, and the two sample sets are disjoint on every
; adapter. This routine is on the fixed cost of every drawing call in the
; machine, so a byte here is worth measuring.
;
; Every geometry word is read through CS, not DS - see gfx_nextrow.
; -----------------------------------------------------------------------------
gfx_rowbase:
    cmp ax, [cs:vid_rowmax]     ; ONE guard, not two: [vid_rowmax] is 0 until
    jae gfx_rowbase_calc        ; vid_apply has filled the table, so "there is
    push bx                     ; no table yet" and "this row is past its end"
    xchg ax, bx                 ; are the same compare (SPEC.md 39.3.1). The
    shl bx, 1                   ; xchg is `mov bx, ax` in one byte instead of
                                ; two - AX is overwritten two instructions
                                ; down, so receiving the old BX is harmless,
                                ; and 3 clocks over 1 byte beats 2 over 2 on
                                ; the fetch floor. THIS IS THE FIXED COST OF
                                ; EVERY DRAWING CALL IN THE MACHINE
    mov ax, [ss:bx + vid_rowtab]    ; LOW_SEG - see the table's own note
    pop bx
    ret

; gfx_rowbase_calc - the arithmetic. It fills the table above, it answers for a
; row past its end, and it is what draws before vid_apply has run at all.
gfx_rowbase_calc:
    push bx
    mov bx, ax
    and bx, [cs:vid_bmask]      ; BX = bank number
    mov cl, [cs:vid_bshift]
    shr ax, cl                  ; AX = row within the bank
    mul word [cs:vid_stride]    ; DX:AX; DX is always 0 (86*90 at worst)
    mov cl, 3                   ; ...and BX = bank * 0x2000. `ror` BY 3 IS
    ror bx, cl                  ; `shl` BY 13 for any value that fits in three
    add ax, bx                  ; bits, which the bank does - [vid_bmask] is
                                ; 0, 1 or 3, four banks being the maximum the
                                ; 0x2000 window assertion at the foot of this
                                ; file allows: 3 -> 0x6000, 2 -> 0x4000,
                                ; 1 -> 0x2000, 0 -> 0. This was a four-entry
                                ; TABLE because `shl bx, 13` is 8 clocks plus
                                ; 4 a bit on an 8088 - 60 - and gfx_rowbase is
                                ; on the fixed cost of every drawing call
                                ; (PERFORMANCE.md Part 2). What the table
                                ; refused was the SHIFT, and it never
                                ; considered the ROTATE: 8 + 4*3 = 20 clocks
                                ; over 4 bytes against the lookup's 25 over 7,
                                ; so this is smaller AND faster, under the
                                ; 8088's max(clocks, 4.34 x bytes) fetch floor
                                ; either way. The two CL uses are sequenced,
                                ; not nested
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
%if (348/4)*90 > 0x2000 || (VID_H_MIN/2)*80 > 0x2000
%error "viddet: intra-bank extent exceeds 0x2000 - gfx_nextrow needs a row counter"
%endif

; -----------------------------------------------------------------------------
; gfx_ink - reduce a 16-colour index to a 1bpp ink (SPEC.md 39.4)
; in:       AL = colour 0..15
; out:      AL = 0x00 black, 0xFF white, 0x01 the 50% dither class
; clobbers: AX, flags       <- AH, because `cbw` writes it. It was "AL"; every
;           one of the six call sites was checked and reads only AL after the
;           call (vga12.inc:1140, :1719, :1939, :2137, icons.inc:460,
;           font.inc:884), so this widening documents the code rather than
;           changing what any caller may do. BX is still preserved -
;           vga12.inc:2137 depends on that and says so.
;
; PER GLYPH CELL on the 1bpp adapters (font_ink's caller is font_char_bb), so
; the accumulator form is worth its two bytes twice over: 7 bytes and 9 clocks
; become 5 and 8, both fetch-bound on the 8088's max(clocks, 4.34 x bytes)
; floor - 30.4 -> 21.7 clocks, ~143 us off a 78-cell row at 4.77 MHz.
;
; ...and `xchg ax, bx` for `mov bx, ax` took a further byte off the same
; argument. MEASURED end to end on MartyPC's own cycle counter, entry to the
; caller's return address, AL = 12, twenty samples: 91 -> 87 cycles on CGA
; (os8088_5150_cga_gla) and 91 -> 87 on Hercules (os8088_5150_herc_gla), the
; two distributions disjoint. `xchg` is a clock DEARER than `mov` and a byte
; shorter, so the four cycles are the prefetch queue and nothing else - which
; is the whole of why the fetch floor is worth designing to.
; -----------------------------------------------------------------------------
gfx_ink:
    push bx
    and al, 15                  ; accumulator form: 2 bytes, not 3
    cbw                         ; 0..15, so the sign bit is clear and AH
    xchg ax, bx                 ; becomes 0 in one byte - and the xchg is
                                ; `mov bx, ax` in one byte rather than two.
                                ; AH ends holding the caller's old BH instead
                                ; of 0, which is inside the "clobbers: AX" the
                                ; header above widened to and re-checked all
                                ; six call sites for
    mov al, [bx + gfx_inktab]
    pop bx
    ret

section .text                   ; ...and back, so the next include lands right

; =============================================================================
; WHAT THE SPLASH LEFT BEHIND (SPEC.md 2.9.4)
;
; The loading screen is stage 2's now, so every call it makes into the kernel
; is across a segment. These four are that, four bytes each - and they are
; HERE, at the end of viddet.inc, rather than in kernel.asm's cw_ block for one
; reason: SPL_RESIDENT. The bar cannot move until the routines the first tick
; calls are ABOARD, and the cw_ block is at the end of `.text` - the last
; sector of the image. Here they land in sector 7, which is what SPL_RESIDENT
; now measures and why it fell from 9 to that.
; =============================================================================
spw_vid_detect:   call vid_detect
                  retf
spw_vid_apply:    call vid_apply
                  retf
spw_vid_setmode:  call vid_setmode
                  retf
spw_gfx_rowbase:  call gfx_rowbase
                  retf
spw_resident_end:

; ...and the THREE BYTES of splash.inc that could not go with it. viddet's mode
; guard, fprog's boot test and vidsel's spl_reset all ask [spl_live] through
; DS, so it lives where DS reaches it. 967 bytes left; these stayed.
spl_live    db 0

; [spl_busy] IS THE LOADING SCREEN'S ONE LOCK (SPEC.md 15.3.8). It is raised
; around every path that draws while [spl_live] is up, and spl_isr - which
; fires from IRQ0 in the middle of a BIOS disk call - refuses the tick unless
; it is clear, then raises it itself for the duration of its own frame. So the
; same byte is the main line's "I am drawing" and the interrupt's re-entrancy
; lock, and there is one rule rather than two.
;
; It is HERE and not beside the rest of splash.inc's state for spl_live's
; reason and one more: vid_setmode raises it, and that is kernel `.text` which
; cannot reach a `.boot2` byte through DS. What it guards is not the pixels -
; the animation writes its own box and nothing else does - it is the VGA's
; GRAPHICS CONTROLLER. spl_bitmask arms the Bit Mask and restores it, and
; font_run programs the write mode and the Map Mask; an opaque `rep movsb`
; landing between one of those and its restore draws through somebody else's
; mask, which is a defect no emulator run would repeat twice the same way.
spl_busy    db 0

; [spl_quiet] IS THE THIRD, and it is here for spl_busy's second reason
; (SPEC.md 15.3.3.1, 15.3.8.2). It was a `.boot2` byte read `cs:`, which was
; right while spl_paint was its only reader - and then 15.3.8 gave the
; animation to IRQ0, where BOTH handlers are kernel `.text` and `cs:` names
; the KERNEL segment rather than the blob. So the flag stopped reaching the
; only two callers that could still spend the 28.7 ms it exists to refuse:
; spl_stepq raised it, mouse_init's identify window ran, and the interrupt
; drew the spinner straight through it. An 8250 holds ONE byte and a
; 1200-baud 7N1 frame is 7.5 ms, so that is three bytes of the burst 9.4
; exists to capture - a mouse that is on the wire and not detected, which is
; PERFORMANCE.md's input overrun and is invisible in an emulator.
spl_quiet   db 0

