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
; be here. They come from the claim heap (SPEC.md 50): one contiguous block
; asked for at startup and carved into four - canvas, undo image, clipboard,
; scratch - with the sizes tiered off OSAPI_MEM_AVAIL, so a small machine
; gives up features one at a time and finally gets a notice window instead of
; a canvas. See the block above PT_GIF_MAX_KB below for what this used to be
; and why none of it is a fixed address any more.
;
; PERFORMANCE. Two decisions carry the whole app:
;
;  * The canvas is 4bpp packed, high nibble = the left pixel, rows stored
;    BOTTOM-UP behind a 118-byte BMP header - i.e. the canvas IS a
;    BITMAPINFOHEADER DIB. Saving is therefore one OSAPI_FILE_WRITE of the
;    canvas claim with no staging pass and no copy - and since the write
;    walks its source by SEGMENT (SPEC.md 18.4.1), one call whatever the
;    picture's size.
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
; drawn under a held lock, so without the
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

; --- the memory we CLAIM from the kernel (SPEC.md 50) ---------------------------
; One contiguous block, asked for at startup and carved into four: canvas,
; undo image, clipboard, scratch. Nothing here is a fixed address any more -
; the kernel owns the map and hands us a segment, which is the whole of what
; docs/PAINT-NOTES.md asked for. What used to be here instead: two hard-coded
; bases (0x66000, or the back buffer's own segment when the kernel could
; never arm one),
; a mirror of the kernel's DB_MIN_KB policy constant to choose between them,
; and a magic-word claim record so two Paints could not silently share one
; canvas. All three are gone: OSAPI_MEM_CLAIM cannot hand the same paragraph
; to two instances, so two Paints now simply both run.
PT_GIF_MAX_KB equ 64            ; the GIF encoder's transient claim, KB: its
                                ; write cursor pt_gout is a 16-bit offset into
                                ; one segment, so more would be unreachable
PT_STAGE_MAX equ 192            ; the load's transient staging claim, KB. A
                                ; picture bigger than this is not one this
                                ; app can hold anyway - a full 640x480 4bpp
                                ; canvas is 154KB with its header - and the
                                ; cap keeps one Open from taking the whole
                                ; heap on a big machine (SPEC.md 42.6)
PT_SC_KB    equ 12                  ; scratch (the flood-fill stack), taken off
                                    ; the TOP of the block
PT_CLIPMINP equ 256                 ; the clipboard's floor, in paragraphs. It
                                    ; was 1024 - 16KB - and that was the GIF
                                    ; codec's requirement, not the
                                    ; clipboard's; the tables have their own
                                    ; claim now, so a machine that can spare
                                    ; only a little gets a small clipboard
                                    ; instead of none
PT_MINP     equ 2000                ; ...and a canvas under 32,000 bytes
                                    ; (320x200) is not worth starting
PT_WANT_KB  equ 318                 ; the hard ceiling: two canvases at the
                                    ; 736x464 row-table limit, the clipboard
                                    ; floor and the scratch. pt_geom asks for
                                    ; far less than this in practice - see
                                    ; pt_want - and this only stops the
                                    ; arithmetic there running away
PT_SC_STACK equ 16                  ; flood-fill span stack starts here
PT_FSTK_MAX equ 1024                ; entries of 8 bytes (y, x1, x2, dy)

; --- the GIF codec's work areas (SPEC.md 42) ---------------------------------
; The LZW tables have their own claim, taken for the length of one GIF and
; handed straight back (pt_alloc_lzw / pt_free_lzw). They used to BORROW the
; clipboard, which is why PT_CLIPMINP had a floor big enough to hold them -
; a legacy of the days before a package could ask the kernel for memory at
; all (SPEC.md 50.3). Borrowing cost more than it saved: it tied Save Gif to
; whether a clipboard happened to be funded, and it kept 16KB reserved in
; every clipboard on every machine for a codec that is idle almost always.
PT_GD_PREF  equ 0                   ; read: prefix[4096] words
PT_GD_SUFF  equ 8192                ; read: suffix[4096] bytes
PT_GD_STK   equ 12288               ; read: the output stack, 4096 bytes
PT_GD_END   equ 16384
PT_GE_CHILD equ 0                   ; write: child[2048] words, 0xFFFF = none
PT_GE_SIB   equ 4096                ; write: next-sibling[2048] words
PT_GE_SUF   equ 8192                ; write: suffix[2048] bytes
PT_GE_END   equ 10240
PT_GE_MAXC  equ 2047                ; the writer's code ceiling: 11 bits, minus
                                    ; one so the READER never crosses 2048 either
                                    ; (see pt_gadd - its table runs one behind)
PT_GDIM_MAX equ 4096                ; a GIF bigger than this in either axis is
                                    ; refused, not decoded row by row to nowhere
PT_LZW_KB   equ 16                  ; the claim both directions run in
%if PT_GD_END > PT_LZW_KB * 1024
%error "the GIF read tables no longer fit PT_LZW_KB"
%endif
%if PT_GE_END > PT_LZW_KB * 1024
%error "the GIF write tables no longer fit PT_LZW_KB"
%endif

; --- canvas geometry -----------------------------------------------------------
; The width is fixed on every adapter: the narrowest screen os8088 drives is
; 640 (SPEC.md 39), and 448 + the palette + the frame fits there. Only the
; HEIGHT varies, because the desktop between the menu bar and the dock does
; (436 / 300 / 152 rows on VGA / Hercules / CGA), and pt_entry derives it
; from the live geometry rather than from a constant.
PT_CW_DEF   equ 448                 ; the canvas a fresh Paint starts with...
PT_CH_DEF   equ 280                 ; ...clamped to the screen and to memory
PT_CW_MAX   equ 736                 ; row-table sizing: the widest screen
PT_CH_MAX   equ 464                 ; os8088 drives is 720, the tallest 480
PT_BMPHDR   equ 118                 ; 14 + 40 + 16*4: the DIB in front of row 0

