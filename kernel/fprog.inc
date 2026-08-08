; =============================================================================
; os8088 - fprog.inc (SPEC.md 12.8)
;
; The file-operation progress widget: a document icon and a progress bar in a
; line box, at the right end of the menu bar immediately left of the clock
; cell (SPEC.md 12.1).
;
; A file operation freezes the machine, and that is by design rather than by
; accident: the disk is the UI task's (SPEC.md 18), dsk_xfer raises
; [sch_lock] across every int 13h, and the caller is a window callback that
; has held the gfx lock since before it started (SPEC.md 22). So the cursor
; stops, no window paints, and nothing moves for as long as the disk takes -
; 65 ms per sector in a run on the field machine (PERFORMANCE.md Part 2 -
; 238 before SPEC.md 18.91's AL fix), which for a
; 116KB module is tens of seconds of a screen that looks hung. What was
; missing was never concurrency; it was FEEDBACK. This is stepped from inside
; the transfer loop, the one piece of code still running while the rest of
; the machine is stopped.
;
; Three entry points, and the split between them is the whole performance
; argument. A gfx_* call costs ~756us of ARRIVING whatever it draws, and the
; icon is a two-pass masked blit - so fpg_begin pays for the icon, the bed
; and the box frame ONCE, and fpg_step draws at most one gfx_fill: the strip
; between the pixel column the bar had reached and the one it has reached
; now. A bar that only grows is completely described by its current extent,
; which is spl_bar's argument (SPEC.md 15.3) reused.
;
; Prefix fpg_. Every entry point is called with the gfx lock ALREADY HELD by
; an outer frame and must never take it (gfx_lock is non-reentrant and would
; deadlock a task that cannot be switched away from anyway). All state lives
; in .text with real initialisers - NOT .bss, which -f bin zeroes not at all:
; drv_boot reads the disk before any init routine of ours could run, and a
; garbage [fpg_on] would put this widget's drawing inside the splash.
; =============================================================================

section .text

FPG_ICO_W  equ 12               ; the document glyph's drawn width
FPG_ICO_H  equ 14               ; rows FPG_ICO_Y..FPG_ICO_Y+13
FPG_ICO_Y  equ 3
FPG_ISP    equ 4                ; icon -> box gap
FPG_BOX_W  equ 64               ; the line box, frame included
FPG_BOX_H  equ 9                ; rows FPG_BOX_Y..FPG_BOX_Y+8
FPG_BOX_Y  equ 5
FPG_GAP    equ 8                ; box -> clock cell gap
FPG_W      equ FPG_ICO_W + FPG_ISP + FPG_BOX_W + FPG_GAP    ; 88
FPG_BAR_PX equ FPG_BOX_W - 2    ; 62: the trough, in pixels
FPG_MINX   equ MENU_SYS_XR + 8  ; an origin left of this would collide with
                                ; the System menu: refuse the whole feature

; -----------------------------------------------------------------------------
; fpg_begin - arm the widget and draw everything expensive about it
; in:  AX = sectors this operation expects (0 refuses: nothing to report);
;      DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved
;
; Refuses, silently and completely, in four cases: already armed (an
; operation nested inside another - the OUTER one is the honest scale), the
; splash still owning the screen and the video mode (SPEC.md 15.3), a visible
; fullscreen window over the bar (SPEC.md 11.2), and an origin that would
; reach the System menu on a narrow screen.
; -----------------------------------------------------------------------------
fpg_begin:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [fpg_on], 0        ; the outermost operation owns the widget
    jne .out
    cmp byte [spl_live], 0      ; boot: the splash owns screen and mode
    jne .out
    call wm_fs_vis              ; the bar is under a fullscreen window
    jc .out

    mov bx, [vid_clk_hx]        ; the LIVE cell edge (SPEC.md 39.2), never
    sub bx, FPG_W               ; MENU_CLK_HX - that one is the VGA reference
    cmp bx, FPG_MINX            ; SIGNED, so the one test also catches a screen
    jl .out                     ; narrower than the widget wrapping the sub
    mov [fpg_x0], bx

    or ax, ax                   ; a 0-sector job has no progress to report and
    jz .out                     ; would be fpg_step's divisor: refuse it whole
    mov [fpg_total], ax
    mov word [fpg_done], 0
    mov word [fpg_px], 0

    call cur_unlazy             ; spend gfx_lock's promised hide before we
                                ; draw anywhere near the arrow (SPEC.md 7.1.4)

    mov ax, bx                  ; --- the bed: white over whatever the bar had
    mov cx, bx                  ;     there, since the space is BORROWED and
    add cx, FPG_W - 1           ;     menu_relayout's limit is untouched
    xor bx, bx
    mov dx, MBAR_H - 2          ; the black rule on the last row is not ours
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov ax, [fpg_x0]            ; ...and the bar has lost those cells: tell it
    mov cx, ax                  ; which, so fpg_end gives back exactly them
    add cx, FPG_W - 1           ; and not the whole bar (SPEC.md 12.9)
    call menu_inval

    mov cx, [fpg_x0]            ; --- the document icon
    mov dx, FPG_ICO_Y
    mov si, fpg_ico
    call icon_draw

    mov byte [gfx_color], CBLACK    ; --- the line box round the trough
    call fpg_boxr               ; AX = box left, CX = box right
    mov bx, FPG_BOX_Y
    mov dx, FPG_BOX_Y + FPG_BOX_H - 1
    call gfx_frame

    mov byte [fpg_on], 1
    call gfx_flush              ; we draw MID-lock: without this the whole
                                ; widget waits for a gfx_unlock that does not
                                ; come until the operation is over (SPEC.md 32)
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_step - one sector done
; in:  nothing;  DS = KERNEL_SEG (every caller is kernel code)
; out: nothing - EVERY register and the flags preserved, because the hot
;      caller is the middle of dsk_xfer's per-sector loop, where CX is a loop
;      counter, AX/BX/SI/DI are live transfer state and CF is the routine's
;      return value in flight. spl_step's contract exactly, and for the same
;      reason.
;
; Unarmed this is a compare and a return, and it stays on the disk path for
; the life of the machine at that price (~13 us against a sector's 65 ms).
; Armed, it draws only when the bar's pixel extent actually CHANGED - the
; trough is 62 pixels and an operation is usually more sectors than that, so
; most steps draw nothing at all.
; -----------------------------------------------------------------------------
fpg_step:
    pushf
    cmp byte [fpg_on], 0
    je .out
    push ax                     ; AX/BX/CX/DX/SI and no more: everything this
    push bx                     ; calls below - gfx_fill, gfx_flush, fpg_boxr
    push cx                     ; - is documented "clobbers: flags", and DI/BP
    push dx                     ; /ES are dsk_xfer's live transfer state that
    push si                     ; nothing on this path touches

    mov ax, [fpg_done]
    inc ax
    cmp ax, [fpg_total]         ; the FAT flush and the directory commit are
    jbe .set                    ; outside the job's sector count, so a long
    mov ax, [fpg_total]         ; operation crowds up against 100% rather than
.set:                           ; wrapping (SPEC.md 12.8)
    mov [fpg_done], ax

    mov cx, FPG_BAR_PX
    mul cx                      ; DX:AX = done * 62, so no 16-bit overflow
    div word [fpg_total]        ; AX = filled pixels, 0..62 (total >= 1)
    cmp ax, [fpg_px]
    jbe .done                   ; nothing new to light

    mov si, ax                  ; SI = the new extent
    call fpg_boxr               ; AX = box left, CX = box right (unused here)
    mov bx, ax
    inc bx                      ; BX = the trough's first pixel column
    mov ax, bx
    add ax, [fpg_px]            ; ...the strip starts where the bar had got to
    mov cx, bx
    add cx, si
    dec cx                      ; ...and ends at the new extent
    mov [fpg_px], si
    mov bx, FPG_BOX_Y + 1
    mov dx, FPG_BOX_Y + FPG_BOX_H - 2
    mov byte [gfx_color], CBLACK
    call gfx_fill
    call gfx_flush              ; mid-lock again (SPEC.md 32)

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_end - disarm, and give the bar back
; in:  nothing;  DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved (its call sites sit in
;      the dskw_*_x epilogues, between a body's CF/AX answer and the caller's)
;
; **Only our own cells go back, not the whole bar** (SPEC.md 12.9). This used
; to be menu_force + menu_draw_bar, which white-filled the bar end to end and
; put back the logo, the app name, every cell title AND the clock - so every
; file operation in the system finished with the bar flashing to restore an
; 88px strip.
;
; The split is between the rows that carry menu text and the rows that do
; not. fpg_begin already told the bar which CELLS it took, so menu_bar_text's
; opaque run is about to rewrite the text band from what the widget left to
; what the menu says, in one pass and with no white in between - erasing that
; band here would put the flash straight back. What it cannot reach is the
; icon's top and bottom and the box frame, which live ABOVE and BELOW the
; glyph rows: those are ours to clean up, and they are two thin fills.
; -----------------------------------------------------------------------------
fpg_end:
    pushf
    push ax
    push bx
    push cx
    push dx
    cmp byte [fpg_on], 0
    je .out
    mov byte [fpg_on], 0

    mov byte [gfx_color], CWHITE
    mov ax, [fpg_x0]            ; --- above the text band: the icon's head
    mov cx, ax
    add cx, FPG_W - 1
    xor bx, bx
    mov dx, MENU_TEXT_Y - 1
    call gfx_fill

    mov ax, [fpg_x0]            ; --- and below it: its foot and the box
    mov cx, ax
    add cx, FPG_W - 1
    mov bx, MENU_TEXT_Y + 8
    mov dx, MBAR_H - 2
    call gfx_fill

    call menu_draw_bar          ; the text band, as the ONE run that differs
    call gfx_flush
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_boxr - the line box's horizontal extent, from the armed origin
; in:  nothing ([fpg_x0] valid)
; out: AX = box left x, CX = box right x (both inclusive)
; clobbers: AX, CX
; -----------------------------------------------------------------------------
fpg_boxr:
    mov ax, [fpg_x0]
    add ax, FPG_ICO_W + FPG_ISP
    mov cx, ax
    add cx, FPG_BOX_W - 1
    ret

; --- state (SPEC.md 12.8: .text, because -f bin zeroes no .bss and the very
;     first int 13h of the machine calls fpg_step) ----------------------------
fpg_on      db 0                ; armed? tested by fpg_step on every sector
fpg_x0      dw 0                ; the widget's left edge, from [vid_clk_hx]
fpg_done    dw 0                ; sectors stepped so far
fpg_total   dw 1                ; ...out of this many (never 0: it divides)
fpg_px      dw 0                ; pixels of trough currently lit

; --- the document icon (icons.inc's record: db wwords, height, then height
;     mask words and height data words, bit 15 = leftmost). A page with a
;     folded top-right corner and four text rules. Pure black on white with
;     no dither anywhere, so it survives the 1bpp reduction intact
;     (SPEC.md 39.4) - a dithered outline would lose half of itself. --------
fpg_ico:
    db 1, FPG_ICO_H
    dw 0xFF80, 0xFFC0, 0xFFE0, 0xFFF0   ; mask: the silhouette, and the fold
    dw 0xFFF0, 0xFFF0, 0xFFF0, 0xFFF0   ; corner cut away from the top right
    dw 0xFFF0, 0xFFF0, 0xFFF0, 0xFFF0
    dw 0xFFF0, 0xFFF0
    dw 0xFF80, 0x80C0, 0x80A0, 0x80F0   ; data: top edge, then the fold's left
    dw 0x8010, 0xBE10, 0x8010, 0xBFD0   ; edge and diagonal, then the page
    dw 0x8010, 0xBFD0, 0x8010, 0xBE10   ; sides with four rules of "text"
    dw 0x8010, 0xFFF0