; --- content layout (all content-relative; SPEC.md 11's content rect) ----------
PT_BW       equ 20                  ; tool / swatch button side, pixels
PT_PAL_X0   equ 1                   ; tool palette, left column
PT_PAL_X1   equ 22                  ; ...and right column
PT_PAL_Y0   equ 1                   ; first button row
PT_PAL_DY   equ 21                  ; button pitch
PT_SEPX     equ 43                  ; the black vline between palette and canvas
PT_CV_X     equ 44                  ; canvas left edge
PT_STRIP_H  equ 22                  ; bottom strip: colours, widths, toggles
PT_CHROME_H equ PT_STRIP_H + 1 + 19 ; canvas height -> frame height (the +1 is
                                    ; the separator row, the 19 is TITLE_H+1)
PT_CHROME_W equ PT_CV_X + 2         ; canvas width  -> frame width
PT_WIN_Y    equ MBAR_H + 4          ; frame top

; The smallest canvas there is, and it is the KERNEL's number rather than one
; of ours: wm_resize and ui_grow both clamp a frame at WMIN_W x WMIN_H
; (SPEC.md 11.1) and neither tells the app they did it, so a floor of our own
; below theirs is a canvas the window can never be - Paint drawing [pt_ch]
; rows inside a content box the kernel made taller, and a strip of stale
; white under the picture that nothing owns. Derived, so the two cannot drift.
;
; It REPLACED a floor of PT_SZ_END - "tall enough to still show the size
; boxes" - and SPEC.md 42.9 is why that went: it was a minimum window size
; imposed to keep a control reachable, and it made every picture shorter than
; 128 rows open at the wrong size on every adapter. The controls degrade on
; their own (pt_szon, pt_draw_dims, pt_draw_pal all draw what fits), and the
; grow box is the kernel's, on the frame, at every size the kernel allows.
PT_CW_MIN   equ WMIN_W - PT_CHROME_W    ; 50
PT_CH_MIN   equ WMIN_H - PT_CHROME_H    ; 22
PT_GROW     equ 15                  ; the grow box owns the content's last 13
                                    ; columns and rows (SPEC.md 11) - which is
                                    ; inside the strip, so the strip's
                                    ; right-hand controls stop short of it

; strip contents, content-relative x (the strip's own top is [pt_stripy])
PT_TH_X     equ 2                   ; four thickness buttons, pitch 21
PT_TH_DX    equ 21
PT_CUR_X    equ 90                  ; current-colour well
PT_SW_X     equ 116                 ; colour swatches: the design pitch, and
PT_SW_DX    equ 21                  ; the tightest one still worth aiming at.
PT_SW_MIN   equ 11                  ; The live pitch is [pt_swdx] (SPEC.md 42)
PT_BTN_W16  equ 16                  ; the two toggles are right-anchored, so
                                    ; their x is [pt_filx] / [pt_fntx]

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
PT_M_NOMEM  equ 1                   ; the heap cannot fund a minimum canvas
PT_M_SMALL  equ 2                   ; the desktop cannot hold a usable canvas

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
;
; A DOCUMENT WE WERE LAUNCHED TO OPEN IS READ HERE, BEFORE wm_create, AND
; SPEC.md 42.11 IS WHY. It used to be read from the first W_PAINT, and that
; is a paint the kernel has already drawn a frame for: the picture's size
; arrives too late to be the window's, pt_wfollow's [pt_wchg] had no caller
; to spend it on that path, and pt_track - which runs a few instructions
; later and exists to follow a GROW BOX - then dragged the canvas back out to
; the window it did not fit. A 466x110 logo opened as 466x258. Loaded here
; the size is simply the template's, and there is no resize at all.
;
; Nothing in the load draws: the decoders write the canvas claim
; (pt_line_put, "canvas only, no screen"), pt_caret_hide and pt_marq_hide are
; gated on flags a fresh bss holds at 0, and the outcome is a TOAST, which
; SPEC.md 59 stages when the lock is not held. The file API is legal here -
; the entry proc runs on the UI task and the loader's own image read, with no
; lock held either, is a few hundred instructions behind us.
; -----------------------------------------------------------------------------
pt_entry:
    mov byte [pt_fscale], 1         ; the bss arrives zeroed, and zero is not a
    mov byte [pt_ethick], 1         ; text scale; the eraser starts at 16px
                                    ; against the pencil's 1px, which is what
                                    ; "much thicker by default" means here
    call pt_geom                    ; screen limits, the memory claim, canvas
    call pt_arg                     ; were we launched to open a picture?
    cmp byte [pt_mode], PT_M_LIVE   ; a machine that cannot fund a canvas has
    jne .make                       ; nothing to decode into
    call pt_canvas_init             ; the canvas to decode INTO - the header
    call pt_font_init               ; and the white, before anything fills it
    call pt_argload                 ; ...and the picture, if pt_arg found one:
                                    ; the [pt_argp] gate is pt_argload's own,
                                    ; and the comment there says why
.make:
    push si
    mov si, pt_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; no window: nothing to flag, nothing to
                                    ; claim - the region stays untouched
    mov [pt_win], bx
    push ax
    mov al, 1                       ; WE OWN OUR BACKGROUND (SPEC.md 11.90.1):
    call OSAPI_WM_OWNBG             ; pt_fsbed + the palette + the strip + the
    pop ax                          ; canvas cover every pixel of the content,
                                    ; so the kernel's white fill in front of
                                    ; W_PAINT is a white HOLE for as long as
                                    ; the canvas takes to blit - measured at
                                    ; 8.7 s on a textured bitmap
                                    ;
                                    ; NO WF_SAVEU here, and the promise WOULD
                                    ; hold - Paint owns no worker, its marquee
                                    ; is a latched XOR and its caret is
                                    ; keystroke-driven. What disqualifies it is
                                    ; MEMORY: a cache over this content is ~9KB
                                    ; on 1bpp, ~36KB on VGA and ~150KB for a
                                    ; window grown over most of a 640x480
                                    ; screen, on top of the ~127KB Paint
                                    ; already holds - so on a 256KB machine it
                                    ; is refused and the feature is not there.
                                    ; (This comment used to say the repaint was
                                    ; "the cheapest in the tree" and that there
                                    ; was one cache for the machine. Both were
                                    ; wrong: PERFORMANCE.md Set 32 measures the
                                    ; repaint at 9 s, and SPEC.md 11.96.3 made
                                    ; the cache one per WINDOW.)
                                    ;
                                    ; ...AND THAT IS WHAT SPEC.md 11.96.11 IS
                                    ; FOR. The tool column is 451 of the 604
                                    ; drawing calls a repaint issues - 75% of
                                    ; a measured 421.8 ms on a 4.77MHz 8088 -
                                    ; and it is 44 pixels wide, so banking THAT
                                    ; is ~1KB on 1bpp and ~13KB on VGA instead
                                    ; of 9KB and 150KB. The promise is made
                                    ; only if the band was taken: refused, a
                                    ; WF_SAVEU here asks for the whole-content
                                    ; cache this app cannot afford
    pushf                           ; THE CF wm_create OWES THE LOADER rides
    push ax                         ; through here too, and this pair asks a
    push cx                         ; question that answers in it
    xor al, al                      ; edge 0 = left
    mov cx, PT_CV_X                 ; ...as far as the canvas: the divider
    call OSAPI_WM_BAND              ; column is the band's last pixel
    jc .noband
    mov al, 1
    call OSAPI_WM_SAVEU             ; our content does not change while we are
.noband:                            ; not drawing: no worker, the marquee is a
    pop cx                          ; latched XOR, the caret is keystroke-driven
    pop ax
    popf
    cmp byte [pt_mode], PT_M_LIVE
    jne .menus
    push ax
    mov al, 1                       ; resizable: the canvas IS the content, so
    call OSAPI_WM_SIZABLE           ; dragging the grow box sizes the picture.
                                    ; It used to be conditional on there being
                                    ; an undo image to stage the old picture
                                    ; in; pt_resize copies block to block now
                                    ; and needs no staging area at all
    mov ax, pt_onsize               ; ...and the kernel asks us before it
    call OSAPI_WM_ONSIZE            ; commits one (SPEC.md 11.1)
    pop ax
    push si                         ; SI is the loader's, like the two other
    mov si, pt_about                ; borrowings in this proc
    call OSAPI_ABOUT_SET            ; 'About Paint' under our name in the bar
    pop si                          ; (SPEC.md 12.2); BX is still the window.
                                    ; LIVE only - see pt_about
    call pt_menufix                 ; and the menus say what is unavailable                      ; a notice window gets the menu bar too,
                                    ; so its name shows and File/Edit are
                                    ; visibly inert rather than absent
.menus:
    push si
    mov si, pt_menus                ; BX is still the window (SPEC.md 20.3:
    call OSAPI_MENU_SET             ; menu_win_set preserves flags too)
    pop si
.out:
    ret

; -----------------------------------------------------------------------------
; pt_geom - the screen's limits, the memory claims, and the first canvas
; in:  nothing
; out: pt_tpl patched; [pt_cwmax]/[pt_chmax] = the biggest canvas this SCREEN
;      can show; [pt_smaxp] = what the canvas CLAIM holds, in paragraphs; the
;      four claim segments; [pt_cw]/[pt_ch] = the starting canvas;
;      [pt_mode] = PT_M_NOMEM or PT_M_SMALL when a bound cannot be met
;
; The screen bound comes from OSAPI_VIDEO (SPEC.md 39.2): the window sits at
; PT_WIN_Y and may reach the row the dock owns.
;
; **FOUR separate claims, sized for the canvas we are about to show** - not
; one block sized for the biggest canvas the screen could ever show. The
; difference is the whole memory story: a package may hold several claims
; (SPEC.md 50.2) and the kernel frees all of them at teardown, so there is no
; reason to reserve the maximum up front. What made the old version do it was
; that pt_resize staged the old picture in the undo image, which therefore had
; to be big enough for any canvas that could ever be adopted - and the bases
; had to be fixed for the staging to be safe. pt_resize re-claims now, so
; both constraints are gone and a fresh 448x280 Paint holds about 150KB on a
; 640KB machine instead of 260KB.
; -----------------------------------------------------------------------------
pt_geom:
    call OSAPI_VIDEO                ; AX=w, BX=h, CX=dock row, DL=kind, DH=bpp
    mov [pt_scrw], ax
    mov [pt_scrh], bx               ; the whole screen, for the SPEC.md 53
    mov [pt_dockr], cx              ; bracket's own pointer (pt_ptr_xor)
    mov byte [pt_ncol], 16
    cmp dh, 1
    jne .colour
    mov byte [pt_mono], 1
    mov byte [pt_ncol], 3
.colour:
    ; --- what the screen allows ---------------------------------------------
    sub cx, PT_WIN_Y + PT_CHROME_H  ; the tallest canvas from y = PT_WIN_Y
    cmp cx, PT_CH_MAX
    jle .h_cap
    mov cx, PT_CH_MAX
.h_cap:
    mov [pt_chmax], cx
    sub ax, PT_CHROME_W             ; ...and the widest the screen leaves
    cmp ax, PT_CW_MAX
    jle .w_cap
    mov ax, PT_CW_MAX
.w_cap:
    mov [pt_cwmax], ax
    cmp cx, PT_CH_MIN
    jl .small
    cmp ax, PT_CW_MIN
    jge .mem
.small:
    mov byte [pt_mode], PT_M_SMALL
    mov word [pt_cwmax], PT_CW_MIN  ; the window will only carry a notice, but
    mov word [pt_chmax], PT_CH_MIN  ; the arithmetic below still has to run
    ; --- what memory allows -------------------------------------------------
    ; Four claims (SPEC.md 50.3), each independently refusable, and each
    ; refusal costs exactly the feature it funds. That is the tier system the
    ; old single block emulated with arithmetic, expressed as what it is.
.mem:
    mov ax, PT_CW_DEF               ; the canvas we are about to show, which is
    cmp ax, [pt_cwmax]              ; what we size the claims for
    jle .dw_ok
    mov ax, [pt_cwmax]
.dw_ok:
    mov dx, PT_CH_DEF
    cmp dx, [pt_chmax]
    jle .dh_ok
    mov dx, [pt_chmax]
.dh_ok:
    call pt_growmax                 ; what one canvas could be, given the heap
    call pt_fit                     ; ...and shrink the request until it fits
    call pt_alloc                   ; scratch, canvas, undo image, clipboard
    jnc .first
.nomem:
    mov byte [pt_mode], PT_M_NOMEM
    mov word [pt_smaxp], PT_MINP    ; nothing is claimed or drawn, but the
                                    ; layout arithmetic still runs once
    ; --- the starting canvas: the default, held inside both bounds ----------
.first:
    mov ax, PT_CW_DEF
    cmp ax, [pt_cwmax]
    jle .cw_ok
    mov ax, [pt_cwmax]
.cw_ok:
    mov dx, PT_CH_DEF
    cmp dx, [pt_chmax]
    jle .ch_ok
    mov dx, [pt_chmax]
.ch_ok:
    call pt_fit                     ; shrink it until memory can hold it
    call pt_layout                  ; stride, row tables, buffer offsets
    call pt_wsize                   ; ...and the window that shows it
    ret

; -----------------------------------------------------------------------------
; pt_wsize - size pt_tpl (and, after creation, the record) from the canvas
; in:  [pt_cw], [pt_ch], [pt_scrw]
; out: pt_tpl WT_W/WT_H/WT_X/WT_Y set; preserves all registers
; -----------------------------------------------------------------------------
pt_wsize:
    push ax
    mov ax, [pt_ch]
    add ax, PT_CHROME_H
    mov [pt_tpl + WT_H], ax
    mov word [pt_tpl + WT_Y], PT_WIN_Y
    mov ax, [pt_cw]
    add ax, PT_CHROME_W
    mov [pt_tpl + WT_W], ax
    mov ax, [pt_scrw]
    sub ax, [pt_tpl + WT_W]
    jns .x_ok
    xor ax, ax
.x_ok:
    shr ax, 1
    mov [pt_tpl + WT_X], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_fit - shrink a candidate canvas until memory can hold it
; in:  AX = width, DX = height
; out: AX/DX clamped, both at least the minimums; CF=1 if something had to
;      give (which a load reports as a truncated picture)
;
; Height gives first: a picture that loses rows off the bottom stays more
; recognisable than one losing columns off the right AND rows off the bottom.
; Only with the height already at the floor does the width start to give.
;
; [pt_pinw]/[pt_pinh] take an axis out of the ladder. pt_sizeask sets one when
; its ink guard has just put that axis back to the size it already has: the
; guard's answer must not then be undone by the very routine whose cut it was
; overruling, so the OTHER axis gives the ground instead. Both pinned means
; nothing can give, and the CF=1 that comes back says exactly that.
; -----------------------------------------------------------------------------
pt_fit:
    push bx
    push cx
    mov byte [pt_fitcut], 0
.retry:
    call pt_paras                   ; BX = stride, CX = paragraphs needed
    cmp cx, [pt_smaxp]
    jbe .out                        ; the block we hold carries it...
    cmp cx, [pt_growp]
    jbe .out                        ; ...or a bigger one we could claim does
    mov byte [pt_fitcut], 1
    cmp byte [pt_pinh], 0
    jne .narrow                     ; an axis the ink guard put back is not
    cmp dx, PT_CH_MIN               ; this routine's to cut (pt_sizeask)
    jbe .narrow
    mov cx, dx
    shr cx, 1                       ; an eighth of the height at a time (the
    shr cx, 1                       ; 8086 shifts by one or by CL, nothing
    shr cx, 1                       ; else)
    or cx, cx
    jnz .cuth
    mov cx, 1
.cuth:
    sub dx, cx
    cmp dx, PT_CH_MIN
    jge .retry
    mov dx, PT_CH_MIN
    jmp short .retry
.narrow:
    cmp byte [pt_pinw], 0
    jne .out
    cmp ax, PT_CW_MIN
    jbe .out                        ; both floors reached: nothing left to give
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or cx, cx
    jnz .cutw
    mov cx, 1
.cutw:
    sub ax, cx
    cmp ax, PT_CW_MIN
    jge .retry
    mov ax, PT_CW_MIN
    jmp short .retry
.out:
    cmp byte [pt_fitcut], 0
    pop cx
    pop bx
    je .clean
    stc
    ret
.clean:
    clc
    ret

; -----------------------------------------------------------------------------
; pt_kb_of - KB a claim of CX paragraphs needs
; in:  CX = paragraphs
; out: AX = KB, rounded UP (a claim a paragraph short is a claim that
;      corrupts); preserves every other register
; -----------------------------------------------------------------------------
pt_kb_of:
    push cx
    mov ax, cx
    add ax, 63
    jnc .ok
    mov ax, 0xFFFF                  ; cannot happen: pt_fit caps the canvas
.ok:
    mov cl, 6
    shr ax, cl
    or ax, ax
    jnz .out
    inc ax
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; pt_alloc - claim everything a canvas of AX x DX needs (SPEC.md 50.3)
; in:  AX = width, DX = height (already through pt_fit)
; out: CF=1 nothing usable could be claimed; else the four claim segments are
;      published and [pt_haveundo]/[pt_haveclip] say what was funded.
;      Preserves AX and DX.
;
; The canvas is the only claim that must succeed. The undo image and the
; clipboard are each worth exactly one feature, so each is asked for on its
; own and a refusal simply switches that feature off - which is the same three
; tiers the single-block version produced by arithmetic, without the
; arithmetic. The scratch is a fixed 12KB and comes first, because the flood
; fill and the file readers borrow it and neither can be given up.
; -----------------------------------------------------------------------------
pt_alloc:
    push bx
    push cx
    push dx
    push ax
    call pt_paras                   ; CX = paragraphs one canvas needs
    mov [pt_needp], cx

    cmp word [pt_scseg], 0          ; scratch is claimed once and never freed
    jne .canvas                     ; - which is exactly the shape SPEC.md
    mov ax, PT_SC_KB                ; 50.2 warns about, a long-lived data claim
    call OSAPI_MEM_CLAIM            ; sitting mid-arena and splitting it in two
    jc .fail
    mov [pt_scseg], dx
    call pt_movable                 ; ...so it is declared too. It is the
                                    ; SMALLEST of the four and the one whose
                                    ; declaration matters most: pinned, it is
                                    ; a BARRIER (SPEC.md 66.4) sitting under
                                    ; the canvas, so nothing above it could
                                    ; ever pack down past it - measured, that
                                    ; alone held a 25KB canvas exactly where it
                                    ; was across a compaction with 137KB free
                                    ; underneath it

.canvas:
    mov cx, [pt_needp]
    call pt_kb_of
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [pt_base], dx
    call pt_movable                 ; the biggest claim on the machine, and
                                    ; the one worth moving (SPEC.md 66)
    mov cl, 6
    shl ax, cl                      ; what the claim actually holds...
    mov [pt_smaxp], ax              ; ...which is >= [pt_needp]

    call pt_alloc_undo
    call pt_alloc_clip
    pop ax
    pop dx
    pop cx
    pop bx
    clc
    ret
.fail:
    pop ax
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; pt_reloc - THE HEAP COMPACTOR MOVED ONE OF OUR CLAIMS (SPEC.md 66.2)
; in:  BX = the base segment it WAS at, DX = the base it is at NOW.
;      DS = CS = ours, ES = KERNEL_SEG. The bytes have already moved.
; out: nothing; every register preserved
;
; The kernel packs the data arena down when a claim would otherwise be
; refused, and tells each holder afterwards. What it CANNOT do is know what
; we derived from a base, which is why this is a callback and not a word the
; kernel pokes - and Paint is the case that makes the point, because the
; canvas's base is not held in one place at all:
;
;   [pt_base]     the block itself
;   pt_rowseg[]   ONE SEGMENT PER ROW, built off it by pt_geom - a canvas can
;                 be bigger than a segment, so a row is a (segment, offset)
;                 pair and the whole table is derived
;   [pt_undelta]  canvas -> undo image, a DELTA, so it is wrong when EITHER
;                 end moves
;
; The row table is SHIFTED rather than rebuilt: every entry is the base plus
; a per-row constant, so adding the move's delta gives exactly what pt_geom
; would recompute, at a fraction of the cost and with no dependence on
; [pt_cw]/[pt_stride] being current. If the table happens to be stale (built
; for a canvas we have since replaced), shifting leaves it exactly as stale as
; it was - never worse - and pt_geom rebuilds it before anything reads it.
;
; [pt_obase]/[pt_osrc] are here because a RESIZE holds the outgoing canvas
; across further claims (pt_alloc_undo's, in the .inplace path), so the old
; block is live, movable, and named by two words nothing else would fix.
;
; It claims nothing, frees nothing, yields nothing and draws nothing, which
; is SPEC.md 66.3 rule 3.
; -----------------------------------------------------------------------------
pt_reloc:
    push ax
    push cx
    push si
    cmp bx, [pt_base]
    jne .notcv
    mov [pt_base], dx
    mov ax, dx
    sub ax, bx                      ; AX = the paragraph delta, and 16-bit
                                    ; wraparound makes a downward move work
                                    ; unchanged - which is every move, since
                                    ; the compactor only ever packs DOWN
    mov cx, [pt_ch]
    jcxz .notcv
    mov si, pt_rowseg
.shift:
    add [si], ax
    inc si
    inc si
    loop .shift
.notcv:
    cmp bx, [pt_unseg]
    jne .notun
    mov [pt_unseg], dx
.notun:
    cmp bx, [pt_cbseg]
    jne .notcb
    mov [pt_cbseg], dx
.notcb:
    cmp bx, [pt_scseg]              ; the fill stack: one word, re-read at every
    jne .notsc                      ; use, and nothing derived from it
    mov [pt_scseg], dx
.notsc:
    cmp bx, [pt_obase]              ; a resize's outgoing canvas, held live
    jne .notob                      ; across the claims below it
    mov [pt_obase], dx
.notob:
    cmp bx, [pt_osrc]               ; ...and whichever block it is staging
    jne .notos                      ; from, which is the old canvas on one
    mov [pt_osrc], dx               ; path and the undo image on the other
.notos:
    mov ax, [pt_unseg]              ; the delta LAST, and unconditionally:
    or ax, ax                       ; either end may have been what moved,
    jz .out                         ; and by here both words are current
    sub ax, [pt_base]
    mov [pt_undelta], ax
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_movable - declare the claim in DX relocatable (SPEC.md 66.2)
; in:  DX = a base segment we hold
; out: nothing; every register and the flags preserved
;
; A REFUSAL IS NOT AN ERROR and is deliberately not reported: the kernel pins
; claims for reasons of its own and does not say which, so a claim that never
; moves is a normal outcome. All this costs when it fails is the fragmentation
; Paint had before.
;
; Four of Paint's five claims are declared and one is not: the GIF codec's
; tables live for one file and have [pt_gbase] derived off them in ways this
; proc does not account for, so they stay pinned and stay transient.
;
; THE SMALL ONE IS NOT THE OPTIONAL ONE. Three of the four are canvas-SIZED
; and are declared because of what they cost; the scratch block is 12KB and is
; declared because of WHERE it is - claimed once at startup, never freed, and
; sitting under the canvas, so pinned it is a barrier (SPEC.md 66.4) that no
; amount of free heap below it lets anything cross.
; -----------------------------------------------------------------------------
pt_movable:
    pushf
    push ax
    mov ax, pt_reloc
    call OSAPI_MEM_MOVABLE
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; pt_pin / pt_unpin - hold the canvas still across a kernel call that keeps a
; pointer INTO it (SPEC.md 66.5.7.1); preserves all registers and the flags
;
; The BMP save is one OSAPI_FILE_WRITE straight out of the canvas, and the
; kernel holds ES:BX across the whole call - a volume switch inside it can
; claim (SPEC.md 18.95), a claim can compact, and pt_reloc would put
; [pt_base] right while the kernel went on writing from the old segment.
; dsk_xfer's [mem_pinseg] covers each transfer, not the walk between them.
; Note Pad's np_dmov and ArtfulType's at_dmov are the same shape.
; -----------------------------------------------------------------------------
pt_pin:
    pushf
    push ax
    push dx
    xor ax, ax                      ; AX = 0: pin it again
    mov dx, [pt_base]
    call OSAPI_MEM_MOVABLE
    pop dx
    pop ax
    popf
    ret
pt_unpin:
    pushf
    push ax
    push dx
    mov ax, pt_reloc
    mov dx, [pt_base]
    call OSAPI_MEM_MOVABLE
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; pt_alloc_undo - claim an undo image the size of the canvas claim
; out: [pt_unseg]/[pt_undelta]/[pt_haveundo]; preserves all registers
;
; [pt_undelta] is a paragraph DELTA and not an address, so pt_urowset stays
; one add per row even though the two blocks are now unrelated claims that
; may sit either way round in memory (16-bit wraparound makes a negative
; delta work unchanged).
; -----------------------------------------------------------------------------
pt_alloc_undo:
    push ax
    push cx
    push dx
    mov byte [pt_haveundo], 0
    mov word [pt_unseg], 0
    mov cx, [pt_smaxp]
    call pt_kb_of
    call OSAPI_MEM_CLAIM
    jc .out
    mov [pt_unseg], dx
    call pt_movable
    sub dx, [pt_base]
    mov [pt_undelta], dx
    mov byte [pt_haveundo], 1
.out:
    call pt_menufix                 ; funded or not, the menu says which
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_alloc_clip - claim a clipboard: the canvas size if the heap will fund it,
;                 the floor if not, nothing if not even that
; out: [pt_cbseg]/[pt_cbparas]/[pt_haveclip]; preserves all registers
;
; The floor is what the GIF codec's tables need (PT_CLIPMINP), so a clipboard
; that shrank to it still leaves Save Gif working while Cut/Copy of a large
; selection starts refusing - which is what [pt_cbparas] already gates.
; -----------------------------------------------------------------------------
pt_alloc_clip:
    push ax
    push cx
    push dx
    mov byte [pt_haveclip], 0
    mov word [pt_cbseg], 0
    mov word [pt_cbparas], 0
    mov cx, PT_CLIPMINP             ; the FLOOR to start with: the GIF codec's
.try:                               ; tables need it and a paste needs nothing
                                    ; until something has been copied, so a
                                    ; canvas-sized clipboard nobody has used
                                    ; is 60KB of dead claim (pt_clip_need
                                    ; grows it when a Copy actually asks)
    call pt_kb_of
    call OSAPI_MEM_CLAIM
    jnc .got
    cmp cx, PT_CLIPMINP             ; already at the floor: no clipboard
    jbe .out
    mov cx, PT_CLIPMINP
    jmp short .try
.got:
    mov [pt_cbseg], dx
    call pt_movable
    mov cl, 6
    shl ax, cl
    mov [pt_cbparas], ax
    mov byte [pt_haveclip], 1
.out:
    call pt_menufix                 ; funded or not, the menu says which
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_clip_need - make the clipboard claim at least AX paragraphs
; in:  AX = paragraphs a Copy is about to need
; out: CF=0 the claim is big enough (it may have been replaced), CF=1 it could
;      not be grown and the old one is still intact; preserves all registers
;
; The clipboard starts at PT_CLIPMINP and grows here, the first time a Copy
; asks for more. Growing means claiming the bigger block BEFORE freeing the
; smaller one - the reverse would hand the memory back and then discover it
; had gone to someone else - so the peak is old + new, both of which are small.
; What is in the old clipboard is discarded either way, which is fine: this
; runs at the top of the Copy that is about to overwrite it.
; -----------------------------------------------------------------------------
pt_clip_need:
    push ax
    push cx
    push dx
    cmp ax, [pt_cbparas]
    jbe .ok
    mov cx, ax
    call pt_kb_of
    call OSAPI_MEM_CLAIM            ; the bigger one first...
    jc .no
    push dx
    call pt_free_clip               ; ...then give the smaller one back
    pop dx
    mov [pt_cbseg], dx
    call pt_movable
    mov cl, 6
    shl ax, cl
    mov [pt_cbparas], ax
    mov byte [pt_haveclip], 1
.ok:
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_free_undo / pt_free_clip - hand a claim back (SPEC.md 50.3)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_free_undo:
    push dx
    mov dx, [pt_unseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [pt_unseg], 0
    mov byte [pt_haveundo], 0
    mov byte [pt_undo_ok], 0
    call pt_menufix             ; the menu says so now
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; pt_alloc_lzw / pt_free_lzw - the GIF codec's tables, for one file only
; out: CF=0 and [pt_lzwseg] is a claim; CF=1 nothing was funded
;
; Taken at the top of a GIF and released at the bottom, so a Paint that is not
; converting a GIF holds none of it. 16KB is the READ direction's need; the
; write direction uses 10KB of the same block.
; -----------------------------------------------------------------------------
pt_alloc_lzw:
    push ax
    push dx
    mov word [pt_lzwseg], 0
    mov ax, PT_LZW_KB
    call OSAPI_MEM_CLAIM
    jc .no
    mov [pt_lzwseg], dx
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

pt_free_lzw:
    push dx
    mov dx, [pt_lzwseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [pt_lzwseg], 0
.out:
    pop dx
    ret

pt_free_clip:
    push dx
    mov dx, [pt_cbseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [pt_cbseg], 0
    mov word [pt_cbparas], 0
    mov byte [pt_haveclip], 0
    mov word [pt_cbw], 0
    call pt_menufix             ; the menu says so now
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; pt_growmax - the biggest canvas a NEW claim could fund, in paragraphs
; out: [pt_growp] set; preserves all registers
;
; What bounds a GROW, as opposed to what the block we already hold can carry.
; A grow re-bases (pt_resize), and re-basing needs the new block and the old
; one live at the same time - so the bound is the largest free RUN, not the
; total free. Assumes the clipboard survives, which it does at every size
; this can return.
; -----------------------------------------------------------------------------
pt_growmax:
    push ax
    push cx
    call OSAPI_MEM_AVAIL            ; AX = largest free run, KB
    cmp ax, PT_WANT_KB
    jbe .capped
    mov ax, PT_WANT_KB
.capped:
    mov cl, 6
    shl ax, cl                      ; KB -> paragraphs
    mov [pt_growp], ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_paras - the stride and the paragraph cost of a canvas
; in:  AX = width, DX = height
; out: BX = row stride in bytes, CX = paragraphs including the DIB header;
;      AX and DX preserved
;
; The stride is the BMP one - ceil(w/2) rounded up to 4 - because the canvas IS
; the file (SPEC.md 42). The byte total needs 32 bits: a 736x464 canvas is
; 171,000 bytes, so MUL's DX:AX is shifted down to paragraphs as a pair.
; -----------------------------------------------------------------------------
pt_paras:
    push ax
    push dx
    inc ax
    shr ax, 1                       ; ceil(w/2)...
    add ax, 3
    and ax, 0xFFFC                  ; ...rounded up to 4
    mov bx, ax
    mov ax, dx
    mul bx                          ; DX:AX = stride * height
    add ax, PT_BMPHDR + 15
    adc dx, 0
    mov cx, ax
    mov ax, dx
    mov dx, cx                      ; AX = high word, DX = low word
    mov cl, 12
    shl ax, cl
    mov cl, 4
    shr dx, cl
    or ax, dx
    mov cx, ax                      ; CX = paragraphs
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_layout - adopt a canvas size: stride and the row tables
; in:  AX = width, DX = height (already through pt_fit)
; out: [pt_cw], [pt_ch], [pt_stride], [pt_cvparas], pt_rowseg/pt_rowoff built;
;      preserves all registers
;
; The row tables are the whole of the addressing story. A canvas can be bigger
; than one 64KB segment - a 636x326 picture is 104KB - so a row is named by a
; (segment, offset) pair instead of a 16-bit offset: rowseg[y] is the paragraph
; it starts in, rowoff[y] the 0..15 bytes into that paragraph. The undo image
; has the identical layout [pt_undelta] paragraphs away, so its row segment is
; rowseg[y] + [pt_undelta] and there is no second table.
;
; **[pt_undelta] is pt_alloc_undo's to set, not this routine's.** It used to be
; stamped here as [pt_smaxp], from the era when one claim held the canvas and
; the undo image back to back; they are two independent claims now and may sit
; either way round, so writing the old constant here sent every undo row into
; whatever the heap put after the canvas.
; -----------------------------------------------------------------------------
pt_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pt_cw], ax
    mov [pt_ch], dx
    call pt_paras                   ; BX = stride, CX = paragraphs
    mov [pt_stride], bx
    mov [pt_cvparas], cx
    ; --- row 0 is the LAST row in the file, so the walk runs downward -------
    mov ax, [pt_ch]
    dec ax
    mul bx                          ; DX:AX = (ch-1) * stride
    add ax, PT_BMPHDR
    adc dx, 0
    mov si, ax                      ; DI:SI = row 0's byte offset, 32 bits
    mov di, dx
    xor bx, bx                      ; BX = y*2, the table index
    mov cx, [pt_ch]
.row:
    mov ax, si
    and ax, 15
    mov [pt_rowoff+bx], ax          ; 0..15 bytes into its paragraph...
    mov ax, di
    mov dx, si
    push cx
    mov cl, 12
    shl ax, cl
    mov cl, 4
    shr dx, cl
    pop cx
    or ax, dx
    add ax, [pt_base]
    mov [pt_rowseg+bx], ax          ; ...whose segment is [pt_base] + off>>4
    sub si, [pt_stride]             ; one row up in the file is one row down
    sbb di, 0                       ; in the picture
    inc bx
    inc bx
    loop .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_rowset  - point ES:DI at the first byte of canvas row AX
; pt_urowset - the same row of the undo image
; in:  AX = row (inside the canvas)
; out: ES:DI = that row's first byte; AX preserved, clobbers BX and flags
;
; Every loop that walks a row calls one of these once per row and then indexes
; ES:[DI + x>>1], so the table pair costs six instructions a row and nothing
; a pixel.
; -----------------------------------------------------------------------------
pt_rowset:
    push bx
    mov bx, ax
    add bx, bx
    mov di, [pt_rowoff+bx]
    mov es, [pt_rowseg+bx]
    pop bx
    ret

pt_urowset:
    push bx
    mov bx, ax
    add bx, bx
    mov di, [pt_rowoff+bx]
    mov bx, [pt_rowseg+bx]
    add bx, [pt_undelta]
    mov es, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_canvas_init - stamp the DIB header, white the picture
; in:  a layout already adopted (pt_layout)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_canvas_init:
    push ax
    call pt_bmp_hdr
    mov al, CWHITE
    call pt_wipe
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_wipe - fill the whole canvas with colour AL, without touching undo
; in:  AL = colour 0..15
; out: nothing; preserves all registers
;
; Row by row, because the canvas may span segments: one long rep stosw would
; wrap at 64KB and paint the start of the picture again.
; -----------------------------------------------------------------------------
pt_wipe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, al
    mov cl, 4
    shl al, cl
    or al, bl
    mov ah, al                      ; AX = the colour in all four nibbles
    mov dx, ax
    xor si, si                      ; SI = row
    cld
.row:
    mov ax, si
    call pt_rowset                  ; ES:DI = the row
    mov cx, [pt_stride]
    shr cx, 1                       ; whole words: the stride is a multiple of 4
    mov ax, dx
    rep stosw
    inc si
    cmp si, [pt_ch]
    jb .row
    pop es
    pop di
    pop si
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
    ; The 54 fixed bytes come from a template rather than two dozen immediate
    ; stores: the same bytes, a third of the code, and a header you can read as
    ; a header. Only four fields are not constant.
    mov ax, [pt_base]
    mov es, ax
    xor di, di
    mov si, pt_hdrtpl
    mov cx, 27
    cld
    rep movsw
    mov ax, [pt_ch]
    mul word [pt_stride]            ; DX:AX = pixel bytes - 32 bits, because a
    mov cx, ax                      ; big canvas is more than 64KB and bfSize
    mov si, dx                      ; has to carry all of it
    add ax, PT_BMPHDR
    mov [es:2], ax                  ; bfSize, low word...
    mov ax, si
    adc ax, 0
    mov [es:4], ax                  ; ...and high
    mov ax, [pt_cw]
    mov [es:18], ax                 ; biWidth
    mov ax, [pt_ch]
    mov [es:22], ax                 ; biHeight (positive = bottom-up)
    mov [es:34], cx                 ; biSizeImage, both words
    mov [es:36], si
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
; pt_font_init - remember where the kernel keeps its 8x8 glyphs
; in:  nothing
; out: [pt_fseg]:[pt_foff] -> glyph 32 (the space); preserves all registers
;
; The text tool writes glyph pixels into the CANVAS rather than onto the
; screen, so it needs the bitmaps themselves, not font_char. This used to be
; font_init's probe re-run inside the package - int 10h AX=1130h with the
; kernel's own F000:FA6E fallback behind it - forty lines to arrive at the
; table the kernel had already built. OSAPI_FONT_GLYPHS hands it over
; (SPEC.md 6/20.3), which also means the picture gets the same typeface the
; UI does on every adapter, rather than whatever the BIOS happens to hold.
;
; The glyphs stay where they are and are fetched eight bytes at a time
; (pt_gfetch) rather than copied into 760 bytes of our own bss, which is 3.8%
; of everything this package is allowed to be.
; -----------------------------------------------------------------------------
pt_font_init:
    push ax
    push cx
    push dx                         ; the slot answers the table's SEGMENT in
    push si                         ; DX now, and this proc preserves it
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL = the first code
    mov [pt_foff], si               ; it covers (32). The SEGMENT comes from
    mov [pt_fseg], dx               ; the call now - it is not KERNEL_SEG
    pop si
    pop dx
    pop cx
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
    push bx
    push cx                         ; CX too: pt_click calls this while CX/DX
    push dx                         ; still hold the click point
    cmp byte [pt_fsx], 0            ; the ONE place full screen is a different
    jne .fsx                        ; answer (SPEC.md 42.7) - everything else
                                    ; in this app, the palette, the divider,
                                    ; the canvas, the strip and the whole click
                                    ; ladder, is derived from the four words
                                    ; below and follows for free
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    push ax
    push dx
    call OSAPI_WM_GEOM              ; CX/DX = the CONTENT box (SPEC.md 41).
    mov [pt_contw], cx              ; This used to be [es:bx+W_W] - 2 and
    mov [pt_conth], dx              ; [es:bx+W_H] - TITLE_H - 1, open-coded
    pop dx                          ; here, which is the same arithmetic with
    pop ax                          ; nothing to say about a window that has
    jmp short .org                  ; no title bar
.fsx:
    call OSAPI_FSX_SURF             ; the bracket owns ONE DISPLAY, and it says
    jnc .fsok                       ; which (SPEC.md 53.7.1): AX = x, BX = y,
    xor ax, ax                      ; CX = w, DX = h. The window record still
    xor bx, bx                      ; describes a window - one nothing can see,
    mov cx, [pt_scrw]               ; and one the kernel puts back for us when
    mov dx, [pt_scrh]               ; the bracket ends
.fsok:                              ;
                                    ; IT USED TO BE (0,0) PLUS THE SCREEN, and
                                    ; that is only the same answer while the
                                    ; desktop is one display: a same-mode
                                    ; bracket does not collapse a two-display
                                    ; one (39.18.3), so (0,0) is the PRIMARY
                                    ; and Paint fullscreened on the second
                                    ; monitor drew its whole face over the
                                    ; OTHER one. The refusal path above is the
                                    ; historical answer, which cannot be
                                    ; reached from inside a bracket
    mov [pt_contw], cx
    mov [pt_conth], dx
    mov dx, bx                      ; ...and .org below wants the origin in
                                    ; AX/DX, which is WM_CONTENT's pairing
.org:
    mov [pt_ox], ax
    mov [pt_oy], dx
    add ax, PT_CV_X
    mov [pt_cx0], ax
    mov [pt_cy0], dx
    call pt_stripset
    ; --- the strip's right-hand controls are anchored to the right edge, clear
    ; --- of the grow box, and the swatch row takes what is left -------------
    mov ax, [pt_contw]
    sub ax, PT_GROW + PT_BTN_W16
    mov [pt_fntx], ax
    sub ax, PT_BTN_W16 + 2
    mov [pt_filx], ax
    sub ax, PT_SW_X + 4             ; pixels available to the swatches
    js .nosw
    ; --- narrow the swatches before dropping any of them ---------------------
    ; A colour you cannot see is worse than a colour you have to aim at, so the
    ; pitch shrinks from PT_SW_DX down to PT_SW_MIN first and only then does the
    ; count start to give.
    mov bx, ax                      ; BX = pixels available
    xor dx, dx
    mov cx, dx
    mov cl, [pt_ncol]
    mov ax, bx
    div cx                          ; AX = pitch that would fit them all
    cmp ax, PT_SW_DX
    jbe .pitch
    mov ax, PT_SW_DX                ; never wider than the design pitch
.pitch:
    cmp ax, PT_SW_MIN
    jae .havep
    mov ax, PT_SW_MIN               ; ...and never tighter than a fingertip
.havep:
    mov [pt_swdx], ax
    dec ax
    mov [pt_swsz], ax               ; one pixel of gap between them
    mov ax, bx
    xor dx, dx
    div word [pt_swdx]              ; how many fit at that pitch
    cmp ax, cx
    jbe .nsw
    mov ax, cx
.nsw:
    mov [pt_nsw], ax
    jmp short .out
.nosw:
    mov word [pt_nsw], 0
    mov word [pt_swdx], PT_SW_DX
    mov word [pt_swsz], PT_BW
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_stripset - where the bottom strip sits, content-relative
; in:  [pt_ch] = the canvas, [pt_conth] = the content box
; out: [pt_stripy]; preserves all registers
;
; Flush with the BOTTOM of the content, not one row under the canvas. In a
; WINDOW those are the same row by construction - [pt_conth] is the canvas plus
; the separator plus PT_STRIP_H - so this changes nothing there and the two
; expressions agree to the pixel. On the fullscreen surface (SPEC.md 42.7) they
; are 67 rows apart on every adapter, that being exactly the chrome a window
; costs and exactly what the canvas may NOT grow into, and a strip left
; floating in the middle of the screen with white below it reads as a broken
; layout rather than a deliberate one.
;
; It is also what keeps pt_strip_click honest: that ladder tests only that the
; click is at or below [pt_stripy] and never that it is above the strip's last
; row, which is safe precisely because the strip ends where the content does.
; -----------------------------------------------------------------------------
pt_stripset:
    push ax
    push bx
    mov ax, [pt_conth]
    sub ax, PT_STRIP_H
    mov bx, [pt_ch]
    inc bx                          ; the separator row is the canvas's last
    cmp ax, bx                      ; row plus one, and the strip can never be
    jge .set                        ; above it however short the content is
    mov ax, bx
.set:
    mov [pt_stripy], ax
    pop bx
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
    cmp ax, [pt_cw]
    jge .empty
    or ax, ax
    jns .x1ok
    xor ax, ax
.x1ok:
    mov [pt_cx1], ax
    mov ax, [pt_rx2]
    or ax, ax
    js .empty
    cmp ax, [pt_cw]
    jl .x2ok
    mov ax, [pt_cw]
    dec ax
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
; gfx_fill: the kernel's own clipping and its adapter dispatch
; (SPEC.md 32/39.5) then apply, so the same call is correct on all three
; adapters.
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

    cld
    mov bx, [pt_cy1]                ; BX = the row being filled
.row:
    mov ax, bx
    call pt_rowset                  ; ES:DI = the row's first byte
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
    cmp bx, [pt_cy2]
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
    cmp byte [pt_haveundo], 0
    je .out                         ; no undo image on this machine (SPEC 41.8)
    cmp byte [pt_undo_off], 0
    jne .out                        ; we ARE the undo: do not re-snapshot
    mov bx, ax                      ; BX = row counter
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
    ; --- copy the row. The two images share a row OFFSET and differ only by
    ; --- [pt_undelta] paragraphs, which is what keeps this to one table.
    mov ax, bx
    call pt_rowset                  ; ES:DI = the canvas row
    mov si, di                      ; the undo row sits at the same offset
    mov cx, [pt_stride]             ; read through DS while DS is still ours
    shr cx, 1
    mov ax, es
    mov bp, ax                      ; BP = source segment (a value, not a ptr)
    add ax, [pt_undelta]
    mov es, ax                      ; ES = the undo image's row
    push ds
    mov ds, bp
    rep movsw
    pop ds
.next:
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
    mov cx, PT_CH_MAX / 8
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
    mov dx, [pt_ch]
    dec dx                          ; DX = last row
    xor bx, bx                      ; BX = row counter
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
    mov ax, bx
    call pt_rowset                  ; ES:DI = the canvas row
    mov si, di
    mov cx, [pt_stride]
    shr cx, 1
    mov ax, es                      ; ES stays the canvas and DS becomes the
    add ax, [pt_undelta]            ; undo image, so the exchange is one loop
    push ds
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
    inc bx
    cmp bx, dx
    jbe .loop
    cmp word [pt_uy1], -1
    je .out
    mov word [pt_rx1], 0
    mov ax, [pt_cw]
    dec ax
    mov [pt_rx2], ax
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
; pt_dmg_get - what does this paint owe? (SPEC.md 11.90.2)
; in:  pt_org has run; gfx lock held
; out: [pt_dall] and, when it is 0, [pt_dx1]..[pt_dy2] CONTENT-relative
; preserves every register
;
; A SELECTION DISQUALIFIES US, and it is the one thing here that is not
; arithmetic: the marquee is an XOR and every paint re-shows it unconditionally,
; so a partial blit would leave the part outside the rect lit and the re-show
; would then invert it OFF. Whole content while one is live.
;
; The fullscreen bracket asks nothing either - there is no window damage there,
; the surface being the machine's (SPEC.md 42.7).
; -----------------------------------------------------------------------------
pt_dmg_get:
    push ax
    push bx
    push cx
    push dx
    mov byte [pt_dall], 1
    cmp byte [pt_fsx], 0
    jne .out
    cmp byte [pt_selon], 0
    jne .out
    mov bx, [pt_win]
    call OSAPI_WM_DAMAGE
    jc .out                         ; the whole content, which is every path
                                    ; with no damage rect behind it
    sub ax, [pt_ox]                 ; absolute -> content-relative, pt_cprep's
    sub cx, [pt_ox]                 ; conversion the other way round
    sub bx, [pt_oy]
    sub dx, [pt_oy]
    mov [pt_dx1], ax
    mov [pt_dy1], bx
    mov [pt_dx2], cx
    mov [pt_dy2], dx
    mov byte [pt_dall], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_dmg_hit - must this content-relative rect be drawn at all?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content-relative, inclusive)
; out: CF = 1 draw it; every register preserved
;
; PER ELEMENT, not per pixel - tm_row_draw's answer (SPEC.md 11.3). Each of this
; app's parts is small and self-contained, so the unit is the whole part; the
; canvas is the one thing worth narrowing, and pt_blit already takes a rect.
; -----------------------------------------------------------------------------
pt_dmg_hit:
    cmp byte [pt_dall], 0
    jne .yes
    cmp ax, [pt_dx2]
    jg .no
    cmp cx, [pt_dx1]
    jl .no
    cmp bx, [pt_dy2]
    jg .no
    cmp dx, [pt_dy1]
    jl .no
.yes:
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; pt_blit_dmg - the canvas, or only the part this paint owes (SPEC.md 11.90.2)
; in:  [pt_dall]/[pt_dx1]..[pt_dy2]; gfx lock held
; out: nothing; preserves every register
;
; No clamping here: pt_clip reads its four words as SIGNED, floors at 0, caps at
; the canvas and refuses an empty rect, so the raw content-minus-PT_CV_X
; arithmetic is already what it wants.
; -----------------------------------------------------------------------------
pt_blit_dmg:
    cmp byte [pt_dall], 0
    jne pt_blit_all                 ; tail call: all of it
    push ax
    mov ax, [pt_dx1]                ; content x -> canvas x; the canvas's rows
    sub ax, PT_CV_X                 ; ARE the content's, only x is offset
    mov [pt_rx1], ax
    mov ax, [pt_dx2]
    sub ax, PT_CV_X
    mov [pt_rx2], ax
    mov ax, [pt_dy1]
    mov [pt_ry1], ax
    mov ax, [pt_dy2]
    mov [pt_ry2], ax
    call pt_blit
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_blit - put a canvas rectangle on screen
; in:  [pt_rx1]..[pt_ry2] = canvas rect; gfx lock held
; out: nothing; preserves all registers
;
; The path for everything that cannot know what it changed: W_PAINT, undo,
; paste, a file load, and erasing the text caret. It is one OSAPI_GFX_BLIT4
; per BAND of rows - usually one call for the whole rectangle - and the
; kernel does the run coalescing, the clipping, the adapter dispatch and the
; renderer.
;
; This used to be the app's own coalescer (pt_runend, `repe scasb` over the
; packed bytes) emitting one OSAPI_GFX_HLINE per run. That was the right
; shape when a gfx call was near; since packages own a segment (SPEC.md 20.1)
; every one of them is a FAR call, and a detailed picture makes thousands per
; repaint. gfx_blit4 runs the identical scan from inside the kernel.
;
; WHAT THIS COSTS, AND IT IS NOT WHAT YOU WOULD GUESS (PERFORMANCE.md Part 9,
; measured on a real 5150). gfx_blit4 still emits one gfx_hline per RUN, and a
; gfx_hline costs about 0.5 ms on a 4.77MHz 8088 WHATEVER ITS LENGTH - a
; drawing call on the mono renderer is ~756 us of fixed setup with almost
; nothing per pixel. So a blit costs `runs x 0.5 ms` and the pixel count
; barely enters it: the same 64x64 block is 28 ms at one run per row and
; 561 ms at sixteen.
;
; So the picture's FLATNESS decides the repaint, not its size. Flat art at a
; couple of runs a row is a few hundred milliseconds for the whole canvas;
; dithered or photographic art at twenty runs a row is SECONDS. Moving the
; scan into the kernel did not change that - it removed a far call per run,
; and the run itself is still the unit of cost. Blitting only the band that
; changed is worth far more here than anything done to the scan.
;
; Two things about the geometry:
;
;  - the source pointer must start on an EVEN pixel, because gfx_blit4 takes
;    the first byte's high nibble as pixel 0. An odd left edge is therefore
;    widened by one column to the left. That column is inside the canvas and
;    inside the window (the frame always fits the picture, pt_wfollow), so it
;    is redrawn with what it already held.
;  - row y+1 sits one stride BELOW row y in memory - the canvas is stored as
;    the BMP it will be written as, bottom row first - so the stride handed
;    over is negative, and the band is addressed from its LAST row's
;    segment so no row's offset can go below zero. [pt_brmax] is how many
;    rows can share one segment that way; a 448x280 canvas is one band.
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
    mov ax, [pt_cx1]
    and ax, 0xFFFE                  ; even left edge (see above)
    mov [pt_bx0], ax
    mov bx, [pt_cx2]
    sub bx, ax
    inc bx
    mov [pt_bwid], bx               ; width in pixels
    mov ax, 65520                   ; rows that fit one segment, worst-case
    xor dx, dx                      ; 15-byte row offset included
    div word [pt_stride]
    or ax, ax
    jnz .rmok
    inc ax                          ; a stride past 64KB cannot happen (the
.rmok:                              ; canvas is clamped well below), but a
    mov [pt_brmax], ax              ; zero band height would not terminate
    mov ax, [pt_cy1]
    mov [pt_bsi], ax                ; the band's first row
.band:
    mov ax, [pt_cy2]
    sub ax, [pt_bsi]
    inc ax                          ; AX = rows left
    cmp ax, [pt_brmax]
    jbe .nok
    mov ax, [pt_brmax]
.nok:
    mov [pt_bn], ax                 ; AX = rows in this band
    add ax, [pt_bsi]
    dec ax                          ; ...whose LAST row is the lowest address
    call pt_rowset                  ; in it: ES:DI, and every other row of the
    mov ax, [pt_bn]                 ; band is a positive offset from there
    dec ax
    mul word [pt_stride]            ; AX = (n-1) strides (bounded by pt_brmax,
    add ax, di                      ; so DX is 0 and this cannot wrap)
    mov bx, [pt_bx0]
    shr bx, 1
    add ax, bx                      ; + the left edge, two pixels to a byte
    mov si, ax                      ; ES:SI = the band's FIRST row
    mov bp, [pt_stride]
    neg bp                          ; down the picture is up the file
    mov cx, [pt_bwid]
    mov dx, [pt_bn]
    mov ax, [pt_bx0]
    add ax, [pt_cx0]
    mov bx, [pt_bsi]
    add bx, [pt_cy0]
    call OSAPI_GFX_BLIT4
    mov ax, [pt_bsi]
    add ax, [pt_bn]
    mov [pt_bsi], ax
    cmp ax, [pt_cy2]
    jbe .band
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
    mov ax, [pt_cw]
    dec ax
    mov [pt_rx2], ax
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
    push di
    push es
    mov ax, dx
    call pt_rowset                  ; ES:DI = the row
    mov ax, cx
    shr ax, 1
    add di, ax
    mov al, [es:di]
    test cl, 1
    jnz .lo
    mov cl, 4
    shr al, cl
.lo:
    and al, 0x0F
    pop es
    pop di
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

; -----------------------------------------------------------------------------
; pt_cwell - a filled, framed box: the chrome's one repeated shape
; in:  AX = x, BX = y, CX = width-1, DX = height-1 (content coords);
;      [pt_pen] = the fill colour
; out: [pt_pen] = CBLACK (the frame's colour, which every caller wanted next);
;      preserves all registers
;
; Every button, well and swatch in the palette and the strip is this: fill in
; some colour, frame in black. Six sites, twelve bytes each.
; -----------------------------------------------------------------------------
pt_cwell:
    push cx
    push dx
    add cx, ax
    add dx, bx
    call pt_cfill
    mov byte [pt_pen], CBLACK
    call pt_cframe
    pop dx
    pop cx
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
    mov ax, dx
    add ax, PT_BW - 1
    cmp ax, [pt_ch]
    jge .done                       ; a short window shows fewer tools rather
    mov [pt_bx], cx                 ; than drawing over its own strip
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
    mov cx, PT_BW - 1
    mov dx, PT_BW - 1
    call pt_cwell
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
.done:
    call pt_draw_dims               ; the canvas size, under the last button
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_draw_dims - the canvas size, stacked in the palette column
; in:  [pt_cw], [pt_ch]; gfx lock held
; out: nothing; preserves all registers
;
; Here rather than in the title bar, and the reason is worth keeping: the
; kernel draws the title BEFORE it calls W_PAINT (SPEC.md 11), so a size
; discovered during the paint - which is exactly when a grow-box resize is
; discovered - would be one repaint stale there, and the only cure would be a
; second full repaint. This is content, painted by the same pass that adopted
; the new size, and it costs two font_str calls.
; -----------------------------------------------------------------------------
PT_DIM_Y    equ PT_PAL_Y0 + 4 * PT_PAL_DY + 2
; --- the size boxes, under the tools: two typable fields and Apply -------------
; They need 41 rows below the last tool button, which a 110-row CGA canvas does
; not have, so pt_szon decides per repaint whether they appear at all and the
; two-line readout is what shows when they do not (SPEC.md 42).
PT_SZ_Y     equ PT_DIM_Y            ; the width box's top row
PT_SZ_DY    equ 13                  ; ...and the height box's, one pitch down
PT_SZ_BH    equ 11                  ; box height (an 8px cell plus its border)
PT_SZ_BX    equ 10                  ; box left: the label sits to its left
PT_SZ_BW    equ 32                  ; three digits and a caret, framed
PT_SZ_AY    equ PT_SZ_Y + 2 * PT_SZ_DY + 2
PT_SZ_AH    equ 13                  ; the Apply button
PT_SZ_END   equ PT_SZ_AY + PT_SZ_AH ; rows the whole group needs   ; below the last button row

pt_draw_dims:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pt_szon
    jc .plain
    call pt_szdraw                  ; room for the real thing
    jmp .out
.plain:
    mov ax, PT_DIM_Y + 17
    cmp ax, [pt_ch]
    jge .out                        ; a short window has no room for it
    mov byte [pt_pen], CWHITE       ; erase first: 488 over 1024 would read as
    xor ax, ax                      ; 4884 otherwise
    mov bx, PT_DIM_Y
    mov cx, PT_SEPX - 1
    mov dx, PT_DIM_Y + 17
    call pt_cfill
    mov byte [pt_pen], CBLACK
    mov di, pt_dimbuf
    mov ax, [pt_cw]
    call pt_num
    mov byte [di], 0
    mov si, pt_dimbuf
    mov cx, 2
    mov dx, PT_DIM_Y
    call pt_ctext
    mov di, pt_dimbuf
    mov byte [di], 'x'
    inc di
    mov ax, [pt_ch]
    call pt_num
    mov byte [di], 0
    mov si, pt_dimbuf
    mov cx, 2
    mov dx, PT_DIM_Y + 9
    call pt_ctext
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_szon - are the size boxes on screen?
; out: CF=0 drawn and clickable, CF=1 not; preserves all registers
;
; One answer for the painter and the hit test both, which is what keeps a
; control that is not drawn from being clickable.
; -----------------------------------------------------------------------------
pt_szon:
    ; NOT gated on [pt_haveundo] any more. It was, on the reasoning that a
    ; machine with no undo image had a fixed canvas - true when the only way
    ; to restage an in-place resize was that buffer. It is not true now: a
    ; resize that MOVES to a new block stages in the old one, pt_resize asks
    ; for an undo image itself when it needs one, and a refusal it cannot
    ; avoid is a toast rather than a wipe. Hiding the boxes made a large
    ; canvas on a small machine unresizable and unexplained - the controls
    ; simply were not there.
    push ax
    mov ax, PT_SZ_END
    cmp ax, [pt_ch]
    pop ax
    jg .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; pt_szdraw - the two size boxes and the Apply button
; out: nothing; preserves all registers
;
; A box that is being typed into shows its edit buffer and a caret; every other
; box shows the live canvas size, so an abandoned edit is discarded the moment
; the focus moves and there is no second copy of the truth to keep in step.
; -----------------------------------------------------------------------------
pt_szdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [pt_pen], CWHITE       ; a clean bed: 488 over 1024 would read as
    xor ax, ax                      ; 4884 otherwise
    mov bx, PT_SZ_Y
    mov cx, PT_SEPX - 1
    mov dx, PT_SZ_END - 1
    call pt_cfill
    mov byte [pt_szi], 1
.box:
    ; --- the value: typed, or the canvas's own --------------------------------
    mov si, pt_wtxt
    mov ax, [pt_cw]
    cmp byte [pt_szi], 1
    je .val
    mov si, pt_htxt
    mov ax, [pt_ch]
.val:
    mov [pt_szp], si
    mov cl, [pt_fbox]
    cmp cl, [pt_szi]
    je .drawn
    mov di, si
    call pt_num
    mov byte [di], 0
.drawn:
    mov ax, PT_SZ_Y
    cmp byte [pt_szi], 1
    je .at
    add ax, PT_SZ_DY
.at:
    mov [pt_szy], ax
    ; --- the label ------------------------------------------------------------
    mov byte [pt_pen], CBLACK
    mov si, pt_s_wlab
    cmp byte [pt_szi], 1
    je .lab
    mov si, pt_s_hlab
.lab:
    mov cx, 1
    mov dx, [pt_szy]
    add dx, 2
    call pt_ctext
    ; --- the field ------------------------------------------------------------
    mov ax, PT_SZ_BX
    mov bx, [pt_szy]
    mov cx, PT_SZ_BX + PT_SZ_BW - 1
    mov dx, bx
    add dx, PT_SZ_BH - 1
    call pt_cframe
    mov si, [pt_szp]
    mov cx, PT_SZ_BX + 2
    mov dx, [pt_szy]
    add dx, 2
    call pt_ctext
    ; --- and the caret, on the box that has the keyboard ----------------------
    mov cl, [pt_fbox]
    cmp cl, [pt_szi]
    jne .next
    mov si, [pt_szp]
    call OSAPI_FONT_WIDTH           ; AX = the digits' pixel width
    add ax, PT_SZ_BX + 2
    mov bx, [pt_szy]
    inc bx
    mov cx, ax
    mov dx, bx
    add dx, PT_SZ_BH - 3
    call pt_cfill
.next:
    inc byte [pt_szi]
    cmp byte [pt_szi], 3
    jb .box
    ; --- Apply, the full width of the column ----------------------------------
    ; The one standard labelled button in Paint, and so the one thing here
    ; that is the shared control (apps/os88ui.inc). Everything else in this
    ; module's chrome is pt_cwell - a filled well with a 16x16 icon and no
    ; label - which is deliberately not a System-1 button.
    ;
    ; ONE PIXEL: the label's y was the literal +3 in a PT_SZ_AH = 13 box and
    ; the shared control centres at (13-8)/2 = 2, so it sits one row higher.
    ; Horizontally it already WAS centred and did not look it - 'Apply' is
    ; 40px in a 42px button, so the x of 1 is (42-40)/2 exactly.
    ;
    ; No OS88UI_FILL: pt_szdraw white-fills the whole group before the first
    ; box, so this always lands on clean ground.
    xor ax, ax
    add ax, [pt_ox]                 ; the rect, in SCREEN coordinates - which
    mov [pt_brect+0], ax            ; is what pt_cframe/pt_ctext were adding
    add ax, PT_SEPX - 2             ; per primitive
    mov [pt_brect+4], ax
    mov ax, PT_SZ_AY
    add ax, [pt_oy]
    mov [pt_brect+2], ax
    add ax, PT_SZ_AH - 1
    mov [pt_brect+6], ax
    mov si, pt_s_apply
    mov bx, pt_brect
    xor di, di
    call os88ui_btn
    mov byte [pt_pen], CBLACK       ; os88ui_btn leaves the KERNEL's pen live;
                                    ; [pt_pen] is this module's own and the
                                    ; two are not the same variable
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_brect - the Apply button's rect, screen coordinates. ONE, because
; pt_szdraw draws one button and nothing hit-tests it against a stored rect:
; pt_onclick has its own band (SPEC.md 42).
; -----------------------------------------------------------------------------
pt_brect:   dw 0, 0, 0, 0

; -----------------------------------------------------------------------------
; pt_szval - a NUL digit string at SI as a number
; out: AX = the value, 0 if the string is empty; preserves all other registers
; -----------------------------------------------------------------------------
pt_szval:
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    mov cx, 10
.d:
    mov bl, [si]
    or bl, bl
    jz .out
    cmp bl, '0'
    jb .out
    cmp bl, '9'
    ja .out
    mul cx                          ; three digits at most, so DX stays 0
    sub bl, '0'
    xor bh, bh
    add ax, bx
    inc si
    jmp short .d
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_szapply - the Apply button, and Enter in either box
; in:  gfx lock held; out: nothing; preserves all registers
;
; The same pt_setsize a grow-box drag goes through, so the crop guard, the
; clamps and the toast are all shared - typing 900 into a box on a machine that
; cannot fund it is exactly a drag that asked for too much.
; -----------------------------------------------------------------------------
pt_szapply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [pt_fbox], 0
    mov si, pt_wtxt
    call pt_szval
    or ax, ax
    jnz .havew
    mov ax, [pt_cw]                 ; an empty box means "leave this one"
.havew:
    mov bx, ax
    mov si, pt_htxt
    call pt_szval
    or ax, ax
    jnz .haveh
    mov ax, [pt_ch]
.haveh:
    mov dx, ax
    mov ax, bx
    call pt_setsize
    pushf                           ; CF = an axis was held back. That is NOT
    cmp byte [pt_szchg], 0          ; the same as "nothing happened": a grow
    je .nomove                      ; that shortens too takes the half it can,
    cmp byte [pt_fs], 0             ; and skipping the frame here left the
    jne .fsdraw                     ; window at one size and the canvas at
    mov bx, [pt_win]                ; another (SPEC.md 11.1) - unless we are
    mov cx, [pt_cw]                 ; FULL SCREEN, where the record is not what
    add cx, PT_CHROME_W             ; anything lays out from (pt_org answers the
    mov dx, [pt_ch]                 ; screen, SPEC.md 42.7) and still holds the
    add dx, PT_CHROME_H             ; windowed size to come back TO. pt_fsx_main
    call OSAPI_WM_RESIZE            ; settles it on the way out; here, just draw
    jmp short .said
.fsdraw:
    call pt_repaint
    jmp short .said
.nomove:
    call pt_draw_dims               ; the boxes go back to the live size
.said:
    popf
    jnc .out
    call pt_szsi                    ; ...and the toast goes on top of whichever
    call pt_msg_show                ; of the two just drew
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_szkey - a keystroke into the focused size box
; in:  AL = ascii; gfx lock held; out: nothing; preserves all registers
;
; Digits, backspace, Enter to apply and Escape to give up. The first digit after
; a box is clicked REPLACES what is there ([pt_fresh]), which is what makes
; retyping a size two keystrokes instead of four.
; -----------------------------------------------------------------------------
pt_szkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp al, 13
    je .apply
    cmp al, 27
    je .cancel
    cmp al, 8
    je .bs
    cmp al, '0'
    jb .out
    cmp al, '9'
    ja .out
    call pt_szbuf                   ; SI = this box's buffer
    cmp byte [pt_fresh], 0
    je .append
    mov byte [si], 0
    mov byte [pt_fresh], 0
.append:
    xor bx, bx
.len:
    cmp byte [si+bx], 0
    je .put
    inc bx
    cmp bx, 3
    jb .len
    jmp short .out                  ; three digits covers the whole range
.put:
    mov [si+bx], al
    inc bx
    mov byte [si+bx], 0
    call pt_draw_dims
    jmp short .out
.bs:
    call pt_szbuf
    mov byte [pt_fresh], 0
    xor bx, bx
.blen:
    cmp byte [si+bx], 0
    je .bcut
    inc bx
    jmp short .blen
.bcut:
    or bx, bx
    jz .out
    dec bx
    mov byte [si+bx], 0
    call pt_draw_dims
    jmp short .out
.cancel:
    mov byte [pt_fbox], 0
    call pt_draw_dims
    jmp short .out
.apply:
    call pt_szapply
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_szbuf - SI = the focused box's edit buffer; clobbers SI and flags
pt_szbuf:
    mov si, pt_wtxt
    cmp byte [pt_fbox], 1
    je .out
    mov si, pt_htxt
.out:
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
    mov cx, [pt_contw]
    dec cx
    mov dx, bx
    call pt_cfill
    mov byte [pt_pen], CWHITE
    xor ax, ax
    mov bx, [pt_ch]                 ; the bed starts just under the canvas, not
    inc bx                          ; at the strip: on the fullscreen surface
    mov cx, [pt_contw]              ; pt_stripset moves the strip to the bottom
    dec cx                          ; of the content and the gap it leaves is
    mov dx, [pt_stripy]             ; ours to erase. In a window the two rows
    add dx, PT_STRIP_H - 1          ; are the same and the fill is unchanged
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
    mov cx, PT_BW - 1
    mov dx, PT_BW - 1
    call pt_cwell
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
    mov cx, PT_BW - 1
    mov dx, PT_BW - 1
    call pt_cwell

    ; --- the palette ------------------------------------------------------
    xor di, di
.sw:
    mov ax, di
    mul word [pt_swdx]
    add ax, PT_SW_X
    mov [pt_bx], ax
    mov ax, di
    call pt_swcol
    mov [pt_pen], al
    mov ax, [pt_bx]
    mov bx, [pt_stripy]
    inc bx
    mov cx, [pt_swsz]
    dec cx
    mov dx, PT_BW - 1
    call pt_cwell
    mov ax, di                      ; the swatch in use gets a second frame
    call pt_swcol
    cmp al, [pt_col]
    jne .swnext
    mov ax, [pt_bx]
    dec ax
    mov bx, [pt_stripy]
    mov cx, ax
    add cx, [pt_swsz]
    inc cx
    mov dx, bx
    add dx, PT_BW + 1
    call pt_cframe
.swnext:
    inc di
    cmp di, [pt_nsw]
    jb .sw

    ; --- filled-shapes toggle, and the font scale -------------------------
    mov byte [pt_pen], CWHITE
    mov ax, [pt_filx]
    mov bx, [pt_stripy]
    inc bx
    mov cx, PT_BTN_W16 - 1
    mov dx, PT_BW - 1
    call pt_cwell
    mov ax, [pt_filx]
    add ax, 4
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
    mov ax, [pt_fntx]
    mov bx, [pt_stripy]
    inc bx
    mov cx, PT_BTN_W16 - 1
    mov dx, PT_BW - 1
    call pt_cwell
    mov al, [pt_fscale]
    add al, '0'
    mov [pt_fdigit], al
    mov si, pt_fdigit
    mov cx, [pt_fntx]
    add cx, 4
    mov dx, [pt_stripy]
    add dx, 7
    call pt_ctext
    ; --- and the grow box, which lives in this strip -------------------------
    ; The white bed above erases it, and the kernel draws it BEFORE W_PAINT
    ; (SPEC.md 11.1), so every path that repaints the strip owes it a redraw -
    ; not just the paint proc. Putting it here covers the tool, colour, width
    ; and toggle clicks in one place; leaving it to pt_paint alone made the box
    ; vanish until the next full repaint.
    call pt_growbox
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_growbox - put the grow box back, if this screen has one
; in:  gfx lock held; out: nothing; preserves all registers
;
; The gate is why this is a routine rather than two call sites: inside the
; SPEC.md 53 bracket there is no grow box, because the window manager is not
; drawing this screen at all (SPEC.md 42.7). wm_grow_paint would draw one at
; the RECORD's corner - a window nobody can see, somewhere in the middle of
; the canvas - and it is not an error there, so nothing would say so. It was
; a stray icon on the CGA canvas until this gate covered both callers.
; -----------------------------------------------------------------------------
pt_growbox:
    cmp byte [pt_fsx], 0
    jne .out
    push bx
    mov bx, [pt_win]
    call OSAPI_WM_GROW
    pop bx
.out:
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
    call pt_track                   ; did ui_grow resize the window under us?
    call pt_dmg_get                 ; ...and what do we owe? (SPEC.md 11.90.2)
    call pt_bands                   ; ...AFTER that, never before: wm_damage's
                                    ; answer above was computed from the
                                    ; extents the cache was laid out for, and
                                    ; a canvas resized by the size boxes moves
                                    ; the strip without moving the WINDOW - so
                                    ; a new extent set here would inset the
                                    ; rect we have already been given
    call pt_fsbed                   ; the beds nothing below covers - the tool
                                    ; column's and any band right of the canvas.
                                    ; WINDOWED THIS USED TO BE THE KERNEL'S
                                    ; white fill; under WF_OWNBG (SPEC.md
                                    ; 11.90.1) it is ours, and this routine was
                                    ; already exactly it for the 53 bracket.
                                    ; It gates its own two fills
    xor ax, ax                      ; the tool column: the palette's buttons sit
    xor bx, bx                      ; on the bed pt_fsbed just laid
    mov cx, PT_SEPX - 1
    mov dx, [pt_ch]
    dec dx
    call pt_dmg_hit
    jnc .nopal
    call pt_draw_pal
.nopal:
    xor ax, ax                      ; the colour strip, below the canvas: its
    mov bx, [pt_ch]                 ; own separator row is its top
    mov cx, [pt_contw]
    dec cx
    mov dx, [pt_conth]
    dec dx
    call pt_dmg_hit
    jnc .nostrip
    call pt_draw_strip
.nostrip:
    mov ax, PT_SEPX                 ; the palette/canvas divider, one column
    xor bx, bx
    mov cx, ax
    mov dx, [pt_ch]
    dec dx
    call pt_dmg_hit
    jnc .nosep
    mov byte [pt_pen], CBLACK
    call pt_cfill
.nosep:
    call pt_blit_dmg                ; ...and the canvas, narrowed to the rect -
                                    ; the one part worth narrowing rather than
                                    ; gating, and 96% of this paint (Set 32)
    mov byte [pt_selshown], 0
    call pt_marq                    ; the marquee, if a selection is live - and
                                    ; pt_dmg_get asked for the whole content if
                                    ; there is one, because this re-show is an
                                    ; XOR (SPEC.md 11.90.2)
    cmp byte [pt_abon], 0           ; ...and the About card over the lot
    je .noab
    call pt_abdraw
.noab:
    cmp byte [pt_apend], 0          ; a refused resize, deferred to here
    je .out
    cmp byte [pt_apend], 2          ; 2 = pt_onsize held an axis back, so the
    je .justsay                     ; frame the kernel drew is already right
    mov byte [pt_apend], 0
    mov bx, [pt_win]
    call OSAPI_WM_FRONT             ; the corrected frame, over a clean desktop
    cmp byte [pt_apsay], 0
    je .out                         ; the frame was corrected but the canvas
    call pt_szsi                    ; moved: no notice is owed (pt_track)
    call pt_msg_show
    jmp short .out
.justsay:
    mov byte [pt_apend], 0
    cmp byte [pt_apsay], 0
    je .out
    call pt_szsi
    call pt_msg_show
    jmp short .out
.notice:
    mov byte [pt_pen], CWHITE       ; UNDER WF_OWNBG the kernel fills nothing,
    xor ax, ax                      ; and two lines of black text is not every
    xor bx, bx                      ; pixel of anything (SPEC.md 11.90.1)
    mov cx, [pt_contw]
    dec cx
    mov dx, [pt_conth]
    dec dx
    call pt_cfill
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
; pt_msg_show - say one line, through the system toast (SPEC.md 59)
; in:  SI = NUL string
; out: nothing; preserves all registers
;
; It used to be a framed white box at the canvas's top-left corner - which
; sat ON the picture, cost a canvas blit to hide, and could not survive a
; repaint. The kernel's is an inverse-video strip in the MENU BAR, so it
; costs this app no pixels at all, needs no hiding, and takes itself down
; after about three seconds. pt_msg_hide went with the box: an interaction
; no longer has to retire anything, because nothing here is covering the
; user's work.
; -----------------------------------------------------------------------------
pt_msg_show:
    push cx
    push es
    push ds
    pop es                          ; the kernel COPIES it (SPEC.md 59.3)
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
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
; also hold the lock for the whole stroke, so every other drawer would be
; starved while
; being drawn. Releasing it on every mouse sample instead is what puts the
; ink on the glass, and the yield is what keeps the clock ticking.
;
; pt_wait_tick is the idle form: when the pointer has not moved there is
; nothing to draw, and polling at tick rate costs nothing.
;
; Inside the SPEC.md 53 bracket both become OSAPI_FSX_WAIT, and that is not a
; substitution of convenience: the gfx lock in there is the CALLER's, taken
; before OSAPI_FSX_RUN and released after it returns (SPEC.md 53.6), and
; unlocking it mid-bracket would hand the mouse ISR a screen the app owns.
; The flush comes back with it - gfx_unlock is where the SPEC.md 32 flush
; lives, so fsx_wait is the only pacing a
; tracking loop gets, which is exactly what SPEC.md 53.5 built it for. What
; is given up is the yield: a bracket has nothing to yield TO, every other
; task being frozen, so the pace is one pass per tick either way.
; -----------------------------------------------------------------------------
pt_wait:
    cmp byte [pt_fsx], 0
    je .win
    cmp byte [pt_mono], 0           ; in the bracket on a 1bpp adapter there is
    jne .nowait                     ; nothing to wait FOR: no frame clock is
    jmp pt_fswait                   ; possible (SPEC.md 32), so nothing is owed
.nowait:                            ; a present, and the lock is the caller's
    ret                             ; and is not being released either way.
                                    ; fsx_wait's tick would put a 55 ms floor
                                    ; under every mouse sample of a stroke -
                                    ; slower than the WINDOW, which yields
.win:
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    ret

; pt_fswait - the bracket's form of both waits: the frame clock AND the flush
pt_fswait:
    push ax
    xor al, al                      ; 0 = return at the next [ticks] change
    call OSAPI_FSX_WAIT
    pop ax
    ret

pt_wait_tick:
    cmp byte [pt_fsx], 0
    jne pt_fswait
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

; =============================================================================
; Full screen - the SPEC.md 53 bracket, and NOT the SPEC.md 11.2 surface
;
; ONE call: OSAPI_FSX_RUN. There is no OSAPI_FULLSCREEN anywhere in this file,
; and that is the decision SPEC.md 42.7 records rather than an omission.
;
; The obvious design is 11.2 THEN the bracket - Missile Command's (SPEC.md
; 48.13) - and it is genuinely tidier: wm_fullscreen writes the screen's
; geometry into the window RECORD, so pt_org's OSAPI_WM_GEOM and
; OSAPI_WM_CONTENT answer the whole screen by themselves and the palette, the
; divider, the canvas, the strip and the entire click ladder follow with no
; fullscreen branch anywhere. It was built that way first. What kills it is
; coming HOME: SPEC.md 53.6 step 4 owes the desktop one wm_paint_all, the
; window is still WF_FULL when it runs, so that repaint draws the FULL-SCREEN
; Paint - which the OSAPI_FULLSCREEN AL=0 after it throws away and repaints
; again as a window. Counted at gfx_blit4/gfx_fill/font_char over one round
; trip, leaving cost 3 full-canvas draws and 1,496 fills against 1 and 770.
;
; So the geometry lives HERE instead, in one branch in one routine: pt_org
; answers origin (0,0) and content [pt_scrw] x [pt_scrh] while [pt_fsx] is
; set, and asks the kernel otherwise. Two things follow from the kernel not
; knowing - pt_repaint skips OSAPI_WM_GROW (wm_grow_paint would put a grow box
; at the record's own corner, a window nobody can see) and the first paint
; clears its own bed (pt_fsbed: there is no wm_draw_win white fill in front of
; it).
;
; It is a SAME-MODE bracket (SPEC.md 53.7): no OSAPI_FSX_MODE call, so every
; drawing slot stays legal and one body serves VGA, Hercules and CGA. That is
; also why nothing here consults OSAPI_FSX_CAPS and why the menu item never
; greys - the caps mask answers a question about MODES and this sets none, so
; there is no fact to grey on (SPEC.md 47 rule 3). What the bracket buys is
; exclusivity, and for a paint program that is the largest item there is:
; pt_wait runs once per mouse sample of every stroke and windowed it is an
; unlock/yield/lock round trip, which PERFORMANCE.md Set 4 priced at 21.8% of
; a Missile Command session with no pixel of the game in it. A drag here
; issues more of those than a game frame does, and on top of it every one
; repaints the system arrow twice.
;
; What the bracket costs is two things a window gets for free, and they are the
; only new code below: the pointer (the held lock keeps the mouse ISR off the
; screen for the whole session, SPEC.md 53.6) and the input model (no events
; are dispatched, so the loop polls int 16h and OSAPI_MOUSE and calls this
; app's own W_ONKEY and W_ONCLICK with the arguments the kernel would have).
; =============================================================================

PT_PTR_R    equ 2                   ; half-side: the pointer is a 5x5 square

; -----------------------------------------------------------------------------
; The bracket's pointer - a SQUARE, and the reason is the call count
;
; It was a crosshair, two XOR bars with the centre pixel left as a hole by the
; double XOR - Missile Command's (SPEC.md 48.18), and the shape a bitmap editor
; wants. On the field machine it flickered, and priced against PERFORMANCE.md
; Part 2 it is easy to see why: a move is four gfx calls and 32 scan lines,
; 4 x 756us of ARRIVING plus 32 x 177us = 8.7 ms, against a 20 ms Hercules
; frame. Nearly half of every refresh caught a partly-updated pointer.
;
; **The fixed cost per call is the lever, not the size.** A shorter crosshair
; only touches the 177us term: R=3 is still four calls and still 5.9 ms. One
; rect is two calls and, at 5x5, ten scan lines - 3.3 ms, 2.6x better - and
; that is the whole of why the shape changed. The centre hole goes with it,
; which was the property worth losing.
;
; The square keeps SPEC.md 42.7.1's rule: a move writes every pixel at most
; once, and the pixels the two squares SHARE are not touched at all, two XORs
; being the identity. pt_ptr_sub is that subtraction - the part of one rect the
; other does not cover, as up to four strips, of which two are ever non-empty
; for equal squares. A move that clears the old square entirely (any move of
; more than PT_PTR_R*2 in either axis, which most are) degenerates to one rect
; each way by itself, and that is the two-call case.
; -----------------------------------------------------------------------------

; pt_ptr_rect - the clipped pointer square about (AX, DX), into the four words
; at DS:DI - left, top, right, bottom
; out: nothing; preserves all registers
;
; IT CLIPS TO THE SURFACE AND NOT TO THE SCREEN. Bracket-only code (pt_ptr_on
; and its two siblings are the only callers, and [pt_ptron] gates all three),
; so [pt_ox]/[pt_oy] plus [pt_contw]/[pt_conth] IS the surface - which on a
; two-display desktop is one display at its own virtual origin (SPEC.md 53.7.1)
; and not 0..[pt_scrw]-1. Clamping to the screen there gave a pointer that
; could not reach the left edge of its own display and ran past the right.
pt_ptr_rect:
    push ax
    push bx
    push dx
    mov bx, ax
    sub bx, PT_PTR_R
    cmp bx, [pt_ox]
    jge .l
    mov bx, [pt_ox]
.l:
    mov [di], bx
    mov bx, ax
    add bx, PT_PTR_R
    mov ax, [pt_ox]
    add ax, [pt_contw]
    dec ax
    cmp bx, ax
    jle .r
    mov bx, ax
.r:
    mov [di+4], bx
    mov bx, dx
    sub bx, PT_PTR_R
    cmp bx, [pt_oy]
    jge .t
    mov bx, [pt_oy]
.t:
    mov [di+2], bx
    mov bx, dx
    add bx, PT_PTR_R
    mov ax, [pt_oy]
    add ax, [pt_conth]
    dec ax
    cmp bx, ax
    jle .b
    mov bx, ax
.b:
    mov [di+6], bx
    pop dx
    pop bx
    pop ax
    ret

; pt_ptr_strip - XOR (AX,BX)-(CX,DX) if it is a non-empty rect
; out: nothing; preserves all registers
pt_ptr_strip:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    push si
    push di
    call OSAPI_GFX_XOR_FILL
    pop di
    pop si
.out:
    ret

; pt_ptr_sub - XOR the part of the rect at DS:SI that the rect at DS:DI does
; NOT cover; both are (left, top, right, bottom) words
; out: nothing; preserves all registers
;
; Four strips - left of the overlap, right of it, and the top and bottom of
; what is left between them - and the empty ones cost a compare. Two squares
; of the same size can only overlap in a corner or an edge, so at most two of
; the four are ever drawn; no overlap at all is the whole rect in one.
pt_ptr_sub:
    push ax
    push bx
    push cx
    push dx
    mov ax, [si]                    ; the overlap, or the whole-rect exit
    cmp ax, [di]
    jge .ovl
    mov ax, [di]
.ovl:
    mov cx, [si+4]
    cmp cx, [di+4]
    jle .ovr
    mov cx, [di+4]
.ovr:
    cmp ax, cx
    jg .whole
    mov bx, [si+2]
    cmp bx, [di+2]
    jge .ovt
    mov bx, [di+2]
.ovt:
    mov dx, [si+6]
    cmp dx, [di+6]
    jle .ovb
    mov dx, [di+6]
.ovb:
    cmp bx, dx
    jg .whole
    mov [pt_ovl], ax
    mov [pt_ovr], cx
    mov [pt_ovt], bx
    mov [pt_ovb], dx
    mov ax, [si]                    ; left of the overlap, full height
    mov cx, [pt_ovl]
    dec cx
    mov bx, [si+2]
    mov dx, [si+6]
    call pt_ptr_strip
    mov ax, [pt_ovr]                ; right of it, full height
    inc ax
    mov cx, [si+4]
    mov bx, [si+2]
    mov dx, [si+6]
    call pt_ptr_strip
    mov ax, [pt_ovl]                ; above it, between the two
    mov cx, [pt_ovr]
    mov bx, [si+2]
    mov dx, [pt_ovt]
    dec dx
    call pt_ptr_strip
    mov ax, [pt_ovl]                ; and below it
    mov cx, [pt_ovr]
    mov bx, [pt_ovb]
    inc bx
    mov dx, [si+6]
    call pt_ptr_strip
    jmp short .out
.whole:
    mov ax, [si]
    mov bx, [si+2]
    mov cx, [si+4]
    mov dx, [si+6]
    call pt_ptr_strip
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_ptr_xor - draw or erase the whole square at [pt_ptrx],[pt_ptry]
; in:  gfx lock held; out: nothing; preserves all registers
;
; XOR is its own inverse, so drawing and erasing are the same call. That is
; what makes [pt_ptrx]/[pt_ptry] load-bearing: they are BANKED and replayed
; rather than re-read from the mouse, because an erase at a position the draw
; never used leaves the square on the picture for the rest of the session.
pt_ptr_xor:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [pt_ptrx]
    mov dx, [pt_ptry]
    mov di, pt_pnew
    call pt_ptr_rect
    mov ax, [pt_pnew]
    mov bx, [pt_pnew+2]
    mov cx, [pt_pnew+4]
    mov dx, [pt_pnew+6]
    call pt_ptr_strip
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_ptr_on / pt_ptr_off - show or hide it, idempotent
; in:  gfx lock held; out: nothing; preserves all registers except FLAGS
pt_ptr_on:
    cmp byte [pt_ptron], 0
    jne .out
    mov byte [pt_ptron], 1
    call pt_ptr_xor
.out:
    ret

pt_ptr_off:
    cmp byte [pt_ptron], 0
    je .out
    mov byte [pt_ptron], 0
    call pt_ptr_xor
.out:
    ret

; -----------------------------------------------------------------------------
; pt_ptr_move - follow the mouse, writing every pixel at most once
; in:  CX/DX = the new screen position; gfx lock held
; out: nothing; preserves all registers except FLAGS
;
; SPEC.md 42.7.1, which is SPEC.md 7.1.2's rule in an app's own overlay. This
; was erase-then-draw, so every pixel the two positions SHARE was written
; twice - dark, and then lit again - and the glass catches the value in
; between. On a real Hercules that is the pointer flickering as the mouse
; moves, the same defect and the same symptom the kernel cursor had before
; cur_move (docs/FIELD-NOTES.md 6).
;
; **The NEW square's own pixels go first**, which costs nothing and is the
; other half. Any move bigger than the square leaves the two disjoint, so the
; difference is "all of the new, then all of the old" - and in that order the
; pointer is never ABSENT, only briefly doubled. An absence reads as a blink;
; a double reads as movement.
;
; A pointer that is HIDDEN still tracks: the position is banked without
; drawing, so the show that follows a dispatch puts it where the mouse
; actually is rather than where it was when it went away.
; -----------------------------------------------------------------------------
pt_ptr_move:
    cmp cx, [pt_ptrx]
    jne .move
    cmp dx, [pt_ptry]
    je .out                         ; a still pointer costs one compare
.move:
    cmp byte [pt_ptron], 0
    je .bank
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pt_pnx], cx
    mov [pt_pny], dx
    mov ax, [pt_ptrx]
    mov dx, [pt_ptry]
    mov di, pt_pold
    call pt_ptr_rect
    mov ax, [pt_pnx]
    mov dx, [pt_pny]
    mov di, pt_pnew
    call pt_ptr_rect
    mov si, pt_pnew                 ; the new square's own pixels...
    mov di, pt_pold
    call pt_ptr_sub
    mov si, pt_pold                 ; ...and only then the old square's
    mov di, pt_pnew
    call pt_ptr_sub
    mov ax, [pt_pnx]
    mov [pt_ptrx], ax
    mov ax, [pt_pny]
    mov [pt_ptry], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.bank:
    mov [pt_ptrx], cx
    mov [pt_ptry], dx
.out:
    ret

; -----------------------------------------------------------------------------
; pt_cmd_fs - take the screen: the fullscreen surface, then the bracket
; in:  gfx lock held (menu command or W_ONKEY context, UI task)
; out: nothing; preserves all registers. Does not return until the user
;      leaves full screen - OSAPI_FSX_RUN is a bracket, not a latch
; -----------------------------------------------------------------------------
pt_cmd_fs:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [pt_mode], PT_M_LIVE   ; a notice window has nothing to show big
    jne .out
    cmp byte [pt_fs], 0             ; already ours - unreachable while the
    jne .out                        ; bracket is up, and cheap to answer
    mov byte [pt_fs], 1             ; set BEFORE the bracket: the W_PAINT that
                                    ; fsx_restore runs on the way home is
                                    ; inside OSAPI_FSX_RUN, and it must already
                                    ; know the frame is not ours to rewrite
    mov ax, pt_fsx_main
    mov bx, [pt_win]
    xor cx, cx                      ; no KEEPWORKER: Paint claims no worker
    call OSAPI_FSX_RUN              ; task, so there is nothing to keep alive
    pushf
    mov byte [pt_fs], 0             ; pt_fsx_main clears it too; this is for
    popf                            ; the path where the bracket never ran
    jnc .out
    mov si, pt_s_nofsx              ; refused, and nothing has changed: no
    call pt_msg_show                ; screen was taken and nothing was drawn
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, gfx lock held for the
;      whole session, every other task frozen
; out: near ret = leave the bracket; the kernel restores the desktop
;
; The editor's own callbacks, driven by a polled loop instead of the event
; ladder, and that is the whole of it: a press becomes pt_click with the
; mouse's own ABSOLUTE screen coordinates, which is exactly the pair W_ONCLICK
; is handed, and a key becomes pt_onkey with int 16h's own AX, which is exactly
; what W_ONKEY is handed. Nothing else in the app is told the difference.
;
; The pointer comes OFF around every dispatch and back on after it. Both a
; click and a key may draw anywhere on the screen - a stroke, a repaint, a
; toast - and an XOR overlay that has been drawn over can never be erased, only
; smeared. Turning it off for the whole of a tracking loop is not a compromise
; either: the ink or the rubber band IS the feedback while the button is down,
; and it is where the pointer is.
; -----------------------------------------------------------------------------
pt_fsx_main:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [pt_fsx], 1
    mov byte [pt_ptron], 0          ; nothing of ours is on this screen yet
    mov bx, [pt_win]                ; ONE draw, and it is the app's ordinary
    call pt_org                     ; one at a new size: pt_org now answers
    call pt_track                   ; the whole screen, pt_track grows the
    call pt_repaint                 ; canvas into it (memory only - pt_resize
                                    ; never draws), and pt_repaint is the same
                                    ; body W_PAINT runs - it forces [pt_dall]
                                    ; and lays the bed itself, so a pt_fsbed
                                    ; here would only fill it a second time.
                                    ; Coming home costs the one wm_paint_all
                                    ; fsx_restore owes the desktop anyway, so a
                                    ; round trip is two full-screen redraws and
                                    ; neither is spare
    call OSAPI_MOUSE                ; AL = buttons, CX/DX = screen position
    mov [pt_pbtn], al               ; the click or key that got us here must
    mov [pt_ptrx], cx               ; not read as a fresh press on the first
    mov [pt_ptry], dx               ; pass
    call pt_ptr_on
.loop:
    mov ah, 1                       ; the bracket's input model: poll int 16h
    int 0x16                        ; (this IS the UI task, SPEC.md 53.1)
    jz .nokey
    xor ah, ah
    int 0x16
    call pt_fsx_key
    jc .done
    jmp .loop                       ; drain the buffer before looking at the
.nokey:                             ; mouse - typing must not lag a still hand
    call OSAPI_MOUSE
    mov ah, [pt_pbtn]
    mov [pt_pbtn], al
    not ah
    and ah, al                      ; AH = buttons pressed since the last look
    test ah, 1
    jnz .press
    call pt_ptr_move
    jmp short .pace
.press:
    call pt_ptr_off
    mov [pt_ptrx], cx               ; bank the press point: pt_click's tracking
    mov [pt_ptry], dx               ; loops move the mouse before we look again
    mov si, [pt_win]
    call pt_click                   ; W_ONCLICK's own arguments, verbatim
    call OSAPI_MOUSE                ; a tracking loop ran the button down to its
    mov [pt_pbtn], al               ; release, so the state it left behind is
    call pt_ptr_move                ; the truth - and the pointer is wherever
    call pt_ptr_on                  ; the drag ended
.pace:
    call pt_fswait                  ; one pass a tick, and the present with it
    jmp .loop
.done:
    call pt_ptr_off                 ; the crosshair dies on THIS screen. The
    mov byte [pt_fsx], 0            ; desktop the kernel is about to repaint
                                    ; never had one, and an XOR erase against
                                    ; it would put one there for good
    ; --- settle the WINDOW before anything paints it ------------------------
    ; fsx_restore's wm_paint_all is the next thing to run and it draws us from
    ; the record, so the record has to be right BEFORE it, not after. It used
    ; to be after: pt_track shrank the canvas during that repaint and, when the
    ; shrink was refused because it would crop artwork, drew a canvas bigger
    ; than the window at the window's own origin - over the frame and out onto
    ; the desktop - and only then did pt_cmd_fs square the frame with
    ; OSAPI_WM_RESIZE and repaint it properly. Two repaints, the first of them
    ; visibly wrong. Here there is no paint in flight, so pt_wfix may write the
    ; frame (which is the whole reason SPEC.md 42 keeps it), and the one
    ; repaint that follows is the correct one.
    mov byte [pt_fs], 0             ; ...which also means pt_track is allowed
    mov bx, [pt_win]                ; to call pt_wfix again
    call pt_org                     ; the window's content box, not the screen
    call pt_track
    cmp byte [pt_apend], 1          ; pt_track asks for the repaint it cannot
    jne .home                       ; know is already coming; 2 is "say it,
    mov byte [pt_apend], 2          ; redraw nothing"
.home:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_fsx_key - the bracket's keyboard
; in:  AX = int 16h's (ascii, scan); gfx lock held
; out: CF=1 leave the bracket; preserves all registers
;
; Everything W_ONKEY does, plus the two keys that only mean something in here.
;
; F is the tree's fullscreen key (SPEC.md 11.2.1): the key that got you here is
; the key that leaves, the same binding Missile Command and Tracker carry. It
; is a BARE letter, so it is offered only while nothing else wants the
; keystroke - the text tool takes printable characters and a size box takes
; its own - which is exactly the gate pt_type already applies, read here
; instead of there. Ctrl+F is the unconditional door and stays the one the
; menu item names, because it is the one that is true in every state.
;
; Escape is offered LAST and only when there is nothing else for it to do: it
; already ends a text run, drops a selection and abandons a size-box edit, and
; answering a half-typed caption by throwing the user out of full screen would
; be the wrong answer to the same keypress.
; -----------------------------------------------------------------------------
pt_fsx_key:
    push ax
    push si
    cmp al, 0x06                    ; Ctrl+F, whatever else is going on
    je .leave
    cmp al, 'f'                     ; ...and the bare letter whenever nothing
    je .bare                        ; else is taking it
    cmp al, 'F'
    je .bare
    cmp al, 27
    jne .pass
    cmp byte [pt_txton], 0          ; Escape with something to cancel is a
    jne .pass                       ; cancel, and only then a way out
    cmp byte [pt_selon], 0
    jne .pass
    cmp byte [pt_fbox], 0
    jne .pass
    jmp short .leave
.bare:
    cmp byte [pt_txton], 0          ; a caption is being typed: this is an 'f'
    jne .pass
    cmp byte [pt_fbox], 0           ; a size box has the keyboard (pt_onkey
    jne .pass                       ; routes to pt_szkey before the canvas)
.leave:
    pop si
    pop ax
    stc
    ret
.pass:
    call pt_ptr_off                 ; a key may draw anywhere: the click path's
    mov si, [pt_win]                ; rule, for the same reason
    call pt_onkey                   ; W_ONKEY's own arguments, verbatim
    call pt_ptr_on
    pop si
    pop ax
    clc
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
    call pt_abdismiss               ; a click anywhere takes the credits down,
    jc .out                         ; and is spent doing it
    mov al, [pt_fbox]               ; a click may move the keyboard focus, and
    mov [pt_fbold], al              ; the group has to be redrawn if it does
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
    mov byte [pt_fbox], 0
    call pt_strip_click
    jmp short .out
.canvas:
    sub cx, PT_CV_X                 ; -> canvas coords
    mov [pt_ax], cx
    mov [pt_ay], dx
    mov byte [pt_fbox], 0
    call pt_canvas_click
.out:
    mov al, [pt_fbox]
    cmp al, [pt_fbold]
    je .done
    call pt_draw_dims
.done:
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
    mov ax, dx
    add ax, PT_BW - 1
    cmp ax, [pt_ch]
    jge .out                        ; not drawn, so not clickable
    mov ax, [pt_hity]
    sub ax, dx
    js .next
    cmp ax, PT_BW
    jge .next
    ; hit: switch tool
    call pt_text_end
    call pt_sel_drop
    mov byte [pt_fbox], 0           ; and the size boxes lose the keyboard
    mov ax, di
    mov [pt_tool], al
    call pt_draw_pal
    call pt_draw_strip              ; the width set belongs to the tool
    jmp short .out
.next:
    inc di
    cmp di, PT_NTOOL
    jb .tool
    ; --- below the tools: the size boxes and Apply ---------------------------
    call pt_szon
    jc .drop
    mov ax, [pt_hitx]
    sub ax, PT_SZ_BX
    js .apply
    cmp ax, PT_SZ_BW
    jge .apply
    mov ax, [pt_hity]
    sub ax, PT_SZ_Y
    js .apply
    cmp ax, PT_SZ_BH
    jl .fw
    sub ax, PT_SZ_DY
    js .apply
    cmp ax, PT_SZ_BH
    jge .apply
    mov byte [pt_fbox], 2
    jmp short .focus
.fw:
    mov byte [pt_fbox], 1
.focus:
    mov byte [pt_fresh], 1          ; the first digit replaces the value
    jmp short .out
.apply:
    mov ax, [pt_hity]
    sub ax, PT_SZ_AY
    js .drop
    cmp ax, PT_SZ_AH
    jge .drop
    mov ax, [pt_hitx]
    cmp ax, PT_SEPX - 1
    jge .drop
    call pt_szapply
    jmp short .out
.drop:
    mov byte [pt_fbox], 0           ; anywhere else in the column
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
    xor dx, dx
    div word [pt_swdx]
    cmp ax, [pt_nsw]
    jge .toggles
    cmp dx, PT_BW
    jge .out
    cmp dx, [pt_swsz]               ; in the gap between two swatches
    jge .out
    call pt_swcol                   ; AX = swatch index -> AL = colour
    mov [pt_col], al
    call pt_draw_strip
    jmp short .out
.toggles:
    mov ax, cx
    sub ax, [pt_filx]
    js .out
    cmp ax, PT_BTN_W16
    jge .fontbtn
    xor byte [pt_filled], 1
    call pt_draw_strip
    jmp short .out
.fontbtn:
    mov ax, cx
    sub ax, [pt_fntx]
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
    call pt_segdo                   ; walks [pt_wx],[pt_wy] to the new point
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
; pt_segdo - draw one stroke segment, by whichever route this brush allows
; in:  [pt_wx],[pt_wy] = the start, [pt_tox],[pt_toy] = the end; lock held
; out: [pt_wx],[pt_wy] = the end; preserves all registers
;
; A ONE-PIXEL brush swept along a line IS a line, so its screen half can be a
; single OSAPI_GFX_LINE (SPEC.md 5.6) instead of a gfx_fill per pixel. That is
; the whole of SPEC.md 42.8, and on the field machine it is the difference
; between a pencil that follows the mouse and one that cannot: pt_seg draws the
; brush's leading edge per step, which at width 1 is one 1x1 fill - 756us of
; ARRIVING plus 177us - and a second whenever the minor axis moves. A 300-pixel
; chord is ~400 calls and 373 ms, which gives the pencil a MAXIMUM DRAWABLE
; SPEED of about 1,000 px/s. A hand drawing a circle passes that on the fast
; part of the arc, and past it the lag runs away: the longer the chord, the
; longer it takes to draw, the further the hand has gone. The visible result is
; a circle that comes out as a few long straight chords with whole arcs missing
; (docs/FIELD-NOTES.md 11). One gfx_line is ~160us a pixel and one call, so the
; same chord is 48 ms and the ceiling moves to ~6,000 px/s.
;
; A WIDER brush keeps pt_seg, and that is not laziness: a swept square is not a
; line, and pt_seg's leading-edge sweep already writes each new pixel exactly
; once, which is why the 32px eraser is usable at all.
; -----------------------------------------------------------------------------
pt_segdo:
    cmp byte [pt_mono], 0           ; 1bpp ONLY, and that is a fact about the
    je pt_seg                       ; KERNEL, not a preference: gfx_line_raw
                                    ; sends a mono adapter to gfx_line_mono and
                                    ; VGA to gfx_line_runs, and the two do not
                                    ; agree pixel for pixel. The walk below
                                    ; mirrors the mono one exactly (verified,
                                    ; SPEC.md 42.8); against the VGA one the
                                    ; same test says 663 bytes. The machine
                                    ; that needs this is the 4.77MHz one and it
                                    ; is mono, so the gate costs nothing real -
                                    ; but it is a gate, not a coincidence
    cmp word [pt_blo], 0            ; width 1 - blo and bhi are both 0 only
    jne pt_seg                      ; for a one-pixel dab
    cmp word [pt_bhi], 0
    jne pt_seg
    push ax
    mov ax, [pt_wx]                 ; the walk is about to move these
    mov [pt_lsx0], ax
    mov ax, [pt_wy]
    mov [pt_lsy0], ax
    mov ax, [pt_wx]                 ; can the SCREEN half be one call? gfx_line
    call pt_lcanx                   ; clips to the SCREEN, not to the canvas,
    jc .perpix                      ; so a stroke dragged off the picture would
    mov ax, [pt_tox]                ; be drawn straight through the tool
    call pt_lcanx                   ; palette. Testing the two ENDS is enough:
    jc .perpix                      ; the canvas is a box and a box is convex
    mov ax, [pt_wy]
    call pt_lcany
    jc .perpix
    mov ax, [pt_toy]
    call pt_lcany
    jc .perpix
    mov byte [pt_noscr], 1          ; the walk writes the canvas and the undo
    call pt_lineseg                 ; image; the screen is the one call below
    mov byte [pt_noscr], 0
    call pt_lndraw
    pop ax
    ret
.perpix:
    call pt_lineseg                 ; same rasterization, screen per pixel -
    pop ax                          ; slow, but it is the same shape, and only
    ret                             ; a stroke leaving the canvas takes it

; pt_lcanx / pt_lcany - CF=1 if AX is off the canvas on that axis
; out: CF; preserves all registers
pt_lcanx:
    or ax, ax
    js .bad
    cmp ax, [pt_cw]
    jge .bad
    clc
    ret
.bad:
    stc
    ret

pt_lcany:
    or ax, ax
    js .bad
    cmp ax, [pt_ch]
    jge .bad
    clc
    ret
.bad:
    stc
    ret

; pt_lndraw - the segment's screen half, in one call
; in:  [pt_lsx0],[pt_lsy0] = the start, [pt_wx],[pt_wy] = the end; lock held
; out: nothing; preserves all registers
pt_lndraw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [pt_ink]
    call OSAPI_SET_COLOR
    mov ax, [pt_lsx0]
    add ax, [pt_cx0]
    mov bx, [pt_lsy0]
    add bx, [pt_cy0]
    mov cx, [pt_wx]
    add cx, [pt_cx0]
    mov dx, [pt_wy]
    add dx, [pt_cy0]
    xor si, si                      ; thin: nothing here erases a line drawn in
    call OSAPI_GFX_LINE             ; segments, so 5.6.5's dilation is not owed
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_lineseg - walk a one-pixel segment into the canvas, THE KERNEL'S WAY
; in:  [pt_wx],[pt_wy] = the start, [pt_tox],[pt_toy] = the end
; out: [pt_wx],[pt_wy] = the end; preserves all registers
;
; **This is gfx_line's own rasterization, and it has to be.** The canvas is
; what a repaint draws from, so a canvas walked one way and a screen drawn
; another disagree - and they are not close: the two forms put almost every
; pixel of a chord in a different place, so the stroke on the glass would snap
; to a different shape at the next repaint. Measured before this existed: 3,015
; differing bytes over twelve strokes, against 0 with the fast path off.
;
; So the arithmetic below is vga12.inc's, verbatim: normalise so y1 <= y2
; (swapping BOTH ends), dy >= 0, sx the sign of dx, err = dx - dy, and one e2
; per iteration read by both axis tests - which is what makes it ONE Bresenham
; rather than two independent ones, and is exactly where the DDA form this
; replaces differed. The walk therefore runs DOWNWARD whichever way the drag
; went, which costs nothing: what is being painted is a SET of pixels, and the
; brush's own position is restored from [pt_tox],[pt_toy] at the end.
;
; The start pixel is plotted, where pt_seg's sweep skips it (the previous
; segment ended there). Re-plotting it in the same ink is free and it is what
; keeps the walk identical to gfx_line's, which draws both endpoints.
; -----------------------------------------------------------------------------
pt_lineseg:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_wx]
    mov bx, [pt_wy]
    mov cx, [pt_tox]
    mov dx, [pt_toy]
    cmp bx, dx                      ; SIGNED: y1 <= y2, or swap both ends
    jle .ord
    xchg ax, cx
    xchg bx, dx
.ord:
    mov [pt_lx], ax
    mov [pt_ly], bx
    mov [pt_lx2], cx
    mov [pt_ly2], dx
    sub dx, bx                      ; dy, and it cannot be negative now
    mov [pt_ldy], dx
    mov word [pt_lsgx], 1
    sub cx, ax                      ; dx = |x2 - x1|, sx = its sign
    jns .dxok
    neg cx
    mov word [pt_lsgx], -1
.dxok:
    mov [pt_ldx], cx
    sub cx, dx                      ; err = dx - dy
    mov [pt_lerr], cx
.pixel:
    call pt_lpix
    mov ax, [pt_lx]
    cmp ax, [pt_lx2]
    jne .step
    mov ax, [pt_ly]
    cmp ax, [pt_ly2]
    je .done
.step:
    mov cx, [pt_lerr]               ; e2 = 2 * err; BOTH tests read the same
    add cx, cx                      ; e2, which is what makes it one Bresenham
    mov ax, [pt_ldy]
    neg ax
    cmp cx, ax                      ; e2 > -dy: step x
    jle .noxs
    mov ax, [pt_lerr]
    sub ax, [pt_ldy]
    mov [pt_lerr], ax
    mov ax, [pt_lx]
    add ax, [pt_lsgx]
    mov [pt_lx], ax
.noxs:
    cmp cx, [pt_ldx]                ; e2 < dx: step y
    jge .pixel
    mov ax, [pt_lerr]
    add ax, [pt_ldx]
    mov [pt_lerr], ax
    inc word [pt_ly]
    jmp .pixel
.done:
    mov ax, [pt_tox]                ; the walk may have run against the drag;
    mov [pt_wx], ax                 ; the BRUSH ends where the drag ended
    mov ax, [pt_toy]
    mov [pt_wy], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pt_lpix - one canvas pixel at [pt_lx],[pt_ly], through the rect path so the
; clip to the canvas and the undo marking are the ones every other tool uses
; out: nothing; preserves all registers
pt_lpix:
    push ax
    mov ax, [pt_lx]
    mov [pt_rx1], ax
    mov [pt_rx2], ax
    mov ax, [pt_ly]
    mov [pt_ry1], ax
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
    cmp ax, [pt_cw]
    jl .out
    mov ax, [pt_cw]
    dec ax
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
; package's OSAPI_GFX_XOR_RECT is a persistent draw when double
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
    mov ax, [pt_cbw]                ; will it fit the clipboard? The buffer is
    inc ax                          ; whatever was left over after the canvas
    shr ax, 1                       ; and its undo image, at least 16KB
    mov dx, [pt_cbh]
    mul dx                          ; DX:AX = packed bytes
    add ax, 4 + 15
    adc dx, 0
    mov cx, ax
    mov ax, dx
    mov dx, cx
    mov cl, 12
    shl ax, cl
    mov cl, 4
    shr dx, cl
    or ax, dx                       ; AX = paragraphs
    call pt_clip_need               ; grow the claim if this needs more
    jnc .room
    mov word [pt_cbw], 0            ; it would not grow: no clipboard rather
    mov word [pt_msgp], pt_s_bigsel ; than a corrupted one
    jmp short .out
.room:
    mov ax, [pt_cbseg]
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
    mov ax, [pt_cbseg]
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
    cmp cx, [pt_cw]
    jge .out
    mov [pt_setval], al             ; pt_rowset wants AX for the row
    mov ax, dx
    call pt_rowset                  ; ES:DI = the row
    mov bx, cx
    shr bx, 1
    add di, bx
    mov al, [pt_setval]
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
    push di
    push es
    mov ax, dx
    call pt_urowset                 ; ES:DI = the undo image's row
    mov ax, cx
    shr ax, 1
    add di, ax
    mov al, [es:di]
    test cl, 1
    jnz .lo
    mov cl, 4
    shr al, cl
.lo:
    and al, 0x0F
    pop es
    pop di
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
    cmp byte [pt_undo_ok], 0
    je .out                         ; nothing was ever snapshotted: with no undo
    call pt_clip                    ; image, backspace cannot uncover anything
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
    mov ax, [pt_fy]
    call pt_rowset                  ; ES = the row's segment...
    mov bp, di                      ; ...and BP its offset
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
    mov ax, [pt_cw]
    dec ax
    cmp [pt_fr], ax
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
    call pt_rect                    ; pt_rect preserves ES, but it walked other
    mov ax, [pt_fy]                 ; rows through it: re-point at ours
    call pt_rowset
    mov bp, di
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
    mov ax, [pt_scseg]
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
    mov ax, [pt_scseg]
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
; pt_gfetch - one glyph's eight rows, out of the ROM font into pt_gl8
; in:  AX = its byte offset within [pt_fseg]
; out: pt_gl8 filled; preserves all registers
; -----------------------------------------------------------------------------
pt_gfetch:
    push ax
    push bx
    push si
    push es
    mov si, ax
    mov es, [pt_fseg]
    xor bx, bx
.b:
    mov al, [es:si]
    mov [pt_gl8+bx], al
    inc si
    inc bx
    cmp bx, 8
    jb .b
    pop es
    pop si
    pop bx
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
    cmp ax, [pt_cw]
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
    add ax, [pt_foff]               ; [pt_gch] counts from the space, and so
                                    ; does the kernel's table (FONT_FIRST)
    call pt_gfetch                  ; ES belongs to the canvas the moment
    mov si, pt_gl8                  ; pt_rect starts drawing, so the glyph
                                    ; comes out of ROM before that, not during
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
    call pt_abdismiss               ; ...and so does any key
    jc .out
    mov ax, [pt_key]
    cmp byte [pt_fbox], 0           ; a size box has the keyboard: it gets the
    je .canvas                      ; digits, not the text tool
    call pt_szkey
    jmp .out
.canvas:
    cmp al, 27
    je .esc
    cmp al, 8
    je .back
    cmp ah, 0x53                    ; grey Delete
    je .del
    cmp al, 13
    je .enter
    ; --- the control codes int 16h hands us for Ctrl+letter ------------------
    cmp al, 0x1A                    ; Ctrl+Z
    je .undo
    cmp al, 0x03                    ; Ctrl+C
    je .copy
    cmp al, 0x18                    ; Ctrl+X
    je .cut
    cmp al, 0x16                    ; Ctrl+V
    je .paste
    cmp al, 0x06                    ; Ctrl+F
    je .full
    cmp byte [pt_txton], 0          ; ...and the bare F, the tree's fullscreen
    jne .print                      ; key (SPEC.md 11.2.1), whenever the text
    cmp al, 'f'                     ; tool is not taking the keystroke. pt_type
    je .full                        ; drops it in that state anyway, so this
    cmp al, 'F'                     ; costs the canvas nothing it was using
    je .full
.print:
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
    jmp short .out
    ; --- the same routines the Edit menu calls, so the two doors cannot drift
.undo:
    call pt_undo_cmd
    jmp short .out
.copy:
    call pt_cmd_copy
    jmp short .out
.cut:
    call pt_cmd_cut
    jmp short .out
.paste:
    call pt_cmd_paste
    jmp short .out
.full:
    call pt_cmd_fs                  ; the View menu's own routine: the two
.out:                               ; doors onto one command cannot drift
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
PT_MF_SAVE   equ 2                  ; the format is the VERB, not the extension:
PT_MF_SAVEAS equ 3                  ; each of these four sets [pt_sfmt] and the
PT_MF_SAVEG  equ 4                  ; name's extension follows it (pt_setext)
PT_MF_SAVEAG equ 5
PT_ME_UNDO   equ 0                  ; Edit
PT_ME_CUT    equ 1
PT_ME_COPY   equ 2
PT_ME_PASTE  equ 3
PT_ME_CLEAR  equ 4
PT_MD_FILL   equ 0                  ; Draw
PT_MD_F1     equ 1
PT_MD_F2     equ 2
PT_MD_F4     equ 3
PT_MV_FULL   equ 0                  ; View

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
    mov ax, [pt_key]
    cmp ah, 1
    je .edit
    cmp ah, 2
    je .draw
    cmp ah, 3
    je .view
    or ah, ah
    jnz .out
; --- File ------------------------------------------------------------------
    cmp al, PT_MF_NEW
    je .new
    cmp al, PT_MF_OPEN
    je .open
    cmp al, PT_MF_SAVE
    je .save_bmp
    cmp al, PT_MF_SAVEAS
    je .saveas_bmp
    cmp al, PT_MF_SAVEG
    je .save_gif
    cmp al, PT_MF_SAVEAG
    je .saveas_gif
.out:
    ret
.new:
    call pt_new
    ret
.save_gif:
    mov byte [pt_sfmt], 1
    jmp short .gifchk
.save_bmp:
    mov byte [pt_sfmt], 0
.save:
    cmp byte [pt_name], 0
    je .saveas                      ; never named: ask, do not guess
    cmp byte [pt_trunc], 0
    je .save_go
    mov word [pt_msgp], pt_s_trunc  ; the file holds more than we loaded: one
    mov si, [pt_msgp]               ; click must not throw the rest away
    call pt_msg_show
    ret
.save_go:
    mov si, pt_s_saving             ; an encode runs before the floppy does and
    call pt_msg_show                ; the file-activity widget cannot see it;
                                    ; SPEC.md 59.4 gets this on the glass
    call pt_save                    ; pt_save only SETS its toast: the dialog's
    mov si, [pt_msgp]               ; completion callback is what shows it on the
    or si, si                       ; Save As path, and this path has none
    jz .out
    call pt_msg_show                ; (which replaces "Saving..." )
    ret
.open:
    mov al, FDLG_OPEN
    jmp short .dlg
.saveas_gif:
    mov byte [pt_sfmt], 1
    push ax
    mov al, 1
    call pt_gate
    pop ax
    jc .out
    jmp short .saveas
.gifchk:
    push ax
    mov al, 1
    call pt_gate
    pop ax
    jc .out
    jmp short .save
.saveas_bmp:
    mov byte [pt_sfmt], 0
.saveas:
    cmp byte [pt_name], 0           ; offer the dialog the name the verb will
    je .askfmt                      ; actually write, extension and all
    call pt_setext
.askfmt:
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
    jmp pt_undo_cmd
.cut:
    jmp pt_cmd_cut
.copy:
    jmp pt_cmd_copy
.paste:
    jmp pt_cmd_paste
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
; --- View ------------------------------------------------------------------
.view:
    cmp al, PT_MV_FULL
    jne .out
    jmp pt_cmd_fs                   ; does not return until the user leaves,
                                    ; and repaints on the way out - so, like
                                    ; the dialog above, NO repaint after it

; =============================================================================
; What a smaller machine does without (SPEC.md 42)
;
; `pt_geom` decides once, from int 12h, and nothing can change it later: the
; buffer bases are fixed from the largest canvas the machine can fund, so the
; undo image is always big enough for any canvas `pt_fit` will allow and the
; clipboard's size is a constant. There is deliberately no re-check on load or
; resize - there is nothing that could have changed.
;
; An unavailable command KEEPS its own label, gains "(NoRam)" after it and is
; marked MENU_DIS so the kernel greys it and refuses to select it (SPEC.md
; 12.2). The label still has to say what the command would do, and the greying
; is what says it cannot right now. The command itself still answers with a
; toast, because the keyboard shortcut never goes near a menu - both doors
; have to be closed.
;
; Open is never one of them. The reader needs somewhere to put the file, and
; when there is no undo image it borrows the scratch area's flood-fill stack,
; which is idle during a load: 12KB still opens a small picture, and a file too
; big for it comes back as the file API's own FERR_BIG.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_menufix - relabel the commands this machine cannot run
; out: nothing; preserves all registers
;
; The item tables are ordinary words in our own image, so this is a handful of
; stores; the kernel reads them at draw time, so any point before the first
; paint will do.
; -----------------------------------------------------------------------------
pt_menufix:
    ; BOTH ways, and called whenever a capability moves - not once at startup.
    ; It used to set the disabled labels only, from the entry proc, so a
    ; clipboard or undo image handed back to fund a bigger canvas left the
    ; menu still offering what it could no longer do; and one re-funded later
    ; stayed greyed for the rest of the session. The kernel reads a
    ; menu set LIVE out of the owning segment (SPEC.md 12.2), so a store here
    ; is enough - there is nothing to re-register.
    cmp byte [pt_haveclip], 0
    je .noclip
    mov word [pt_it_edit + 2 * PT_ME_CUT], pt_i_cut
    mov word [pt_it_edit + 2 * PT_ME_COPY], pt_i_copy
    mov word [pt_it_edit + 2 * PT_ME_PASTE], pt_i_paste
    jmp short .undo
.noclip:
    mov word [pt_it_edit + 2 * PT_ME_CUT], pt_i_cut2
    mov word [pt_it_edit + 2 * PT_ME_COPY], pt_i_copy2
    mov word [pt_it_edit + 2 * PT_ME_PASTE], pt_i_paste2
.undo:
    ; The GIF items follow the LZW claim, which is taken per file and so
    ; cannot be tested here - what decides them is whether the heap could
    ; fund PT_LZW_KB at all, which is also what pt_save will ask.
    push ax
    push bx                         ; MEM_AVAIL answers in AX *and* BX, and
    call OSAPI_MEM_AVAIL            ; AX = largest free run, KB
    cmp ax, PT_LZW_KB               ; this routine promises to preserve both
    pop bx
    pop ax
    jb .nogif
    mov word [pt_it_file + 2 * PT_MF_SAVEG], pt_i_saveg
    mov word [pt_it_file + 2 * PT_MF_SAVEAG], pt_i_saveag
    jmp short .undo2
.nogif:
    mov word [pt_it_file + 2 * PT_MF_SAVEG], pt_i_saveg2
    mov word [pt_it_file + 2 * PT_MF_SAVEAG], pt_i_saveag2
.undo2:
    cmp byte [pt_haveundo], 0
    je .noundo
    mov word [pt_it_edit + 2 * PT_ME_UNDO], pt_i_undo
    ret
.noundo:
    mov word [pt_it_edit + 2 * PT_ME_UNDO], pt_i_undo2
    ret

; -----------------------------------------------------------------------------
; pt_gate - is this feature funded on this machine?
; in:  AL = 0 undo / 1 clipboard
; out: CF=0 go ahead; CF=1 and the toast is already up; preserves all registers
; -----------------------------------------------------------------------------
pt_gate:
    push si
    or al, al
    jnz .clip
    mov si, pt_s_nu
    cmp byte [pt_haveundo], 0
    jmp short .test
.clip:
    mov si, pt_s_nc
    cmp byte [pt_haveclip], 0
.test:
    jne .ok
    call pt_msg_show
    pop si
    stc
    ret
.ok:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; pt_cmd_copy / pt_cmd_cut / pt_cmd_paste - the gated Edit commands
; in:  gfx lock held; out: nothing; preserves all registers
;
; One routine per command, reached by BOTH the menu item and the Ctrl key, so
; the gate cannot be present on one path and missing on the other.
; -----------------------------------------------------------------------------
pt_cmd_copy:
    push ax
    mov al, 1
    call pt_gate
    jc .out
    call pt_copy
.out:
    pop ax
    ret

pt_cmd_cut:
    push ax
    mov al, 1
    call pt_gate
    jc .out
    call pt_copy
    call pt_sel_clear
.out:
    pop ax
    ret

pt_cmd_paste:
    push ax
    mov al, 1
    call pt_gate
    jc .out
    call pt_paste
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_undo_cmd - Undo, or Redo: the same exchange either way (Ctrl+Z, Edit menu)
; in:  gfx lock held; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_undo_cmd:
    push ax
    xor al, al
    call pt_gate
    jc .out
    call pt_text_end                ; Ctrl+Z lands mid-sentence otherwise, and
                                    ; the caret would be left on a picture that
                                    ; no longer has the text under it
    call pt_marq_hide
    call pt_undo_swap
    call pt_marq
.out:
    pop ax
    ret

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
; pt_track - has the window been resized under us? then resize the canvas
; in:  [pt_contw]/[pt_conth] from pt_org; gfx lock held
; out: the canvas matches the content (or is as close as memory allows);
;      preserves all registers
;
; There is no resize callback in the window ABI (SPEC.md 11.1): ui_grow
; rewrites W_W/W_H and repaints, and a resizable window's paint proc is
; required to lay out from the live record. So this runs at the top of every
; W_PAINT, and a drag of the grow box arrives here as a content size that no
; longer matches the picture.
; -----------------------------------------------------------------------------
pt_track:
    push ax
    push bx
    push cx
    push dx
    mov ax, [pt_contw]              ; the canvas the content asks for
    sub ax, PT_CV_X
    mov dx, [pt_conth]
    sub dx, PT_STRIP_H + 1
    call pt_setsize
    jnc .out
    cmp byte [pt_fs], 0             ; full screen: the record is not what this
    jne .fsonly                     ; lays out from (pt_org answers the screen,
                                    ; SPEC.md 42.7) and it still holds the
                                    ; windowed size to come back TO, so a
                                    ; refusal owes the toast and nothing else.
                                    ; pt_fsx_main squares the frame on the way
                                    ; out, after [pt_fs] is clear and from
                                    ; outside a paint proc, which is where
                                    ; pt_wfix is legal
    call pt_wfix                    ; the frame follows the canvas, not the drag
    call pt_org                     ; ...AND THE REST OF THIS PAINT LAYS OUT AT
                                    ; THE NEW SIZE. pt_org ran before pt_track
                                    ; and pt_wfix has just rewritten W_W/W_H
                                    ; under it, so [pt_contw] and everything
                                    ; derived from it - the strip's bed, its
                                    ; right-hand controls, [pt_stripy] - were
                                    ; the size the window used to be. Under
                                    ; WF_OWNBG the kernel fills nothing, so the
                                    ; band between the two widths held the
                                    ; DESKTOP until some later whole repaint
                                    ; covered it (SPEC.md 42.9)
    mov al, 1
    cmp byte [pt_szchg], 0
    je .say
    xor al, al                      ; the canvas DID move, so the frame just
.say:                               ; followed it - and that is the answer,
    mov [pt_apsay], al              ; without a notice contradicting it
    mov byte [pt_apend], 1          ; and the toast goes up at the END of this
                                    ; paint, not here: the frame ui_grow already
                                    ; drew is the wrong size, so one repaint is
                                    ; owed, and a toast drawn before it would be
                                    ; wiped by it (SPEC.md 42)
    jmp short .out
.fsonly:
    mov byte [pt_apsay], 1          ; 2 = "the frame is already what it is
    mov byte [pt_apend], 2          ; going to be": say so, redraw nothing
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_setsize - adopt a canvas size, or refuse it to save the artwork
; in:  AX = wanted width, DX = wanted height (unclamped, may be negative)
; out: CF=1 if a shrink was refused on either axis, [pt_szchg] = 1 if the canvas
;      actually changed; preserves all registers
;
; The one place a size is COMMITTED, whichever way it was asked for: the grow
; box, the Apply button, or Enter in a size box. Deciding it is pt_sizeask's
; job, because the kernel asks that question a step earlier now.
; -----------------------------------------------------------------------------
; pt_sizeask - what canvas size will we take, given this one was asked for?
; in:  AX = wanted width, DX = wanted height (unclamped, may be negative)
; out: AX/DX = the size we will accept, [pt_kept] = 1 if an axis was held back
;      to save artwork; preserves every other register
;
; Clamps to the screen, then to what memory will fund, then to what the
; picture will stand to lose. It decides and commits nothing, which is what
; lets OSAPI_WM_ONSIZE (SPEC.md 11.1) run it before the kernel has drawn a
; frame at either size - the answer and the commit used to be the same
; routine, and that is the whole reason a refused drag needed a second
; repaint to undo.
; -----------------------------------------------------------------------------
pt_sizeask:
    push bx
    push cx
    call pt_growmax                 ; what a NEW canvas claim could fund right
                                    ; now - the bound on a GROW, since
                                    ; pt_resize re-claims (the block we hold
                                    ; is [pt_smaxp], and pt_fit takes either)
    cmp ax, PT_CW_MIN               ; signed: a tiny window makes this negative
    jge .w_ok
    mov ax, PT_CW_MIN
.w_ok:
    cmp ax, [pt_cwmax]
    jle .w_cap
    mov ax, [pt_cwmax]
.w_cap:
    ; The height floor is PT_CH_MIN - the smallest window the KERNEL will
    ; make - and nothing else. It used to be PT_SZ_END, "tall enough to still
    ; show the size boxes", and SPEC.md 42.9 is the removal: a control's
    ; convenience was setting the minimum size of a PICTURE, so every image
    ; under 128 rows opened at the wrong size on every adapter, with a white
    ; band under it that was not in the file.
    cmp dx, PT_CH_MIN               ; signed: a tiny window makes this negative
    jge .h_ok
    mov dx, PT_CH_MIN
.h_ok:
    cmp dx, [pt_chmax]
    jle .h_cap
    mov dx, [pt_chmax]
.h_cap:
    mov [pt_reqw], ax               ; what was asked for, before memory has
    mov [pt_reqh], dx               ; its say
    call pt_fit                     ; what memory will actually fund
    mov byte [pt_szmem], 0          ; ...and whether that is what bound it
    cmp ax, [pt_reqw]
    jb .ramcut
    cmp dx, [pt_reqh]
    jae .nocut
.ramcut:
    mov byte [pt_szmem], 1
.nocut:
    ; --- and what the picture will stand to lose ----------------------------
    ; A shrink that would throw away ink is refused per axis: the other axis
    ; still moves, so widening while shortening does the half that is safe.
    mov byte [pt_kept], 0
    mov byte [pt_pinw], 0
    mov byte [pt_pinh], 0
    cmp ax, [pt_cw]
    jae .w_ok2                      ; growing (or level): nothing to lose
    call pt_lose_w
    jnc .w_ok2
    mov ax, [pt_cw]
    mov byte [pt_kept], 1
    mov byte [pt_pinw], 1
    mov byte [pt_szmem], 0          ; ink, not memory, is why THIS axis held
.w_ok2:
    cmp dx, [pt_ch]
    jae .h_ok2
    call pt_lose_h                  ; against the width we just settled on
    jnc .h_ok2
    mov dx, [pt_ch]
    mov byte [pt_kept], 1
    mov byte [pt_pinh], 1
    mov byte [pt_szmem], 0
.h_ok2:
    ; --- and it has to FIT again ------------------------------------------
    ; The guards above put an axis back to a size pt_fit had already ruled
    ; out, so the pair may no longer be one memory can fund - and pt_resize
    ; would then discover that, fail its claim and re-fit blindly, cropping
    ; the very rows the guard just saved. Re-fit with the held axis pinned so
    ; the other one gives instead, and never below what we already have, which
    ; is fundable by definition. That makes typing 600 into the width box on a
    ; machine that cannot fund it a partial grow, not a wipe.
    cmp byte [pt_kept], 0
    je .out
    call pt_paras                   ; CX = paragraphs the pair now needs
    cmp cx, [pt_smaxp]
    jbe .out
    cmp cx, [pt_growp]
    jbe .out
    call pt_fit
    cmp ax, [pt_cw]
    jae .w_ok3
    mov ax, [pt_cw]
.w_ok3:
    cmp dx, [pt_ch]
    jae .out
    mov dx, [pt_ch]
.out:
    mov byte [pt_pinw], 0
    mov byte [pt_pinh], 0
    ; A grow memory would not fund is a refusal too. [pt_kept] used to mean
    ; "ink held an axis back" alone, so a size memory simply could not reach
    ; either said nothing at all or - through a [pt_szmem] left over from the
    ; last commit - borrowed the crop toast and blamed the artwork.
    cmp byte [pt_szmem], 0
    je .askdone
    cmp ax, [pt_cw]
    jne .askdone
    cmp dx, [pt_ch]
    jne .askdone
    mov byte [pt_kept], 1           ; nothing moved, and memory is why
.askdone:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pt_onsize - the resize negotiator the kernel calls before it commits a drag
; in:  SI = our window, CX = proposed frame width, DX = proposed frame height;
;      gfx lock held, nothing drawn yet
; out: CX/DX = the frame size we will take; preserves every other register
; -----------------------------------------------------------------------------
pt_onsize:
    push ax
    push bx
    push dx
    cmp byte [pt_mode], PT_M_LIVE   ; a notice window has no canvas to resize
    jne .fixed
    mov ax, cx
    sub ax, PT_CHROME_W             ; the canvas the proposal implies
    sub dx, PT_CHROME_H
    call pt_sizeask
    ; --- is a notice owed? ONLY if nothing at all moved ---------------------
    ; The guards are per axis, so a drag that narrows the window and refuses
    ; to shorten it is a drag that was largely honoured - and a toast reading
    ; "Would crop artwork" on top of a window that visibly just got smaller
    ; says the opposite of what happened. The unshrunk axis is its own
    ; explanation; the toast is for the case where the drag did nothing.
    xor bl, bl
    cmp byte [pt_kept], 0
    je .sized
    cmp ax, [pt_cw]
    jne .sized
    cmp dx, [pt_ch]
    jne .sized
    mov bl, 1
.sized:
    add ax, PT_CHROME_W             ; ...and the frame the answer implies
    mov cx, ax
    add dx, PT_CHROME_H
    mov [pt_wanth], dx
    mov [pt_apsay], bl
    or bl, bl
    jz .out
    mov byte [pt_apend], 2          ; refused outright: the toast is owed, but
    jmp short .out                  ; not the repaint that used to undo the
.fixed:                             ; frame - there is nothing to undo now
    mov cx, [pt_cw]
    add cx, PT_CHROME_W
    mov ax, [pt_ch]
    add ax, PT_CHROME_H
    mov [pt_wanth], ax
.out:
    pop dx
    mov dx, [pt_wanth]
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_szsi - SI = the toast that explains the last refused resize
; out: SI; clobbers flags and nothing else
; -----------------------------------------------------------------------------
pt_szsi:
    mov si, pt_s_crop
    cmp byte [pt_szmem], 0
    je .out
    mov si, pt_s_noram              ; refused for want of memory, not ink
.out:
    ret

pt_setsize:
    push ax
    push bx
    push cx
    push dx
    mov byte [pt_szchg], 0
    call pt_sizeask                 ; AX/DX = the size we will take, and
                                    ; [pt_szmem] = whether memory bound it
    cmp ax, [pt_cw]
    jne .go
    cmp dx, [pt_ch]
    je .done                        ; nothing moved; [pt_kept] says whether
.go:                                ; that was a refusal
    call pt_resize
    jc .noram                       ; no staging room: the canvas is untouched
    call pt_stripset                ; a canvas memory would not fund leaves the
                                    ; strip where the canvas ends, not where
                                    ; the window ends - pt_org ran before this
    mov byte [pt_szchg], 1
    jmp short .done
.noram:
    mov byte [pt_kept], 1           ; refused, and for a different reason than
    mov byte [pt_szmem], 1          ; cropping - the toast says which
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [pt_kept], 0
    je .clc
    stc
    ret
.clc:
    clc
    ret

; -----------------------------------------------------------------------------
; pt_resize - adopt a new canvas size, keeping the picture's top-left corner
; in:  AX = new width, DX = new height (both already through pt_fit)
; out: CF=0 the canvas is relaid out and repopulated, undo, clipboard and
;      selection dropped; CF=1 nothing changed at all (see .nostage);
;      preserves all registers
;
; **It re-claims.** The undo image and the clipboard go back to the kernel
; first - a resize drops both anyway - then a NEW canvas claim is taken, the
; picture is copied across, the old claim is freed, and undo and clipboard are
; asked for again at the new size. So the peak is the old canvas plus the new
; one, and the steady state is what the picture actually needs.
;
; The version this replaces staged the old picture in the undo image, which is
; what forced every claim to be sized for the largest canvas the screen could
; ever show: the undo image had to be able to hold any canvas that could be
; adopted later, and the bases had to be fixed for the staging to be safe.
; Copying block-to-block needs no staging area at all, which also means a
; resize no longer depends on there being an undo image - a machine that could
; not fund one can still resize.
;
; If the new claim is refused the size is re-fitted against the block we
; already hold and the old path runs, so a resize can degrade but never fail
; half-done. The old row geometry is recomputed arithmetically for the
; read-back, because the tables describe the new layout by then.
;
; The in-place path DOES need a staging area, and the refused grow above has
; just handed the undo image back to make room for a claim that did not
; happen - so it asks for one again before staging. If there is genuinely
; none to be had the resize is REFUSED (CF=1), because the alternative is the
; pt_wipe below erasing a picture there was nowhere to carry across, which is
; exactly what typing an unfundable width into the size box used to do.
; -----------------------------------------------------------------------------
pt_resize:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax                         ; the new size, held across the move
    push dx

    call pt_paras                   ; CX = paragraphs the new canvas needs
    cmp cx, [pt_smaxp]
    jbe .inplace                    ; the claim we hold already carries it

    ; --- grow: give back what a resize drops, then claim the new canvas -----
    call pt_free_undo
    call pt_free_clip
    call pt_kb_of                   ; AX = KB for CX paragraphs
    push ax
    call OSAPI_MEM_CLAIM            ; DX = the new base, CF = refused
    pop bx                          ; BX = the KB, for the fallback below
    jnc .moved

    ; The heap could not hand us a SECOND block that big - but it may well be
    ; able to extend the one we already hold, which needs only the DIFFERENCE
    ; free rather than old + new at once. That is the whole fragmentation
    ; case: plenty of total room, no single run wide enough for two canvases.
    ; It costs the staging the two-block path avoids, so it is the fallback
    ; and not the first move.
    mov ax, bx
    mov dx, [pt_base]
    call OSAPI_MEM_REGROW           ; DX = where it is now (same = in place)
    jc .refit
    mov [pt_base], dx               ; unchanged unless it had to move, and the
    mov ax, bx                      ; picture came with it either way
    mov cl, 6
    shl ax, cl
    mov [pt_smaxp], ax
    jmp .inplace                    ; one block now: stage as in-place does -
                                    ; the wanted size is still on the stack,
                                    ; which is exactly what .inplace expects

.moved:
    mov bx, [pt_base]               ; ...read AFTER the claim above, which may
                                    ; have compacted the heap and moved this
                                    ; very block - pt_reloc will have put
                                    ; [pt_base] right, and nothing else here
                                    ; has read it yet (SPEC.md 66.3 rule 2)
    mov [pt_obase], bx              ; the old canvas: source, then freed
    mov [pt_osrc], bx
    mov [pt_base], dx
    call pt_movable                 ; the new one is ours to move too
    mov cl, 6
    shl ax, cl
    mov [pt_smaxp], ax
    jmp short .geom

.refit:
    pop dx                          ; no bigger block after all: take what the
    pop ax                          ; one we hold can carry
    mov word [pt_growp], 0
    call pt_fit
    push ax
    push dx

.inplace:
    mov word [pt_obase], 0          ; no move: stage in the undo image, the
    cmp word [pt_unseg], 0          ; way this always did - and again the
    jne .stage                      ; SEGMENT, because pt_osrc below is one
    call pt_alloc_undo              ; ...so ASK for one, because the grow just
    cmp word [pt_unseg], 0          ; handed it back to make room and was
    je .nostage                     ; refused anyway (and a machine that could
.stage:                             ; not fund one at startup may be able to)
    mov ax, [pt_base]
    add ax, [pt_undelta]
    mov [pt_osrc], ax
    mov byte [pt_undo_off], 0
    call pt_undo_new
    xor ax, ax
    mov dx, [pt_ch]
    dec dx
    call pt_umark
    jmp .geom

    ; --- nowhere to stage the old picture ----------------------------------
    ; A blank canvas has nothing to carry, so the resize goes ahead. Anything
    ; else would be wiped white by the pt_wipe below and the artwork silently
    ; destroyed - which is what a 384KB machine (no undo image) or a refused
    ; grow used to do - so refuse the resize instead and leave the picture
    ; exactly where it is. The caller reports it like any other refusal.
.nostage:
    xor ax, ax
    call pt_lose_w                  ; is there any ink at all to lose?
    jnc .blank
    cmp byte [pt_haveclip], 0
    jne .nocb
    call pt_alloc_clip              ; put back what the grow attempt gave away
.nocb:                              ; (pt_alloc_undo above already tried)
    pop dx
    pop ax
    stc
    jmp .out
.blank:
    mov word [pt_osrc], 0

.geom:
    mov ax, [pt_cw]                 ; the old geometry, for the read-back
    mov [pt_ocw], ax
    mov ax, [pt_ch]
    mov [pt_och], ax
    mov ax, [pt_stride]
    mov [pt_ostride], ax
    pop dx
    pop ax
    call pt_layout                  ; the new geometry, tables and all
    call pt_bmp_hdr
    mov al, CWHITE
    call pt_wipe                    ; whatever the old picture does not reach

    ; --- copy the overlap back, row by row ----------------------------------
    cmp word [pt_osrc], 0
    je .done                        ; nothing to carry across
    mov bp, [pt_och]                ; BP = rows to carry
    cmp bp, [pt_ch]
    jbe .rows
    mov bp, [pt_ch]
.rows:
    mov dx, [pt_ostride]            ; DX = bytes a row can carry
    cmp dx, [pt_stride]
    jbe .bytes
    mov dx, [pt_stride]
.bytes:
    xor si, si                      ; SI = row
.row:
    cmp si, bp
    jae .done
    mov ax, si
    call pt_orowset                 ; ES:DI = the old row
    mov bx, di                      ; BX = its offset; [pt_orseg] its segment
    mov ax, si
    call pt_rowset                  ; ES:DI = the new row
    mov cx, dx
    shr cx, 1
    mov ax, [pt_orseg]              ; read it while DS is still ours
    push ds
    mov ds, ax
    xchg si, bx                     ; SI = source offset, BX = the row
    cld
    rep movsw
    xchg si, bx                     ; ...and back
    pop ds
    inc si
    jmp short .row
.done:
    mov dx, [pt_obase]              ; the old canvas has been read out of
    or dx, dx
    jz .kept
    call OSAPI_MEM_FREE
    mov word [pt_obase], 0
    call pt_alloc_undo              ; ...and the two claims a resize drops come
    call pt_alloc_clip              ; back at the new size, best effort
.kept:
    cmp byte [pt_haveclip], 0
    jne .kept2
    call pt_alloc_clip              ; the grow attempt gave it away; without
.kept2:                             ; this a refused grow disabled Copy for
                                    ; the rest of the session
    mov byte [pt_undo_ok], 0        ; a resize is never undoable (the image it
    mov word [pt_cbw], 0            ; would need was just reused or replaced)
    mov byte [pt_selon], 0
    mov byte [pt_selshown], 0
    call pt_text_end
    clc
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
; pt_lose_w - would dropping the columns from AX rightwards lose ink?
; in:  AX = the proposed width
; out: CF=1 if any pixel in columns AX..[pt_cw]-1 is not white; preserves all
;
; Scanned as bytes with `repe scasb` against 0xFF, so a clean band costs half a
; byte-compare per pixel. Only the boundary byte needs nibble treatment, and
; only when the new width is odd: its high nibble is the last column kept.
; The stride padding beyond the canvas width is always white (pt_wipe fills the
; whole stride and pt_rect clips to the width), so whole-byte tests are safe.
; -----------------------------------------------------------------------------
pt_lose_w:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, ax
    shr bx, 1
    mov [pt_lwb], bx
    mov byte [pt_lwn], 0
    test al, 1
    jz .rows
    mov byte [pt_lwn], 1            ; odd: byte [pt_lwb]'s LOW nibble is lost
.rows:
    xor si, si                      ; SI = row
.row:
    mov ax, si
    call pt_rowset
    add di, [pt_lwb]
    cmp byte [pt_lwn], 0
    je .bytes
    mov al, [es:di]
    and al, 0x0F
    cmp al, 0x0F
    jne .dirty
    inc di
.bytes:
    mov cx, [pt_stride]
    sub cx, [pt_lwb]
    cmp byte [pt_lwn], 0
    je .cnt
    dec cx
.cnt:
    jcxz .next
    mov al, 0xFF
    cld
    repe scasb
    jne .dirty
.next:
    inc si
    cmp si, [pt_ch]
    jb .row
    clc
    jmp short .out
.dirty:
    stc
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
; pt_lose_h - would dropping the rows from DX downwards lose ink?
; in:  AX = the width that will survive, DX = the proposed height
; out: CF=1 if any pixel in rows DX..[pt_ch]-1 (within that width) is not
;      white; preserves all registers
; -----------------------------------------------------------------------------
pt_lose_h:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    inc ax
    shr ax, 1                       ; bytes the surviving width occupies
    mov [pt_lwb], ax
    mov si, dx                      ; SI = first row to be dropped
.row:
    mov ax, si
    call pt_rowset
    mov cx, [pt_lwb]
    jcxz .next
    mov al, 0xFF
    cld
    repe scasb
    jne .dirty
.next:
    inc si
    cmp si, [pt_ch]
    jb .row
    clc
    jmp short .out
.dirty:
    stc
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
; pt_wfix - put the frame back where the canvas needs it
; in:  [pt_cw], [pt_ch]; out: the window record's W_* updated; preserves all
;
; The last place this app still writes the record itself, and the only one it
; cannot give up: it runs from pt_track, which runs at the TOP of W_PAINT, and
; OSAPI_WM_RESIZE repaints - calling it there would re-enter our own paint
; proc. The grow box no longer arrives here at all (pt_onsize settles the size
; before the kernel commits it); what does is a size change with no
; negotiation in front of it, wm_fullscreen being the one in the tree. The
; position is clamped as ui_drag would: fully on screen, above the dock.
; -----------------------------------------------------------------------------
pt_wfix:
    push ax
    push bx
    push dx
    push es
    mov ax, KERNEL_SEG          ; the record we are about to rewrite is the
    mov es, ax                  ; kernel's (SPEC.md 20.1)
    mov bx, [pt_win]
    mov ax, [pt_cw]
    add ax, PT_CHROME_W
    mov [es:bx + W_W], ax
    mov dx, [pt_ch]
    add dx, PT_CHROME_H
    mov [es:bx + W_H], dx
    mov ax, [pt_scrw]               ; x + w <= screen width
    sub ax, [es:bx + W_W]
    jns .xc
    xor ax, ax
.xc:
    cmp [es:bx + W_X], ax
    jle .yc
    mov [es:bx + W_X], ax
.yc:
    mov ax, [pt_dockr]              ; y + h <= the row the dock owns
    sub ax, dx
    cmp ax, MBAR_H
    jge .yc2
    mov ax, MBAR_H
.yc2:
    cmp [es:bx + W_Y], ax
    jle .out
    mov [es:bx + W_Y], ax
.out:
    pop es
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_orowset - ES:DI = row AX of the OLD picture, wherever pt_resize left it
; in:  AX = row, [pt_och]/[pt_ostride], [pt_osrc] = its base segment
; out: ES:DI, and [pt_orseg] = ES; clobbers BX, flags
; -----------------------------------------------------------------------------
pt_orowset:
    push ax
    push cx
    push dx
    mov bx, [pt_och]
    dec bx
    sub bx, ax                      ; the file row this picture row is
    mov ax, bx
    mul word [pt_ostride]           ; DX:AX = its byte offset
    add ax, PT_BMPHDR
    adc dx, 0
    mov bx, ax
    and bx, 15
    mov di, bx                      ; DI = offset inside the paragraph
    mov cx, dx
    mov dx, ax
    mov ax, cx
    mov cl, 12
    shl ax, cl
    mov cl, 4
    shr dx, cl
    or ax, dx
    add ax, [pt_osrc]               ; the old canvas claim, or the undo image
    mov es, ax                      ; it was staged in (pt_resize)
    mov [pt_orseg], ax
    pop dx
    pop cx
    pop ax
    ret

; pt_num - AX in decimal at DS:DI, DI advanced; clobbers AX, BX, CX, DX
pt_num:
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    ret

; =============================================================================
; The About panel (SPEC.md 12.2/42) - a card drawn ON the content, not a
; window of its own.
;
; A second WINDOW would be the obvious way to do this, and it is wrong here.
; Worth writing down why: a package's SECOND window is never bound
; to its instance record (only the entry's is - SPEC.md 21), so nothing
; destroys it at teardown, and after Paint closes the kernel frees the region
; while the window still carries W_SEG/W_DISP pointing into it. The next
; repaint far-calls into whatever claimed that memory. There is also no
; OSAPI_WM_DESTROY for a package to clean up with. A card drawn on our own
; content has none of that: it is a flag and some pixels, and it dies with
; the instance because it never existed apart from it. Solitaire and Arkanoid
; do the same, so this is the tree's idiom rather than a workaround.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_about - the OSAPI_ABOUT_SET handler (slot 0x01E0, SPEC.md 12.2)
; in:  SI = our window; UI task, gfx lock HELD
; out: nothing; preserves all registers
;
; Registered only in PT_M_LIVE (pt_entry), because dismissing it repaints
; through pt_repaint and that draws a canvas. A Paint that could not claim
; one has a notice on screen already saying so, which is the more useful
; thing for it to be showing.
; -----------------------------------------------------------------------------
pt_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call pt_org                     ; the window may have been dragged
    mov byte [pt_abon], 1
    call pt_abdraw                  ; straight on top - the content under it
    pop di                          ; is untouched and comes back on dismiss
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_abdismiss - take the card down if it is up
; in:  the origin already tracked; gfx lock held
; out: CF = 1 it WAS up and the content has been repainted, so the caller
;      swallows the click or key that dismissed it; CF = 0 otherwise.
;      Preserves every register.
; -----------------------------------------------------------------------------
pt_abdismiss:
    cmp byte [pt_abon], 0
    je .none
    mov byte [pt_abon], 0
    call pt_repaint
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; pt_abmeas - size and centre the card from the strings themselves
; out: [pt_abw]/[pt_abh]/[pt_abl]/[pt_abt], content coords
; preserves every register
;
; Measured, never pinned: the widest line decides the width. Both axes are
; clamped to the content, because this window is RESIZABLE (SPEC.md 11.1) -
; the card has to fit whatever the user has dragged it down to, not the size
; it was measured against.
; -----------------------------------------------------------------------------
pt_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx                      ; CX = widest line, DI = line count
    xor di, di
    mov si, pt_ablines
.next:
    mov bx, [si]
    or bx, bx
    jz .done
    inc di
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    pop si
    cmp ax, cx
    jbe .next
    mov cx, ax
    jmp .next
.done:
    add cx, 24                      ; 12px of margin either side
    cmp cx, [pt_contw]
    jbe .wok
    mov cx, [pt_contw]
.wok:
    mov [pt_abw], cx
    mov ax, di                      ; height = lines * PT_ABLH + margins
    mov bx, PT_ABLH
    mul bx
    add ax, 16
    cmp ax, [pt_conth]
    jbe .hok
    mov ax, [pt_conth]
.hok:
    mov [pt_abh], ax
    mov ax, [pt_contw]
    sub ax, [pt_abw]
    shr ax, 1
    mov [pt_abl], ax
    mov ax, [pt_conth]
    sub ax, [pt_abh]
    shr ax, 1
    mov [pt_abt], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_abdraw - the card: white fill, black frame, the credit centred in it
; in:  the origin already tracked; gfx lock held
; out: nothing; preserves all registers
;
; Every line is centred on the CARD rather than on the content, so the block
; still reads as one card when the card has been clamped narrower than the
; text wanted.
; -----------------------------------------------------------------------------
pt_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pt_abmeas
    mov ax, [pt_abl]                ; pt_cwell: fill in [pt_pen], frame black
    mov bx, [pt_abt]
    mov cx, [pt_abw]
    dec cx                          ; it takes width-1 / height-1
    mov dx, [pt_abh]
    dec dx
    mov byte [pt_pen], CWHITE
    call pt_cwell
    mov si, pt_ablines
    mov di, [pt_abt]
    add di, 8                       ; the first baseline, inside the frame
.line:
    mov bx, [si]
    or bx, bx
    jz .out
    add si, 2
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX = this line's width
    mov cx, [pt_abw]
    sub cx, ax
    jns .fits
    xor cx, cx                      ; wider than the card: flush left rather
.fits:                              ; than off its left edge
    shr cx, 1
    add cx, [pt_abl]
    mov dx, di
    mov byte [pt_pen], CBLACK
    call pt_ctext                   ; content coords; it adds the origin
    pop si
    add di, PT_ABLH
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    mov byte [pt_dall], 1           ; A FULL repaint, so it must not inherit the
                                    ; last W_PAINT's damage rect - which would
                                    ; skip whichever parts that one owed and
                                    ; this one does not (SPEC.md 11.90.2)
    call pt_fsbed                   ; ...and it lays its own beds now: under
                                    ; WF_OWNBG nothing else does, and this
                                    ; routine's own comment already claimed
                                    ; every part draws its background
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
    mov byte [pt_careton], 0
    call pt_marq
    cmp byte [pt_abon], 0           ; the card sits on top of everything, so
    je .noab                        ; it is drawn last and by every repaint -
    call pt_abdraw                  ; otherwise a paint triggered while it is
.noab:                              ; up would quietly erase it
    call pt_growbox                 ; the white-fill idiom ate it (SPEC.md
                                    ; 11.1) - put it back, unless this screen
                                    ; has none (pt_growbox)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_bands - keep the kernel's cached bands in step with our layout
; in:  nothing; out: nothing, every register and the flags preserved
;
; SPEC.md 11.96.11.1. The tool column is a constant 44 pixels and is named once
; at entry; the STRIP's height is [pt_conth] - [pt_ch], which the size boxes
; move without moving the window, so it is named from here - every paint, which
; costs a compare, because OSAPI_WM_BAND drops nothing when the figure has not
; changed. It is 451 of a repaint's 604 drawing calls that the column saves and
; 131 that the strip does (PERFORMANCE.md Set 46).
; -----------------------------------------------------------------------------
pt_bands:
    pushf
    push ax
    push bx
    push cx
    cmp byte [pt_fsx], 0            ; the bracket owns the machine and there is
    jne .out                        ; no window under it to cache
    cmp byte [pt_mode], PT_M_LIVE
    jne .out
    mov bx, [pt_win]
    or bx, bx
    jz .out
    mov cx, [pt_conth]
    sub cx, [pt_ch]
    jbe .out                        ; no strip yet: pt_org has not run
    mov al, 3                       ; edge 3 = bottom
    call OSAPI_WM_BAND
.out:
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; pt_fsbed - the pixels pt_repaint does NOT cover, white, once
; in:  [pt_ch], [pt_cw], [pt_contw]; gfx lock held
; out: nothing; preserves all registers
;
; Windowed, wm_draw_win white-fills the whole content before it calls W_PAINT
; and this never runs. The bracket has no wm_draw_win, so the first paint on a
; screen still showing the desktop has to clear what the repaint will not
; write - which is the tool column's bed and, if memory would not fund a
; canvas as wide as the screen, the columns to the right of it. Two fills
; rather than one over the whole screen, because the rest is about to be
; covered by pt_blit_all and the strip's own bed, and painting it twice is
; PERFORMANCE.md's double-draw with the picture as the second layer.
; -----------------------------------------------------------------------------
pt_fsbed:
    push ax
    push bx
    push cx
    push dx
    mov byte [pt_pen], CWHITE
    xor ax, ax
    xor bx, bx
    mov cx, PT_SEPX - 1
    mov dx, [pt_ch]
    dec dx
    call pt_dmg_hit                 ; SPEC.md 11.90.2: only if this paint owes
    jnc .right                      ; it. Always, in the 42.7 bracket
    call pt_cfill
.right:
    mov ax, [pt_cw]                 ; ...and anything the canvas leaves to its
    add ax, PT_CV_X                 ; right (pt_fit shrank it, not the user)
    mov cx, [pt_contw]
    dec cx
    cmp ax, cx
    jg .out
    xor bx, bx
    mov dx, [pt_ch]
    dec dx
    call pt_dmg_hit
    jnc .out
    call pt_cfill
.out:
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
; of the canvas claim with no staging pass. It used to need the result to fit
; 64KB as well - a full 448x280 picture is 62,838 bytes, which it just did -
; and since SPEC.md 18.4.1 it does not: DX:CX is the count and the source
; advances by segment, so a picture bigger than one segment saves whole. The
; reader needs a staging buffer for the file it is about to parse and uses
; the clipboard segment for it, which is why Open empties the clipboard.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_dlg - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The fence at the top is SPEC.md 53's, made explicit rather than assumed: the
; dialog is MODAL and its answer comes back through the event ladder (SPEC.md
; 38), and no events are dispatched inside a bracket - fdlg_open would create
; the window, return, and leave a dialog on screen that nothing can ever
; answer. fsx_run refuses to START while one is up for the same reason, from
; the other end. Nothing routes here from the bracket today (the menu bar is
; unreachable in there and Paint puts no file verb on a key), so this is a
; fence and not a path; it is here because the rule belongs with the routine
; it constrains rather than with the callers that happen to respect it.
; -----------------------------------------------------------------------------
pt_dlg:
    cmp byte [pt_fsx], 0
    jne .nofsx
    push bx
    push si
    push di
    mov bx, si
    mov di, pt_ondlg
    mov si, pt_name
    cmp byte [pt_name], 0
    jne .named
    mov si, pt_s_defname
    cmp byte [pt_sfmt], 0
    je .named
    mov si, pt_s_defgif
.named:
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret
.nofsx:
    push si
    mov si, pt_s_nodlg
    call pt_msg_show
    pop si
    ret

; -----------------------------------------------------------------------------
; pt_arg - accept a picture handed to us at launch (SPEC.md 54.5)
; in:  nothing; called from pt_entry BEFORE the window exists
; out: nothing; preserves all registers AND the flags
;
; It RECORDS and does not load; pt_argload, a few instructions later, is the
; load. The two are still separate because the slot is READ-AND-CLEAR
; (SPEC.md 54.5) and pt_load has a dozen ways not to finish - so what the
; kernel handed over is banked whole before anything can fail with it half
; taken.
;
; It ran after wm_create until SPEC.md 42.11, on the reasoning that a load
; needs the gfx lock. It does not: the decoders write memory, and the toast
; is staged rather than drawn when no lock is held (SPEC.md 59.3).
; -----------------------------------------------------------------------------
pt_arg:
    pushf
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the usual case
    jc .out
    mov [pt_argclus], dx
    mov [pt_argdrv], bl
    mov ax, KERNEL_SEG              ; the name is a KERNEL pointer, so ES is
    mov es, ax                      ; loaded rather than trusted
    mov di, pt_name
    mov cx, PT_NAMEMAX
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.named:
    push ds
    pop es                          ; ES = DS again, the callback default
    call pt_readable                ; refuse a format we do not decode BEFORE
    jc .clear                       ; the disk, exactly as the dialog does
    mov byte [pt_argp], 1
    jmp short .out
.clear:
    mov byte [pt_name], 0           ; not ours: start blank rather than named
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; pt_argload - spend it, from the ENTRY PROC (SPEC.md 54.5/42.11)
; in:  no window yet, no gfx lock; [pt_argp] = 1 and pt_name the document, or
;      [pt_argp] = 0 and there is nothing to do
; out: pt_tpl sized for the picture; preserves all registers
;
; Called before wm_create, which is the whole of SPEC.md 42.11: pt_load ends
; in pt_wfollow, so the template this returns is the PICTURE's size and the
; window is created at it. From the first paint - where this used to run -
; the same call could only ask for a resize, and the asking is what had no
; answer.
;
; THE [pt_argp] GATE IS THIS ROUTINE'S OWN, and it is here rather than at the
; call site because it has already been lost once by being left there. It was
; a `cmp byte [pt_argp], 0 / je` around the call in pt_paint; SPEC.md 42.11
; moved the call up to the entry proc and the gate did not come with it, so
; EVERY Paint launch ran the launch-document load. With no document pt_name is
; the empty string a zeroed bss holds, and an empty name is exactly what
; dskw_name83 refuses - so an ordinary double-click on PAINT.O88 answered
; FERR_NAME and toasted 'Bad file name', with OSAPI_FILE_GOTO having quietly
; moved the machine to A: root (drive 0, cluster 0) on the way past. A gate
; the ROUTINE holds cannot be left behind by the next caller that moves.
; -----------------------------------------------------------------------------
pt_argload:
    cmp byte [pt_argp], 0           ; nothing was handed to us (SPEC.md 54.5):
    jne .have                       ; no document, no volume switch, no read
    ret
.have:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [pt_argp], 0           ; once, whatever happens below
    mov dx, [pt_argclus]
    mov bl, [pt_argdrv]
    call OSAPI_FILE_GOTO            ; the folder it was opened from
    jc .out                         ; unlistable: pt_load would only fail
    mov si, pt_name                 ; pt_load reads the name from SI, not from
    call pt_load                    ; the buffer - ...and this is the dialog's
                                    ; own load
    mov byte [pt_wchg], 0           ; pt_wfollow raised it and the TEMPLATE is
                                    ; the answer here, so the debt is paid
                                    ; before it is owed. Left set, the next
                                    ; File > Open would take pt_ondlg's resize
                                    ; branch on a size nothing had changed
    mov si, [pt_msgp]               ; the outcome - 'Opened ...', or why not.
    or si, si                       ; A double-clicked picture that fails to
    jz .out                         ; decode used to say NOTHING at all: this
    call pt_msg_show                ; path set [pt_msgp] and nobody read it
    mov word [pt_msgp], 0           ; ...and it is not owed twice
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_ondlg - the dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, DI = the chosen name;
;      UI task, gfx lock HELD, the dialog window already destroyed
; out: nothing; no register need be preserved
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; pt_readable - can we even try this name? (SPEC.md 38.6)
; in:  [pt_name] = the chosen name, NUL
; out: CF = 0 the extension is one we decode, CF = 1 it is not.
;      All registers preserved.
;
; A NAME test, deliberately, and not a content one: its whole job is to refuse
; BEFORE the file is read. The decoders keep the last word - a .BMP that is
; not one still ends at pt_s_badpic, just not for free - and this stays a
; cheap filter rather than a second opinion about the format.
; -----------------------------------------------------------------------------
pt_readable:
    push ax
    push si
    push di
    xor di, di                      ; DI = past the last dot, 0 = no extension
    mov si, pt_name
.find:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    jne .find
    mov di, si
    jmp short .find
.end:
    or di, di
    jz .no
    mov si, pt_exts                 ; walk the table of NUL 3-char extensions
.try:
    cmp byte [si], 0
    je .no
    mov al, [si]
    cmp al, [di]
    jne .nextext
    mov al, [si+1]
    cmp al, [di+1]
    jne .nextext
    mov al, [si+2]
    cmp al, [di+2]
    jne .nextext
    cmp byte [di+3], 0              ; and nothing after it
    jne .nextext
    pop di
    pop si
    pop ax
    clc
    ret
.nextext:
    add si, 4
    jmp short .try
.no:
    pop di
    pop si
    pop ax
    stc
    ret

pt_exts:                            ; what the decoders below actually read;
    db 'BMP', 0                     ; the mount's display names are upper-case
    db 'GIF', 0                     ; 8.3 (SPEC.md 19), so no case folding
    db 0

pt_ondlg:
    mov [pt_dmode], al              ; to MEMORY, not to a register: the name
                                    ; copy below wants AL and pt_org wants BX,
                                    ; so the mode has to outlive both
    mov [pt_fsize], cx              ; DX:CX = the file's size (SPEC.md 38.6),
    mov [pt_fsize+2], dx            ; banked FIRST for the same reason - the
                                    ; copy below uses CX as its counter
    mov si, di
    mov di, pt_name
    mov cx, PT_NAMEMAX              ; bounded even though SPEC.md 38.6 promises
.copy:                              ; 12 or fewer: a package that trusts a
    mov al, [es:si]                 ; promise is a package with an overrun.
    mov [di], al                    ; The name buffer is the KERNEL's and ES
    or al, al                       ; points there on entry (SPEC.md 20.1/38.6)
    jz .copied
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    mov bx, [pt_win]
    call pt_org                     ; the window moved while the dialog was up
    cmp byte [pt_dmode], FDLG_SAVE
    je .save

    ; --- refuse before the disk, not after it (SPEC.md 38.6) ----------------
    ; The decoders below judge a picture by its first bytes, which used to
    ; mean reading the WHOLE file to find out it was not one - and on a
    ; 4.77MHz machine a wrong pick then costs exactly what a right one does.
    ; The dialog now hands us the name and the size, so both refusals are
    ; free: no toast, no staging claim, no motor.
    call pt_readable
    jc .badfile
    mov ax, [pt_fsize]              ; a size we could never stage is the other
    mov dx, [pt_fsize+2]            ; one. PT_STAGE_MAX is the cap, and the
    mov bx, ax                      ; heap's largest run is the real limit
    or bx, dx
    jz .sizeok                      ; 0 = the dialog had no size (a typed
                                    ; name): let the old path answer
    add ax, 1023                    ; bytes -> KB, rounded up, across 32 bits
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = KB the file needs
    cmp ax, PT_STAGE_MAX
    ja .toobig
    push ax
    call OSAPI_MEM_AVAIL            ; AX = the largest free run, KB
    pop bx
    cmp ax, bx
    jb .toobig
.sizeok:

                                    ; NO 'Loading...' here. It said what
                                    ; SPEC.md 12.8's file-activity widget says
                                    ; better - a live bar rather than a static
                                    ; line - and it said it over the widget's
                                    ; own pixels, which is why fpg_begin used
                                    ; to retire it a moment after it went up.
                                    ; Since SPEC.md 59.8 the strip is in the
                                    ; clock's field and would simply survive
                                    ; beside the widget, saying the same thing
                                    ; twice; the decision stands on the first
                                    ; reason alone. The SAVE below keeps its
                                    ; message because an encode comes FIRST
                                    ; there and the widget cannot see it
    mov si, pt_name
    call pt_load
    jmp short .draw
.badfile:
    mov word [pt_msgp], pt_s_badpic ; nothing was claimed and nothing was
    jmp short .draw                 ; read: the canvas is untouched
.toobig:
    mov word [pt_msgp], pt_s_bigpic
    jmp short .draw
.save:
    mov si, pt_s_saving             ; ...but a save is not the mirror of it: a
    call pt_msg_show                ; GIF encodes 125,000 pixels BEFORE the
                                    ; floppy starts, and the file-activity
                                    ; widget cannot report work that has not
                                    ; reached the disk. SPEC.md 59.4's
                                    ; toast_now is what puts this on the glass
                                    ; before the machine goes quiet, and is
                                    ; why the pt_wait that used to be here for
                                    ; the lock round trip is gone
    call pt_save
.draw:
    cmp byte [pt_wchg], 0           ; a load that resized the window has to
    je .plain                       ; repaint the DESKTOP too, not just us:
    mov byte [pt_wchg], 0           ; the old frame is still on the glass
    mov bx, [pt_win]
    mov cx, [pt_tpl + WT_W]
    mov dx, [pt_tpl + WT_H]
    call OSAPI_WM_RESIZE            ; clamp, re-fit and repaint the lot
    jmp short .toast                ; (SPEC.md 11.1), which re-enters our
                                    ; W_PAINT at the picture's size
.plain:
    call pt_repaint
.toast:
    mov si, [pt_msgp]               ; the outcome, on top of the fresh picture
    or si, si
    jz .out
    call pt_msg_show
.out:
    ret

; -----------------------------------------------------------------------------
; pt_save - write pt_name out in the format the menu verb asked for
; in:  [pt_sfmt] = 0 BMP / 1 GIF; UI task, gfx lock held
; out: [pt_msgp] = the toast to show; preserves all registers
;
; The name is coerced to the verb's extension first (pt_setext), so "Save Gif"
; on PICTURE.BMP writes PICTURE.GIF rather than GIF bytes into a file whose
; name promises a bitmap.
; -----------------------------------------------------------------------------
pt_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov word [pt_msgp], pt_s_wrote
    call pt_setext
    mov si, pt_name
    cmp byte [pt_sfmt], 0
    je .bmp
    call pt_alloc_lzw               ; the tables, for the length of this file
    jnc .gif
    mov word [pt_msgp], pt_s_ng
    jmp .out
.gif:
    call pt_gif_out
    call pt_free_lzw                ; ...and straight back
    jmp .out
.bmp:
    ; --- one write, and no 64KB ceiling on it any more (SPEC.md 18.4.1). The
    ; canvas is one contiguous claim with the DIB at offset 0, which is
    ; exactly the segment run the write walks - so DX:AX goes straight into
    ; DX:CX and a full-screen picture saves whole.
    mov ax, [pt_ch]
    mul word [pt_stride]            ; DX:AX = pixel bytes
    add ax, PT_BMPHDR
    adc dx, 0
    mov cx, ax                      ; DX:CX = the file's 32-bit length
    call pt_bmp_hdr                 ; re-stamp it: the live size is the truth
    call pt_pin                     ; PINNED across the write (SPEC.md
    mov ax, [pt_base]               ; 66.5.7.1): the kernel holds ES:BX into
    mov es, ax                      ; the canvas for the whole call
    xor bx, bx                      ; ES:BX = the DIB, header and all
    call OSAPI_FILE_WRITE           ; SI is still the name
    call pt_unpin                   ; ...and movable again, CF untouched
    jnc .wrote
    call pt_ferr
    jmp short .out
.wrote:
    mov byte [pt_trunc], 0          ; what is on disk now IS what we hold
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
    mov ax, PT_STAGE_MAX
    call pt_stage                   ; ES:0000 = somewhere to put the file, and
    jc .nostage                     ; DX:CX = how much of it there is
    mov es, [pt_gseg]
    xor bx, bx
.rd:
    ; The destination advances by SEGMENT (SPEC.md 18.4.1), so a picture whose
    ; file runs past 64KB - which a full-screen canvas's BMP does - loads in
    ; one call instead of being refused. ES:BX is the base and the buffer
    ; starts at ES:0000, which is what both decoders already assume; DX:CX is
    ; the capacity, and the size comes back in DX:AX.
    call OSAPI_FILE_READ
    jnc .got
    call pt_ferr
    jmp short .out
.got:
    mov [pt_fsz], ax                ; DX:AX is the file's 32-bit size, and the
    mov [pt_fsz+2], dx              ; decoders read it from here
    call pt_stagefit                ; ...and the staging claim gives back what
                                    ; the file did not need, BEFORE pt_adopt
                                    ; goes looking for a canvas and an undo
                                    ; image. Nothing knows the size until the
                                    ; read reports it, so the claim was for
                                    ; the largest run the heap had
    or dx, dx
    jnz .sz32
    cmp ax, 64
    jb .bad
.sz32:
    cmp word [es:0], 0x4D42         ; 'BM'
    je .bmp
    cmp word [es:0], 0x4947         ; 'GI'
    je .gifin
    cmp word [es:0], 0xD8FF         ; JPEG SOI
    je .nofmt
.bad:
    mov word [pt_msgp], pt_s_badpic
    jmp short .out
.nofmt:
    mov word [pt_msgp], pt_s_nofmt
    jmp short .out
.gifin:
    cmp word [pt_fsz+2], 0          ; the GIF decoder is 16-bit end to end and
    jne .gifbig                     ; deliberately so (SPEC.md 42.6): every
                                    ; offset into the stream is a word, and
                                    ; pt_gdeblk flattens the sub-blocks with
                                    ; one rep movsb inside one segment. A GIF
                                    ; is COMPRESSED - the 448x280 test picture
                                    ; is 1,285 bytes where its BMP is 62,838 -
                                    ; so 64KB of GIF is a picture far past
                                    ; PT_GDIM_MAX. Refuse it; do not truncate
    call pt_alloc_lzw               ; the tables, for the length of this file
    jnc .gif
    mov word [pt_msgp], pt_s_ng
    jmp short .out
.gif:
    call pt_gif_in                  ; the magic decides, not the extension
    pushf
    call pt_free_lzw                ; ...and straight back, error or not
    popf
    jnc .out
    mov [pt_msgp], si
    jmp short .out
.bmp:
    call pt_bmp_in                  ; DX:AX = the byte count read
    jnc .out
    mov [pt_msgp], si               ; SI = why not
    jmp short .out
.nostage:
    mov word [pt_msgp], pt_s_nostage
    jmp short .out
.gifbig:
    mov word [pt_msgp], pt_s_gifbig
.out:
    call pt_unstage                 ; the staging buffer goes straight back:
    pop es                          ; it is the biggest thing this app holds
    pop di                          ; and it is wanted for one decode
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_stage - a transient buffer for a file being decoded or encoded
; in:  AX = the most KB worth taking
; out: CF=0 with [pt_gseg] set and DX:CX = its byte capacity, or CF=1;
;      preserves every other register
;
; A TRANSIENT claim, sized from what the heap actually has (SPEC.md 42.6).
; This used to borrow the undo image - "the biggest single buffer we have" -
; and, when there was no undo image, the flood-fill stack: PT_SC_KB, twelve
; kilobytes, which was the real ceiling on what Paint could open. Both were
; written before there was an allocator to ask, and borrowing the undo image
; is also why opening a picture threw away the undo history. Neither is a
; reason any more.
;
; The claim is capped at PT_STAGE_MAX and floored at nothing: a machine that
; can spare 8KB opens an 8KB picture, which is what the fallbacks used to be
; for. Refusal is an ordinary path and says so.
; -----------------------------------------------------------------------------
pt_stage:
    push ax
    push bx
    mov [pt_stgkb], ax              ; the caller's cap, across the AVAIL call
    call OSAPI_MEM_AVAIL            ; AX = the largest free run, in KB
    cmp ax, [pt_stgkb]
    jbe .kb
    mov ax, [pt_stgkb]
.kb:
    or ax, ax
    jz .no
    mov [pt_stgkb], ax
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base segment
    jc .no
    mov [pt_gseg], dx
    mov ax, [pt_stgkb]              ; DX:CX = KB * 1024, 32-bit
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    mov cx, ax
    clc
    jmp short .out
.no:
    mov word [pt_gseg], 0
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_stagefit - shrink the staging claim to the bytes actually read
; in:  [pt_fsz] = the 32-bit size
; out: nothing (all registers and the flags preserved)
;
; A shrink always succeeds in place (SPEC.md 50.3.1), so this cannot fail and
; cannot move the buffer. It matters because pt_adopt runs next and wants a
; canvas claim and an undo image, and until this call the staging is holding
; the largest free run the heap had.
; -----------------------------------------------------------------------------
pt_stagefit:
    pushf
    push ax
    push cx
    push dx
    mov ax, [pt_fsz]
    mov dx, [pt_fsz+2]
    add ax, 1023                    ; -> whole KB, 32-bit
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = KB (a file this big cannot exist on
    or ax, ax                       ; the media, so this cannot overflow)
    jnz .kb
    inc ax                          ; an empty claim is not a claim
.kb:
    mov dx, [pt_gseg]
    call OSAPI_MEM_REGROW
    jc .out
    mov [pt_gseg], dx
.out:
    pop dx
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; pt_unstage - hand the staging buffer back
; out: nothing (all registers and the flags preserved)
; -----------------------------------------------------------------------------
pt_unstage:
    pushf
    push ax
    push dx
    mov dx, [pt_gseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [pt_gseg], 0
.out:
    pop dx
    pop ax
    popf
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
; in:  ES = the staging segment, [pt_fsz] = the 32-bit byte count read;
;      gfx lock held
; out: CF=0 done, CF=1 with SI = the reason; preserves all other registers
;      except ES, which the row loop re-points per row (pt_srowset)
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
    mov ax, [es:10]                 ; bfOffBits, both halves: a picture past
    mov [pt_boff], ax               ; the 64KB horizon has pixel data past it
    mov ax, [es:12]                 ; too, and this is where that used to be
    mov [pt_boff+2], ax             ; refused (SPEC.md 42.6)
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
    ; --- and the pixel data must be inside what we read, in 32 bits -------
    ; rows x stride is the number this used to give up on: 448 x 326 is
    ; 73,024 and every picture worth the resizable canvas passes 65,535. It
    ; is a dword here, added to a dword bfOffBits and compared against the
    ; dword the read reported (SPEC.md 42.6).
    mov ax, [pt_bh]
    mul word [pt_bstr]              ; DX:AX = the pixel bytes
    add ax, [pt_boff]
    adc dx, [pt_boff+2]
    jc .big                         ; past 4GB: a hostile header, not a file
    cmp dx, [pt_fsz+2]
    ja .big
    jb .fits
    cmp ax, [pt_fsz]
    ja .big
.fits:
    ; --- the palette, mapped to our sixteen -------------------------------
    cmp word [pt_bpp], 24
    je .size
    call pt_bmp_pal
    ; --- the canvas becomes the picture's size, as far as it can go --------
.size:
    mov ax, [pt_bw]
    mov dx, [pt_bh]
    call pt_adopt
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
    mul word [pt_bstr]              ; DX:AX = the row's 32-bit byte offset...
    add ax, [pt_boff]
    adc dx, [pt_boff+2]
    call pt_srowset                 ; ...as a (segment, offset) pair, which is
    call pt_bmp_row                 ; how the CANVAS has been addressed since
                                    ; it outgrew a segment (pt_rowseg)
    call pt_line_put                ; -> the canvas row DI
    inc di
    jmp short .row
.done:
    call pt_wfollow                 ; the window follows the picture's size
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
; pt_srowset - point ES:[pt_srow] at the staged file's byte DX:AX
; in:  DX:AX = a 32-bit offset into the staging buffer
; out: ES = the segment, [pt_srow] = an offset of 0..15; clobbers ES and
;      [pt_srow] only
;
; dskw_norm's arithmetic (SPEC.md 18.4.1) inside an app, and the same reason:
; the paragraph part of the offset goes into the segment, so what is left is
; small enough that nothing added to it can carry. A row is at most
; [pt_cols] x 3 bytes wide and [pt_cols] is the canvas's width, so the
; per-column offsets pt_bmp_row builds on top of this cannot reach a segment
; boundary from an offset under 16.
; -----------------------------------------------------------------------------
pt_srowset:
    push ax
    push cx
    push dx
    mov cx, ax
    and cx, 15                      ; the offset that survives...
    mov [pt_srow], cx
    mov cl, 4                       ; ...and the paragraphs that do not
    shr ax, cl
    mov cl, 12
    shl dx, cl                      ; the high word's paragraphs are its low
    or ax, dx                       ; four bits shifted up: 20 bits of
    add ax, [pt_gseg]               ; address is 16 bits of paragraph
    mov es, ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_bmp_row - one source row into pt_line as colour indices
; in:  ES:[pt_srow] = the row (pt_srowset), [pt_cols], [pt_bpp]
; out: pt_line filled; preserves all registers except ES, which the caller
;      re-points per row anyway
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
.col:                               ; ONE dispatch, at the loop top: the depth
    cmp word [pt_bpp], 24           ; cannot change inside a row, but a second
    je .rgb                         ; copy of this ladder at the bottom cost 21
    cmp word [pt_bpp], 8            ; bytes to say so
    je .idx8
    cmp word [pt_bpp], 4
    je .idx4
; --- 1bpp -------------------------------------------------------------------
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
    jb .col
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
    mov ax, di
    call pt_rowset                  ; ES:DI = the row...
    mov bx, di                      ; ...and BX its offset, for the packing
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

; -----------------------------------------------------------------------------
; pt_adopt - make the canvas the picture's size, as far as it can go
; in:  AX = the picture's width, DX = its height
; out: the canvas relaid out and wiped white, [pt_cols] = columns to take,
;      [pt_trunc] = 1 if anything had to be given up; preserves all registers
;
; Shared by both readers, which is the point: one place decides what "as far as
; it can go" means, so a GIF and a BMP of the same size open the same way.
; -----------------------------------------------------------------------------
pt_adopt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pt_pw], ax
    call pt_text_end
    call pt_sel_drop
    mov byte [pt_trunc], 0
    mov ax, [pt_pw]                 ; what the SCREEN can show...
    cmp ax, [pt_cwmax]
    jbe .w_ok
    mov ax, [pt_cwmax]
    mov byte [pt_trunc], 1
.w_ok:
    cmp ax, PT_CW_MIN
    jae .w_min
    mov ax, PT_CW_MIN
.w_min:
    cmp dx, [pt_chmax]
    jbe .h_ok
    mov dx, [pt_chmax]
    mov byte [pt_trunc], 1
.h_ok:
    cmp dx, PT_CH_MIN
    jae .h_min
    mov dx, PT_CH_MIN
.h_min:
    call pt_fit                     ; ...and then what MEMORY can hold
    jnc .fits
    mov byte [pt_trunc], 1          ; cropped: File > Save must not overwrite
.fits:                              ; the original with less than it held
    call pt_layout
    call pt_bmp_hdr
    call pt_undo_new
    mov byte [pt_undo_ok], 0        ; a picture just arrived: there is nothing
                                    ; to go BACK to. The clipboard used to be
                                    ; emptied here too, because the codec's
                                    ; tables lived in it and the file was
                                    ; staged in the undo image; both have had
                                    ; their own claims for a while and the
                                    ; line outlived the reason (SPEC.md 42.6)
    mov ax, [pt_pw]                 ; columns we will actually take
    cmp ax, [pt_cw]
    jbe .cols
    mov ax, [pt_cw]
.cols:
    mov [pt_cols], ax
    mov al, CWHITE                  ; anything the file does not cover
    call pt_wipe
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_wfollow - the window is to take the canvas's size
; out: pt_tpl sized from the canvas, [pt_wchg] = 1 so the caller asks the
;      kernel for it; preserves all registers
;
; It used to write W_W/W_H/W_X/W_Y in the record itself - the second of the
; two liberties docs/PAINT-NOTES.md recorded - and OSAPI_WM_RESIZE
; (SPEC.md 11.1) retired it. The caller does the asking rather than this
; routine, because it is deep inside a decoder and the answer is a full
; repaint.
; -----------------------------------------------------------------------------
pt_wfollow:
    call pt_wsize
    mov byte [pt_wchg], 1
    ret

; -----------------------------------------------------------------------------
; pt_setext - make pt_name carry the extension [pt_sfmt] asks for
; out: nothing; preserves all registers
;
; Left alone if there is no room for one, which cannot happen with a name the
; Standard File dialog produced (SPEC.md 38.6 promises 8.3) but is checked
; anyway - the buffer is 14 bytes and this writes four.
; -----------------------------------------------------------------------------
pt_setext:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, pt_name
    xor bx, bx                      ; BX = the last '.' seen
.find:
    mov al, [si]
    or al, al
    jz .end
    cmp al, '.'
    jne .next
    mov bx, si
.next:
    inc si
    jmp short .find
.end:
    or bx, bx
    jnz .have
    mov bx, si                      ; no extension at all: append one
    mov byte [bx], '.'
.have:
    mov ax, bx
    sub ax, pt_name
    cmp ax, PT_NAMEMAX - 4          ; '.' + three characters + the NUL
    ja .out
    mov byte [bx], '.'
    mov di, bx
    inc di
    mov si, pt_s_ebmp
    cmp byte [pt_sfmt], 0
    je .cp
    mov si, pt_s_egif
.cp:
    mov cx, 4
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .c
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; GIF - LZW in both directions (SPEC.md 42)
;
; Where everything lives, and why:
;
;   READ   the staged file is in the undo image, exactly as the BMP reader's is.
;          prefix[4096] words + suffix[4096] bytes + a 4096-byte output stack
;          fill the clipboard's reserved floor to the byte.
;   WRITE  child/sibling/suffix for 2048 codes (10KB) go in the clipboard, the
;          GIF being built goes in the undo image, and the canvas is read one
;          row at a time into pt_line.
;
; What decides those placements is that DS must stay on the kernel segment for
; the bss, so ES is the only far pointer there is and no inner loop may need
; two. The reader's bit window borrows ES for three bytes and gives it straight
; back; the writer's output goes through a 255-byte block in bss, flushed once
; per GIF sub-block - which is the shape the format wants anyway.
;
; The writer stops growing codes at 11 bits rather than the format's 12. That
; halves its tables and costs a Clear code every 2030 strings, which for
; flat-shaded drawings is nothing. The READER is full 12-bit: it has to take
; whatever a host tool wrote.
; =============================================================================

; -----------------------------------------------------------------------------
; pt_gif_in - decode the staged GIF into the canvas
; in:  ES = the staging segment, AX = bytes read; gfx lock held
; out: CF=0 done, CF=1 with SI = the reason; preserves all other registers
;
; Every offset is checked against the byte count actually read before it is
; used, because the file came off someone else's floppy (SPEC.md 19). The
; dimensions are capped at PT_GDIM_MAX as well: a header claiming 60000 rows
; would otherwise be decoded row by row into nothing for a very long time.
; -----------------------------------------------------------------------------
pt_gif_in:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [pt_fsz]                ; the caller set it, both words, and has
    cmp ax, 14                      ; already refused a high one (SPEC.md 42.6)
    jb .bad
    cmp byte [es:2], 'F'
    jne .bad
    mov byte [pt_ghavp], 0
    mov si, 13                      ; past the logical screen descriptor
    mov al, [es:10]
    test al, 0x80                   ; a global colour table?
    jz .blk
    call pt_gctsz                   ; CX = entries
    call pt_gif_pal
    jc .bad
; --- the block stream ---------------------------------------------------------
.blk:
    cmp si, [pt_fsz]
    jae .bad
    mov al, [es:si]
    inc si
    cmp al, 0x2C                    ; the image descriptor: what we came for
    je .img
    cmp al, 0x21                    ; an extension: skip its sub-blocks
    jne .bad
    cmp si, [pt_fsz]
    jae .bad
    inc si                          ; the label byte
.skip:
    cmp si, [pt_fsz]
    jae .bad
    mov al, [es:si]
    inc si
    or al, al
    jz .blk
    mov ah, 0
    add si, ax
    jmp short .skip
; --- the image descriptor -----------------------------------------------------
.img:
    mov ax, si
    add ax, 9
    jc .bad
    cmp ax, [pt_fsz]
    ja .bad
    mov ax, [es:si+4]               ; width and height; left/top are ignored,
    mov [pt_gw], ax                 ; the picture goes at the corner
    or ax, ax
    jz .bad
    cmp ax, PT_GDIM_MAX
    ja .bad
    mov ax, [es:si+6]
    mov [pt_gh], ax
    or ax, ax
    jz .bad
    cmp ax, PT_GDIM_MAX
    ja .bad
    mov al, [es:si+8]
    mov ah, al
    and ah, 0x40
    mov [pt_gilace], ah
    add si, 9
    test al, 0x80                   ; a local colour table wins
    jz .pal_ok
    call pt_gctsz
    call pt_gif_pal
    jc .bad
.pal_ok:
    cmp byte [pt_ghavp], 0
    je .bad                         ; no table anywhere: nothing to map from
    cmp si, [pt_fsz]
    jae .bad
    mov al, [es:si]                 ; the LZW minimum code size
    inc si
    cmp al, 2
    jb .bad
    cmp al, 8
    ja .bad
    mov [pt_gmin], al
    call pt_gdeblk                  ; the sub-blocks, flattened in place
    cmp word [pt_glen], 0
    je .bad
    mov ax, [pt_gw]                 ; the canvas takes the picture's size...
    mov dx, [pt_gh]
    call pt_adopt
    call pt_gdec                    ; ...and the stream fills it
    call pt_wfollow
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    mov si, pt_s_badpic
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_gctsz - colour table entries from a packed descriptor byte
; in:  AL = the packed field; out: CX = 2 << (AL & 7); preserves all others
; -----------------------------------------------------------------------------
pt_gctsz:
    push ax
    and al, 7
    mov cl, al
    xor ch, ch
    mov ax, 2
    shl ax, cl
    mov cx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gif_pal - a GIF colour table into pt_pmap
; in:  ES = the file, SI = the table's offset, CX = entries
; out: CF=1 if the table runs past the file (nothing written); else SI is
;      advanced past it and [pt_ghavp] = 1; preserves all other registers
;
; Indices the table does not define map to WHITE rather than to colour 0: an
; index out of range is a corrupt file, and paper is a safer guess than ink.
; -----------------------------------------------------------------------------
pt_gif_pal:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, cx
    mov bx, 3
    mul bx                          ; DX:AX = the table's bytes
    or dx, dx
    jnz .over
    add ax, si
    jc .over
    cmp ax, [pt_fsz]
    ja .over
    mov [pt_gpend], ax
    mov di, 0
    mov bx, 256
    mov al, CWHITE
.pre:
    mov [pt_pmap+di], al
    inc di
    dec bx
    jnz .pre
    xor di, di
.ent:
    push cx                         ; CX is the counter AND pt_map16's green
    mov cl, [es:si]                 ; GIF orders them red, green, blue
    mov ch, [es:si+1]
    mov dl, [es:si+2]
    call pt_map16
    mov [pt_pmap+di], al
    pop cx
    add si, 3
    inc di
    loop .ent
    mov byte [pt_ghavp], 1
    mov si, [pt_gpend]
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.over:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pt_gdeblk - flatten the LZW sub-blocks in place
; in:  ES = the file, SI = the first sub-block's length byte
; out: [pt_gbase] = where the flattened stream starts (SI as passed),
;      [pt_glen] = its length; preserves all registers
;
; Each block's data is copied back over the byte that announced it, so the
; destination always trails the source and one `rep movsb` per block is safe.
; Flattening first is what lets the bit reader be three bytes and a shift
; instead of a sub-block state machine inside the hot loop, and it costs one
; pass over a file that cannot exceed 64KB.
; -----------------------------------------------------------------------------
pt_gdeblk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov [pt_gbase], si
    mov dx, [pt_fsz]                ; the limit, read before DS moves
    mov ax, es
    mov ds, ax                      ; both halves of the copy are the file
    mov di, si
    cld
.blk:
    cmp si, dx
    jae .done
    mov cl, [si]
    inc si
    xor ch, ch
    jcxz .done                      ; the block terminator
    mov ax, si
    add ax, cx
    jc .trunc
    cmp ax, dx
    jbe .copy
.trunc:
    mov cx, dx                      ; a truncated last block: take what is
    sub cx, si                      ; there, then stop
    rep movsb
    jmp short .done
.copy:
    rep movsb
    jmp short .blk
.done:
    pop ds
    mov ax, di
    sub ax, [pt_gbase]
    mov [pt_glen], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gbits - the next [pt_gcsize] bits of the flattened stream
; in:  [pt_gbyte]/[pt_gbit]
; out: AX = the code, CF=1 at the end of the stream; preserves all others
;
; A code spans at most three bytes (7 leftover bits plus twelve), so the window
; is a word and a byte shifted down as a pair. ES is borrowed and given back:
; the caller keeps it on the table segment.
; -----------------------------------------------------------------------------
pt_gbits:
    push bx
    push cx
    push dx
    push es
    mov bx, [pt_gbyte]
    cmp bx, [pt_glen]
    jae .end
    add bx, [pt_gbase]
    mov ax, [pt_gseg]
    mov es, ax
    mov ax, [es:bx]
    mov dl, [es:bx+2]
    xor dh, dh
    xor ch, ch
    mov cl, [pt_gbit]
    jcxz .aligned
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
.aligned:
    mov cl, [pt_gcsize]
    mov bx, 1
    shl bx, cl
    dec bx
    and ax, bx                      ; AX = the code
    add cl, [pt_gbit]               ; and on past it
    mov bl, cl
    and cl, 7
    mov [pt_gbit], cl
    mov cl, 3
    shr bl, cl
    xor bh, bh
    add [pt_gbyte], bx
    pop es
    pop dx
    pop cx
    pop bx
    clc
    ret
.end:
    pop es
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; pt_greset - the reader's table back to its initial state (a Clear code)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_greset:
    push ax
    push cx
    xor ch, ch
    mov cl, [pt_gmin]
    mov ax, 1
    shl ax, cl
    mov [pt_gclr], ax               ; 1 << min
    inc ax
    mov [pt_geoi], ax
    inc ax
    mov [pt_gfree], ax              ; the first code a string can take
    inc cl
    mov [pt_gcsize], cl
    mov word [pt_gold], 0xFFFF
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gpush / pt_gpop - the reader's output stack
; in:  AL = a colour index (push); ES = the table segment
; out: nothing; preserves all registers
;
; LZW hands a string back last character first, so it is unwound onto a stack
; and emitted in reverse. The stack is bounded at 4096 - one entry per possible
; code - and a chain that would overrun it is a corrupt file, cut short rather
; than followed.
; -----------------------------------------------------------------------------
pt_gpush:
    push bx
    mov bx, [pt_gsp]
    cmp bx, 4096
    jae .full
    mov [es:bx+PT_GD_STK], al
    inc bx
    mov [pt_gsp], bx
.full:
    pop bx
    ret

pt_gpop:
    push ax
    push bx
.more:
    mov bx, [pt_gsp]
    or bx, bx
    jz .out
    dec bx
    mov [pt_gsp], bx
    mov al, [es:bx+PT_GD_STK]
    call pt_gemit
    jmp short .more
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gemit - one decoded pixel into the picture
; in:  AL = the file's colour index
; out: nothing; preserves all registers
;
; Columns the canvas does not have are counted but not stored, because the
; stream has to stay in step whether the picture was cropped or not; rows it
; does not have are decoded and dropped for the same reason.
; -----------------------------------------------------------------------------
pt_gemit:
    push ax
    push bx
    push cx
    push di
    cmp byte [pt_gdone], 0
    jne .out
    mov bx, [pt_gcol]
    cmp bx, [pt_cols]
    jae .skip
    mov cl, al
    xor ch, ch
    mov di, cx
    mov al, [pt_pmap+di]
    mov [pt_line+bx], al
.skip:
    inc bx
    mov [pt_gcol], bx
    cmp bx, [pt_gw]
    jb .out
    mov word [pt_gcol], 0           ; the row is complete
    mov di, [pt_grw]
    cmp di, [pt_ch]
    jae .adv
    call pt_line_put
.adv:
    call pt_gnrow
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gnrow - the next destination row, interlace and all
; out: [pt_gdone] = 1 once every row has been placed; preserves all registers
; -----------------------------------------------------------------------------
pt_gnrow:
    push ax
    push bx
    cmp byte [pt_gilace], 0
    jne .ilace
    inc word [pt_grw]
    mov ax, [pt_grw]
    cmp ax, [pt_gh]
    jb .out
    mov byte [pt_gdone], 1
    jmp short .out
.ilace:
    xor bh, bh
    mov bl, [pt_gpass]
    mov al, [pt_gstep+bx]
    xor ah, ah
    add [pt_grw], ax
.chk:
    mov ax, [pt_grw]
    cmp ax, [pt_gh]
    jb .out
    inc byte [pt_gpass]
    cmp byte [pt_gpass], 4
    jb .pass
    mov byte [pt_gdone], 1
    jmp short .out
.pass:
    xor bh, bh
    mov bl, [pt_gpass]
    mov al, [pt_gstart+bx]
    xor ah, ah
    mov [pt_grw], ax
    jmp short .chk                  ; a pass starting past the bottom is empty
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gdec - the reader's LZW loop
; in:  the header fields, the canvas laid out and wiped
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_gdec:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [pt_lzwseg]
    mov es, ax                      ; the tables, for the whole loop
    mov word [pt_gbyte], 0
    mov byte [pt_gbit], 0
    mov word [pt_gcol], 0
    mov byte [pt_gpass], 0
    mov word [pt_grw], 0
    mov byte [pt_gdone], 0
    call pt_greset
.next:
    cmp byte [pt_gdone], 0
    jne .out
    call pt_gbits
    jc .out
    cmp ax, [pt_gclr]
    je .clear
    cmp ax, [pt_geoi]
    je .out
    mov [pt_gcode], ax
    cmp word [pt_gold], 0xFFFF
    jne .chain
    mov [pt_gold], ax               ; the first code after a Clear is a root
    mov [pt_gfirst], ax
    call pt_gemit
    jmp .next
.chain:
    mov word [pt_gsp], 0
    cmp ax, [pt_gfree]
    jb .walk
    mov ax, [pt_gfirst]             ; the code about to be defined: its own
    call pt_gpush                   ; first character closes the string
    mov ax, [pt_gold]
.walk:
    cmp word [pt_gsp], 4096
    jae .root                       ; a cyclic chain: stop unwinding
    cmp ax, [pt_gclr]
    jb .root
    mov bx, ax
    add bx, bx
    mov dx, [es:bx+PT_GD_PREF]      ; the prefix first: BX is about to move
    mov bx, ax
    mov al, [es:bx+PT_GD_SUFF]
    call pt_gpush
    mov ax, dx
    jmp short .walk
.root:
    mov [pt_gfirst], ax
    call pt_gpush
    call pt_gpop                    ; and out, top of the stack first
    mov bx, [pt_gfree]
    cmp bx, 4096
    jae .noadd
    mov ax, bx
    add ax, ax
    mov di, ax
    mov ax, [pt_gold]
    mov [es:di+PT_GD_PREF], ax
    mov ax, [pt_gfirst]
    mov [es:bx+PT_GD_SUFF], al
    inc bx
    mov [pt_gfree], bx
    cmp byte [pt_gcsize], 12
    jae .noadd
    xor ch, ch
    mov cl, [pt_gcsize]
    mov ax, 1
    shl ax, cl
    cmp bx, ax                      ; the table crossed a power of two
    jb .noadd
    inc byte [pt_gcsize]
.noadd:
    mov ax, [pt_gcode]
    mov [pt_gold], ax
    jmp .next
.clear:
    call pt_greset
    jmp .next
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
; pt_line_get - canvas row DI into pt_line, one colour index per column
; in:  DI = canvas row; out: pt_line holds [pt_cw] indices; preserves all
; -----------------------------------------------------------------------------
pt_line_get:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, di
    call pt_rowset                  ; ES:DI = the row
    mov bx, di
    xor si, si
.col:
    mov di, si
    shr di, 1
    add di, bx
    mov al, [es:di]
    test si, 1
    jnz .lo
    mov cl, 4
    shr al, cl                      ; the high nibble is the left pixel
    jmp short .put
.lo:
    and al, 0x0F
.put:
    mov [pt_line+si], al
    inc si
    cmp si, [pt_cw]
    jb .col
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gwr - one byte into the GIF being built
; in:  AL; out: nothing ([pt_govf] set if it does not fit); preserves all
; -----------------------------------------------------------------------------
pt_gwr:
    push ax
    push bx
    push cx
    push es
    mov cl, al                      ; the byte, before AX becomes the segment
    mov bx, [pt_gout]
    cmp bx, [pt_gcap]
    jae .full
    mov ax, [pt_gseg]
    mov es, ax
    mov [es:bx], cl
    inc bx
    mov [pt_gout], bx
    jmp short .done
.full:
    mov byte [pt_govf], 1
.done:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; pt_gwrw - a little-endian word; preserves all registers
pt_gwrw:
    push ax
    call pt_gwr
    mov al, ah
    call pt_gwr
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_ghdr - magic, screen descriptor, our palette, image descriptor
; out: nothing; preserves all registers
;
; The logical screen IS the picture and the global table IS our sixteen, in the
; order pt_pal_rgb already keeps them - which is GIF's own red, green, blue.
; -----------------------------------------------------------------------------
pt_ghdr:
    push ax
    push cx
    push si
    mov si, pt_g_magic
    mov cx, 6
.mag:
    mov al, [si]
    call pt_gwr
    inc si
    loop .mag
    mov ax, [pt_cw]
    call pt_gwrw
    mov ax, [pt_ch]
    call pt_gwrw
    mov al, 0x83                    ; a global table of 2 << 3 entries
    call pt_gwr
    xor al, al                      ; background index
    call pt_gwr
    xor al, al                      ; no aspect ratio given
    call pt_gwr
    mov si, pt_pal_rgb
    mov cx, 48
.pal:
    mov al, [si]
    call pt_gwr
    inc si
    loop .pal
    mov al, 0x2C                    ; the image descriptor
    call pt_gwr
    xor ax, ax
    call pt_gwrw                    ; left
    xor ax, ax
    call pt_gwrw                    ; top
    mov ax, [pt_cw]
    call pt_gwrw
    mov ax, [pt_ch]
    call pt_gwrw
    xor al, al                      ; no local table, not interlaced
    call pt_gwr
    mov al, 4                       ; LZW minimum code size: sixteen colours
    call pt_gwr
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gbo / pt_gflush - the writer's sub-block staging
; in:  AL = one data byte (pt_gbo)
; out: nothing; preserves all registers
;
; GIF wants its LZW data in blocks of at most 255 bytes, so the natural staging
; unit is one block in bss - which is also what keeps ES on the table segment
; through the whole encode: the far write happens once per block, not per byte.
; -----------------------------------------------------------------------------
pt_gbo:
    push bx
    xor bx, bx
    mov bl, [pt_gbn]
    mov [pt_gbuf+bx], al
    inc bl
    mov [pt_gbn], bl
    cmp bl, 255
    jb .out
    call pt_gflush
.out:
    pop bx
    ret

pt_gflush:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    xor cx, cx
    mov cl, [pt_gbn]
    jcxz .out
    mov ax, [pt_gout]
    add ax, cx
    inc ax
    cmp ax, [pt_gcap]
    ja .full
    mov ax, [pt_gseg]
    mov es, ax
    mov di, [pt_gout]
    mov al, cl
    mov [es:di], al                 ; the block's own length byte
    inc di
    mov si, pt_gbuf
    cld
    rep movsb
    mov [pt_gout], di
    mov byte [pt_gbn], 0
    jmp short .out
.full:
    mov byte [pt_govf], 1
    mov byte [pt_gbn], 0
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gput - one code into the bit stream, [pt_gcsize] bits wide
; in:  AX = the code; out: nothing; preserves all registers
;
; Up to 7 pending bits plus a 12-bit code is 19, so the accumulator is a word
; and a spill word shifted down as a pair - two iterations at most.
; -----------------------------------------------------------------------------
pt_gput:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax                      ; DX = the code
    xor ch, ch
    mov cl, [pt_gaccn]
    mov ax, dx
    shl ax, cl                      ; the low sixteen bits of code << pending
    or ax, [pt_gacc]
    mov bx, dx
    neg cl
    add cl, 16
    shr bx, cl                      ; and whatever spilled past them
    mov cl, [pt_gaccn]
    add cl, [pt_gcsize]             ; CL = bits now held
.byte:
    cmp cl, 8
    jb .rest
    push cx
    call pt_gbo                     ; AL goes out
    pop cx
    mov al, ah                      ; BX:AX >>= 8
    mov ah, bl
    mov bl, bh
    mov bh, 0
    sub cl, 8
    jmp short .byte
.rest:
    mov [pt_gacc], ax
    mov [pt_gaccn], cl
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gtclr - the writer's roots back to childless
; out: nothing; preserves all registers
;
; Only the eighteen initial codes need clearing, however full the table got:
; every code above them has its child word written when it is created. That is
; what makes a Clear cheap enough to do as often as an 11-bit ceiling asks for.
; -----------------------------------------------------------------------------
pt_gtclr:
    push ax
    push bx
    push cx
    xor bx, bx
    mov cx, 18
    mov ax, 0xFFFF
.z:
    mov [es:bx+PT_GE_CHILD], ax
    inc bx
    inc bx
    loop .z
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gfind - the child of [pt_gent] whose suffix is BL
; in:  BL = the suffix, ES = the tables
; out: CF=0 with AX = that code, CF=1 if there is none; preserves all others
;
; A sibling chain, not a hash table: with a 4-bit minimum code size a node has
; at most sixteen children, so the walk is bounded by sixteen byte compares and
; needs no probe sequence, no load factor and no third array of codes.
; -----------------------------------------------------------------------------
pt_gfind:
    push bx
    push dx
    mov dl, bl
    mov bx, [pt_gent]
    add bx, bx
    mov ax, [es:bx+PT_GE_CHILD]
.walk:
    cmp ax, 0xFFFF
    je .none
    mov bx, ax
    cmp [es:bx+PT_GE_SUF], dl
    je .found
    add bx, bx
    mov ax, [es:bx+PT_GE_SIB]
    jmp short .walk
.found:
    pop dx
    pop bx
    clc
    ret
.none:
    pop dx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; pt_gadd - give [pt_gent] a child for suffix BL, or start the table over
; in:  BL = the suffix, ES = the tables
; out: nothing; preserves all registers
;
; The one place where writing and reading LZW are NOT mirror images, and the
; only place it matters. A writer defines its new string as it emits the code
; before it; a reader cannot define that string until it has seen the code
; AFTER it (which is what the KwKwK case in pt_gdec covers). So the reader's
; table is permanently one entry behind the writer's, and the two rules that
; decide the code WIDTH have to be off by one to compensate: the writer widens
; when free passes 1<<size, the reader when free reaches it. Both then widen
; before the same code, which is the only thing that has to be true.
;
; The same offset is why PT_GE_MAXC is 2047 and not 2048: the writer must Clear
; one string early, or the reader's trailing entry lands on 2048 and it widens
; to twelve bits for a Clear code the writer emitted in eleven. Verified by
; round-tripping flat, banded, striped and pure-noise pictures through a host
; decoder - noise is what fills the table fast enough to reach a Clear at all.
; -----------------------------------------------------------------------------
pt_gadd:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, bl                      ; AL = the suffix, BX is about to be an index
    mov di, [pt_gfree]
    cmp di, PT_GE_MAXC
    jae .full
    mov bx, di
    mov [es:bx+PT_GE_SUF], al
    mov si, di
    add si, si
    mov word [es:si+PT_GE_CHILD], 0xFFFF
    mov bx, [pt_gent]
    add bx, bx
    mov cx, [es:bx+PT_GE_CHILD]
    mov [es:si+PT_GE_SIB], cx       ; the parent's old first child...
    mov [es:bx+PT_GE_CHILD], di     ; ...now hangs off us
    inc di
    mov [pt_gfree], di
    cmp byte [pt_gcsize], 12
    jae .out
    xor ch, ch
    mov cl, [pt_gcsize]
    mov ax, 1
    shl ax, cl
    cmp di, ax                      ; STRICTLY greater: see the note above
    jbe .out
    inc byte [pt_gcsize]
    jmp short .out
.full:
    mov ax, 16                      ; Clear, and everything starts again
    call pt_gput
    call pt_gtclr
    mov word [pt_gfree], 18
    mov byte [pt_gcsize], 5
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_genc - the writer's LZW loop, one canvas row at a time
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pt_genc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [pt_lzwseg]
    mov es, ax                      ; the tables, for the whole loop
    mov byte [pt_gbn], 0
    mov word [pt_gacc], 0
    mov byte [pt_gaccn], 0
    mov word [pt_gfree], 18
    mov byte [pt_gcsize], 5
    call pt_gtclr
    mov word [pt_gent], 0xFFFF
    mov ax, 16                      ; a leading Clear, as every writer emits
    call pt_gput
    xor di, di
.row:
    cmp di, [pt_ch]
    jae .last
    call pt_line_get
    xor si, si
.px:
    cmp si, [pt_cw]
    jae .nextrow
    xor bh, bh
    mov bl, [pt_line+si]
    and bl, 0x0F
    cmp word [pt_gent], 0xFFFF
    jne .have
    mov [pt_gent], bx               ; the picture's first pixel
    jmp short .adv
.have:
    call pt_gfind
    jc .miss
    mov [pt_gent], ax               ; the string grows by one
    jmp short .adv
.miss:
    mov ax, [pt_gent]               ; the longest match goes out...
    call pt_gput
    call pt_gadd                    ; ...and gains a child for this pixel
    mov [pt_gent], bx
.adv:
    inc si
    jmp short .px
.nextrow:
    inc di
    jmp short .row
.last:
    cmp word [pt_gent], 0xFFFF
    je .eoi
    mov ax, [pt_gent]
    call pt_gput
.eoi:
    mov ax, 17                      ; End Of Information
    call pt_gput
    cmp byte [pt_gaccn], 0          ; the last partial byte...
    je .blk
    mov al, [pt_gacc]
    call pt_gbo
    mov word [pt_gacc], 0
    mov byte [pt_gaccn], 0
.blk:
    call pt_gflush                  ; ...and the block holding it
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_gif_out - write the canvas as a GIF under the name in SI
; in:  SI = NUL 8.3 name; UI task, gfx lock held
; out: [pt_msgp] = the toast; preserves all registers
;
; The file is built in the undo image, so its ceiling is that buffer or the
; file API's 64KB, whichever is lower (SPEC.md 18.4). LZW normally beats the
; raw 4bpp rows by a wide margin on drawings, but it can expand noise, so the
; capacity is checked at every block and an overflow reports rather than
; writing a truncated GIF.
; -----------------------------------------------------------------------------
pt_gif_out:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Where the file is BUILT, and how much of it there is room for. It used
    ; to be [pt_unseg] unconditionally, with pt_save gating only on the
    ; clipboard - so on a machine whose undo image had been handed back to
    ; fund a big canvas this encoded the GIF at 0000:0000, over the interrupt
    ; vector table, and the machine died mid-save. The load path already had
    ; the answer (pt_load's .scratch): stage in the flood-fill scratch, which
    ; is idle here and re-initialised by the next fill.
    ;
    ; **The gate is [pt_unseg], not [pt_haveundo].** The two agree today, so
    ; testing the flag happens to work - but they are different questions.
    ; [pt_haveundo] means "the user can Undo" and is what the menu and pt_gate
    ; ask; this needs "there is a buffer I can put a segment register on", and
    ; the only variable that means that is the segment. The three sites that
    ; stage in the undo image (here, pt_load, and pt_resize's in-place path)
    ; all ask the second question, so all three test the segment. The moment
    ; anything makes an undo image that is not conventionally addressable,
    ; testing the flag puts ES back at 0000 and this fault returns - which is
    ; not hypothetical: it is what the paragraph above is about.
    mov ax, PT_GIF_MAX_KB           ; a TRANSIENT claim, like the load's
    call pt_stage                   ; (SPEC.md 42.6). This used to build the
    jc .nostage                     ; GIF in the undo image - and, with no
                                    ; undo image, in the twelve kilobytes of
                                    ; the flood-fill stack - which is why
                                    ; saving a GIF threw away the undo history
                                    ; and why a small machine could not save
                                    ; one at all
    or dx, dx                       ; DX:CX = the bytes claimed, capped at
    jnz .full                       ; what a 16-bit write cursor can address
    cmp cx, 65520
    jbe .cap
.full:
    mov cx, 65520
.cap:
    mov [pt_gcap], cx
    mov word [pt_gout], 0
    mov byte [pt_govf], 0
    call pt_ghdr
    call pt_genc
    xor al, al                      ; the LZW block terminator...
    call pt_gwr
    mov al, 0x3B                    ; ...and the trailer
    call pt_gwr
    cmp byte [pt_govf], 0
    jne .toobig                     ; the ENCODER's own ceiling, not the file
    mov cx, [pt_gout]               ; API's: pt_gout is a 16-bit offset into
    xor dx, dx                      ; one staging segment (SPEC.md 18.4.1)
    mov ax, [pt_gseg]
    mov es, ax
    xor bx, bx
    call OSAPI_FILE_WRITE           ; SI is still the name
    jnc .wrote
    call pt_ferr
    jmp short .out
.wrote:
    mov word [pt_msgp], pt_s_wrote
    mov byte [pt_trunc], 0          ; what is on disk now IS what we hold
    jmp short .out
.toobig:
    mov word [pt_msgp], pt_s_toobig
    jmp short .out
.nostage:
    mov word [pt_msgp], pt_s_nostage
.out:
    call pt_unstage
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

pt_s_title:  db 'Paint', 0          ; the title's stem, and the fingerprint
                                    ; pt_dupchk matches on
pt_appname:  db 'Paint', 0          ; the menu bar's app label (SPEC.md 12.2)

; --- the About card (SPEC.md 12.2/42) ---------------------------------------
; A 12px line pitch rather than the kernel About's 16: every line has to fit
; a CGA content of about 132 rows (SPEC.md 39.2), and pt_abmeas clamps to
; whatever the window has actually been resized to anyway.
PT_ABLH     equ 12
pt_ablines:
    dw pt_ab_1, pt_ab_2, pt_ab_3, pt_ab_4, 0
pt_ab_1:     db 'Paint for os8088', 0
pt_ab_2:     db 'a bitmap editor for the 8086', 0
pt_ab_3:     db 0                   ; a blank line is a line with no glyphs
pt_ab_4:     db 'Contributed by Elendilon', 0
pt_s_defname: db 'PICTURE.BMP', 0
pt_s_defgif:  db 'PICTURE.GIF', 0
pt_s_ebmp:    db 'BMP', 0
pt_s_egif:    db 'GIF', 0
pt_g_magic:   db 'GIF87a'
pt_gstep:     db 8, 8, 4, 2         ; the interlaced row passes: step...
pt_gstart:    db 0, 4, 2, 1         ; ...and where each one starts
pt_s_crop:   db 'Would crop artwork', 0
pt_s_noram:  db 'No memory to resize', 0
pt_s_saving: db 'Saving...', 0
pt_s_wlab:   db 'W', 0
pt_s_hlab:   db 'H', 0
pt_s_apply:  db 'Apply', 0
pt_s_nofsx:  db 'The screen is not free', 0
pt_s_nodlg:  db 'Leave Full Screen first', 0
pt_s_nu:     db 'No RAM for undo here', 0
pt_s_nc:     db 'No RAM for clipboard', 0
pt_s_ng:     db 'No RAM for GIF here', 0

; --- the window template (SPEC.md 11; x/y/w/h patched by pt_geom) -------------
pt_tpl:
    dw 0                            ; WT_X
    dw PT_WIN_Y                     ; WT_Y
    dw PT_CW_DEF + PT_CHROME_W      ; WT_W
    dw PT_CH_DEF + PT_CHROME_H      ; WT_H
    dw pt_s_title                   ; WT_TITLE
    dw pt_paint                     ; WT_PAINT
    dw pt_onkey                     ; WT_ONKEY
    dw pt_click                     ; WT_ONCLICK

; --- menus (SPEC.md 12.2) ----------------------------------------------------
    OS88_MENUSET pt_menus, pt_appname, pt_oncmd
        OS88_MENU pt_s_file, pt_it_file, 6
        OS88_MENU pt_s_edit, pt_it_edit, 5
        OS88_MENU pt_s_draw, pt_it_draw, 4
        OS88_MENU pt_s_view, pt_it_view, 1
    OS88_MENUSET_END pt_menus

pt_s_file:   db 'File', 0
pt_s_edit:   db 'Edit', 0
pt_s_draw:   db 'Draw', 0
pt_s_view:   db 'View', 0
pt_it_file:  dw pt_i_new, pt_i_open, pt_i_save, pt_i_saveas
             dw pt_i_saveg, pt_i_saveag
pt_it_edit:  dw pt_i_undo, pt_i_cut, pt_i_copy, pt_i_paste, pt_i_clear
pt_it_draw:  dw pt_i_fill, pt_i_f1, pt_i_f2, pt_i_f4
pt_it_view:  dw pt_i_full
pt_i_new:    db 'New', 0
pt_i_open:   db 'Open...', 0
pt_i_save:   db 'Save Bmp', 0
pt_i_saveas: db 'Save as Bmp...', 0
pt_i_saveg:  db 'Save Gif', 0
pt_i_saveag: db 'Save as Gif...', 0
; --- and the same items on a machine that cannot fund them (SPEC.md 42) -----
; The leading MENU_DIS byte is the kernel's disabled marker (SPEC.md 12.2):
; the item draws grey and cannot be highlighted or picked. The suffix stays,
; shortened - greying says "not now", the words say why - and at 24 glyphs
; the longest of them fits the pull-down without truncation.
pt_i_undo2:   db MENU_DIS, 'Undo / Redo (NoRam)', 0
pt_i_cut2:    db MENU_DIS, 'Cut (NoRam)', 0
pt_i_copy2:   db MENU_DIS, 'Copy (NoRam)', 0
pt_i_paste2:  db MENU_DIS, 'Paste (NoRam)', 0
pt_i_saveg2:  db MENU_DIS, 'Save Gif (NoRam)', 0
pt_i_saveag2: db MENU_DIS, 'Save as Gif... (NoRam)', 0
pt_i_undo:   db 'Undo / Redo', 0
pt_i_cut:    db 'Cut', 0
pt_i_copy:   db 'Copy', 0
pt_i_paste:  db 'Paste', 0
pt_i_clear:  db 'Clear', 0
pt_i_fill:   db 'Filled Shapes', 0
pt_i_f1:     db 'Text Size 1x', 0
pt_i_f2:     db 'Text Size 2x', 0
pt_i_f4:     db 'Text Size 4x', 0
; Never greyed, on any machine: this sets no video mode, so OSAPI_FSX_CAPS has
; nothing to say about it and there is no FACT to grey on (SPEC.md 47 rule 3).
pt_i_full:   db 'Full Screen  ^F', 0

; --- the notice windows (indexed by [pt_mode] - 1) ---------------------------
pt_notice:   dw pt_s_nomem, pt_s_small
pt_s_nomem:  db 'Not enough memory.', 0
pt_s_small:  db 'The screen is too small.', 0
pt_s_note2:  db 'Close this window.', 0

; --- toasts ------------------------------------------------------------------
pt_s_wrote:   db 'Saved', 0
pt_s_opened:  db 'Opened', 0
pt_s_nofmt:   db 'Only BMP and GIF', 0
pt_s_badpic:  db 'Unreadable picture', 0
pt_s_bigpic:  db 'Picture too big', 0
pt_s_nocomp:  db 'No compressed BMP', 0
pt_s_nodepth: db 'Need 1/4/8/24-bit BMP', 0
pt_s_toobig:  db 'GIF too big - try Bmp', 0
pt_s_nostage: db 'No memory to open file', 0
pt_s_gifbig:  db 'GIF larger than 64KB', 0
pt_s_trunc:   db 'Cropped - use Save As', 0
pt_s_bigsel:  db 'Selection too big', 0

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
; --- the BMP header's fixed 54 bytes; pt_bmp_hdr patches the other four fields
pt_hdrtpl:
    db 'BM'
    dd 0                            ; bfSize (patched)
    dw 0, 0                         ; bfReserved1/2
    dd PT_BMPHDR                    ; bfOffBits
    dd 40                           ; biSize: BITMAPINFOHEADER
    dd 0                            ; biWidth (patched)
    dd 0                            ; biHeight (patched, positive = bottom-up)
    dw 1                            ; biPlanes
    dw 4                            ; biBitCount
    dd 0                            ; biCompression = BI_RGB
    dd 0                            ; biSizeImage (patched)
    dd 0                            ; biXPelsPerMeter
    dd 0                            ; biYPelsPerMeter
    dd 16                           ; biClrUsed
    dd 0                            ; biClrImportant

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
    PTWORD pt_bx0                   ; pt_blit: the band's (even) left edge...
    PTWORD pt_bwid                  ; ...its width in pixels...
    PTWORD pt_bsi                   ; ...its first canvas row...
    PTWORD pt_bn                    ; ...how many rows it covers...
    PTWORD pt_brmax                 ; ...and the most that fit one segment

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
    PTWORD pt_msgp                  ; the toast a file operation asked for
    PTBYTE pt_abon                  ; 1 = the About card is up (SPEC.md 12.2)
    PTWORD pt_abl                   ; ...and where pt_abmeas settled it,
    PTWORD pt_abt                   ; content coords
    PTWORD pt_abw
    PTWORD pt_abh
    PTWORD pt_uy1                   ; the rows an undo swap touched
    PTWORD pt_uy2
    PTWORD pt_mbest                 ; pt_map16's best distance so far

    ; the BMP reader
    PTWORD pt_fsz                   ; bytes actually read, low word...
    PTWORD pt_fszh                  ; ...and high ([pt_fsz+2], SPEC.md 42.6)
    PTWORD pt_boff                  ; bfOffBits, low word...
    PTWORD pt_boffh                 ; ...and high ([pt_boff+2])
    PTWORD pt_bhsz                  ; biSize
    PTWORD pt_bw                    ; biWidth
    PTWORD pt_bh                    ; |biHeight|
    PTWORD pt_bpp                   ; biBitCount
    PTWORD pt_bstr                  ; source row stride
    PTWORD pt_srow                  ; the source row being read: an offset of
                                    ; 0..15, the segment part having gone into
                                    ; ES (pt_srowset)
    PTWORD pt_stgkb                 ; pt_stage: the transient staging claim's
                                    ; size in KB, across the claim call
    PTWORD pt_cols                  ; columns we take from it

    ; the canvas, its geometry and the memory that funds it
    PTWORD pt_cw                    ; canvas width in pixels
    PTWORD pt_stride                ; ...and the BMP row stride that implies
    PTWORD pt_cvparas               ; what the canvas costs, in paragraphs
    PTWORD pt_cwmax                 ; the biggest canvas this SCREEN can show
    PTWORD pt_chmax
    PTWORD pt_smaxp                 ; ...and what the canvas CLAIM holds, paras
    PTWORD pt_growp                 ; ...and what a NEW claim could hold, paras
    PTWORD pt_needp                 ; pt_alloc scratch: paragraphs per canvas
    PTWORD pt_obase                 ; pt_resize: the canvas claim being replaced
    PTWORD pt_osrc                  ; ...the segment pt_orowset reads through
    PTWORD pt_scrw                  ; screen width, for centring the window
    PTWORD pt_unseg                 ; the undo image's base segment
    PTWORD pt_cbseg                 ; the clipboard's
    PTWORD pt_cbparas               ; ...and how many paragraphs it got
    PTWORD pt_scseg                 ; scratch: claim record + fill stack
    PTWORD pt_undelta               ; canvas segment -> undo segment
    PTWORD pt_contw                 ; the live content rect, from the record
    PTWORD pt_conth
    PTWORD pt_filx                  ; the strip's right-anchored controls
    PTWORD pt_fntx
    PTWORD pt_nsw                   ; colour swatches that fit...
    PTWORD pt_swdx                  ; ...at this pitch, this wide
    PTWORD pt_swsz
    PTWORD pt_ocw                   ; pt_resize: the outgoing geometry
    PTWORD pt_och
    PTWORD pt_ostride
    PTWORD pt_orseg                 ; pt_orowset's answer
    PTWORD pt_dockr                 ; the first row the dock owns
    PTWORD pt_lwb                   ; pt_lose_*: first byte of the doomed band
    PTWORD pt_scrh                  ; screen height, for the bracket's pointer
    PTWORD pt_ptrx                  ; ...and where its crosshair is DRAWN, which
    PTWORD pt_ptry                  ; is what the XOR erase has to replay
    PTWORD pt_pnx                   ; pt_ptr_move: where it is going...
    PTWORD pt_pny
    PTBUF  pt_pold, 8               ; ...and the two squares, l/t/r/b
    PTBUF  pt_pnew, 8
    PTWORD pt_ovl                   ; pt_ptr_sub: where they overlap
    PTWORD pt_ovt
    PTWORD pt_ovr
    PTWORD pt_ovb

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
    PTBYTE pt_dall                  ; 1 = this paint owes the WHOLE content
    PTWORD pt_dx1                   ; ...else the rect it owes, CONTENT-relative
    PTWORD pt_dy1                   ; (SPEC.md 11.90.2)
    PTWORD pt_dx2
    PTWORD pt_dy2
    PTBYTE pt_selshown              ; ...and its marquee is on the glass
    PTBYTE pt_txton                 ; a text run is open
    PTBYTE pt_careton               ; ...and its caret is on the glass
    PTBYTE pt_fs                    ; a full-screen session is on: set by
                                    ; pt_cmd_fs BEFORE the bracket, so the
                                    ; W_PAINT inside it already knows. NOT the
                                    ; SPEC.md 11.2 surface - this app never
                                    ; takes one (SPEC.md 42.7)
    PTBYTE pt_fsx                   ; ...and pt_fsx_main is actually running,
                                    ; which is what pt_org branches on
    PTBYTE pt_ptron                 ; the bracket's crosshair is on the glass
    PTBYTE pt_pbtn                  ; ...and the mouse buttons it last saw
    PTBYTE pt_undo_ok               ; the undo image holds something
    PTBYTE pt_undo_off              ; we ARE the undo: do not re-snapshot
    PTBYTE pt_noscr                 ; pt_rect: pixels only, no gfx call
    PTWORD pt_lsx0                  ; pt_segdo: the segment's start, banked
    PTWORD pt_lsy0                  ; before the walk moves the brush
    PTWORD pt_lx                    ; pt_lineseg's walk: the live point...
    PTWORD pt_ly
    PTWORD pt_lx2                   ; ...its far end...
    PTWORD pt_ly2
    PTWORD pt_ldx                   ; ...and the Bresenham state
    PTWORD pt_ldy
    PTWORD pt_lsgx
    PTWORD pt_lerr
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
    PTWORD pt_fsize                 ; the chosen file's size, from the dialog
    PTWORD pt_fsize_hi              ; (SPEC.md 38.6); 0 = it reported none
    PTBYTE pt_trunc                 ; the loaded picture was cropped: File >
                                    ; Save must not overwrite the original
    PTBYTE pt_fitcut                ; pt_fit had to give something up
    PTBYTE pt_setval                ; pt_setpx's colour, across pt_rowset
    PTBYTE pt_wchg                  ; the window was resized: repaint the lot
    PTBYTE pt_kept                  ; a resize was refused to save the artwork
    PTBYTE pt_apend                 ; ...and the notice for it is owed
    PTWORD pt_wanth                 ; pt_onsize: the height it settled on, held
                                    ; over the register shuffle on the way out
    PTWORD pt_base                  ; the claimed block's base (SPEC.md 50.3)
    PTBYTE pt_haveundo              ; this machine can fund an undo image...
    PTBYTE pt_haveclip              ; ...and a clipboard (SPEC.md 42)
    PTBYTE pt_fbox                  ; the size box with the keyboard, 0 = none
    PTBYTE pt_fbold                 ; ...as it was before this click
    PTBYTE pt_fresh                 ; the next digit replaces the value
    PTBYTE pt_szi                   ; pt_szdraw's box counter
    PTBYTE pt_szchg                 ; pt_setsize moved the canvas
    PTBYTE pt_szmem                 ; ...and refused for want of memory, not ink
    PTWORD pt_reqw                  ; the size asked for, before pt_fit had its
    PTWORD pt_reqh                  ; say - so the two can be compared
    PTWORD pt_lzwseg                ; the LZW tables' own claim, 0 = none
    PTWORD pt_gseg                  ; where a GIF is staged, either direction:
                                    ; the undo image when there is one, the
                                    ; flood-fill scratch when there is not
    PTBYTE pt_apsay                 ; 1 = pt_apend also owes the user a notice
    PTBYTE pt_pinw                  ; pt_fit: an axis pt_sizeask's ink guard
    PTBYTE pt_pinh                  ; has already settled - do not cut it
    PTWORD pt_szy                   ; the box being drawn: its top row...
    PTWORD pt_szp                   ; ...and the text it shows
    PTBUF  pt_wtxt, 4               ; the two edit buffers, three digits and a
    PTBUF  pt_htxt, 4               ; NUL
    PTBYTE pt_lwn                   ; pt_lose_w: the boundary nibble matters
    PTBYTE pt_sfmt                  ; the save verb's format: 0 BMP, 1 GIF
    PTWORD pt_pw                    ; a loaded picture's own width
    PTWORD pt_gw                    ; --- GIF (SPEC.md 42) ---
    PTWORD pt_gh                    ; the image descriptor's own size
    PTWORD pt_gpend                 ; one past a colour table
    PTWORD pt_gbase                 ; the flattened LZW stream: where...
    PTWORD pt_glen                  ; ...and how long
    PTWORD pt_gbyte                 ; the bit reader's position
    PTBYTE pt_gbit
    PTBYTE pt_gmin                  ; the stream's minimum code size
    PTBYTE pt_gcsize                ; the code width in force
    PTWORD pt_gclr                  ; the Clear code...
    PTWORD pt_geoi                  ; ...and End Of Information
    PTWORD pt_gfree                 ; the next code to hand out
    PTWORD pt_gold                  ; the previous code, 0xFFFF = none
    PTWORD pt_gfirst                ; its string's first character
    PTWORD pt_gcode                 ; the code as it arrived
    PTWORD pt_gsp                   ; the output stack's depth
    PTWORD pt_gcol                  ; where in the row we are
    PTWORD pt_grw                   ; and which row that is
    PTBYTE pt_gpass                 ; the interlace pass
    PTBYTE pt_gilace                ; non-zero if interlaced
    PTBYTE pt_gdone                 ; every row placed
    PTBYTE pt_ghavp                 ; a colour table was found
    PTWORD pt_gent                  ; the writer's longest match so far
    PTWORD pt_gacc                  ; its bit accumulator...
    PTBYTE pt_gaccn                 ; ...and how many bits are in it
    PTWORD pt_gout                  ; the write cursor in the undo image
    PTWORD pt_gcap                  ; and what it may not pass
    PTBYTE pt_govf                  ; it tried to
    PTBYTE pt_gbn                   ; bytes staged for the current sub-block
    PTBUF  pt_gbuf, 255             ; the sub-block itself
    PTBUF  pt_fdigit, 2             ; the strip's scale digit, NUL-terminated

    PTBUF  pt_umask, PT_CH_MAX / 8  ; one bit per canvas row
    PTBUF  pt_rowseg, PT_CH_MAX * 2 ; canvas row -> the paragraph it starts in
    PTBUF  pt_rowoff, PT_CH_MAX * 2 ; ...and the 0..15 bytes into it. Together
                                    ; they are the whole of the bottom-up
                                    ; story AND of spanning segments
    PTWORD pt_fseg                  ; the ROM 8x8 font, where the BIOS keeps it
    PTWORD pt_foff
    PTBUF  pt_gl8, 8                ; ...and the one glyph being drawn
    PTBUF  pt_line, PT_CW_MAX       ; one decoded source pixel per column
    PTBUF  pt_dimbuf, 8             ; "x464", NUL - the palette's size readout
    PTBUF  pt_pmap, 256             ; a loaded palette -> our sixteen
    PTBUF  pt_name, 14              ; the current document
    PTBUF  pt_argp, 1               ; 1 = we were LAUNCHED to open pt_name
                                    ; (SPEC.md 54.5) and the first paint owes
                                    ; the load
    PTBUF  pt_argdrv, 1             ; ...and where it lives
    PTBUF  pt_argclus, 2

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%include "os88ui.inc"

    OS88_BSS PT_BSS
    OS88_IMAGE_END
