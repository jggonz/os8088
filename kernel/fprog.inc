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
; ~24 ms per 512 bytes on the field machine (PERFORMANCE.md Part 2, Set 24 -
; 65 at Set 17, 238 before SPEC.md 18.91's AL fix), which for a
; 116KB module is several seconds of a screen that looks hung. What was
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
FPG_GAP    equ 0                ; box -> clock cell gap. It was 8, and the
                                ; whole cell it cost is worth more to the
                                ; TOAST than to the eye (SPEC.md 59.9.2): the
                                ; widget's reservation is FPG_W / 8 cells off
                                ; the gap a message has to fit in, and the
                                ; clock's own field is right-aligned with
                                ; seven or so blank cells at its left end, so
                                ; the separation the eye sees is unchanged
                                ; while the strip gets a cell back
FPG_W      equ FPG_ICO_W + FPG_ISP + FPG_BOX_W + FPG_GAP    ; 80
FPG_BAR_PX equ FPG_BOX_W - 2    ; 62: the trough, in pixels
FPG_MINX   equ MENU_SYS_XR + 8  ; an origin left of this would collide with
                                ; the System menu: refuse the whole feature
FPG_UNIT   equ 512              ; the trough's internal unit, in bytes: a
FPG_USHR   equ 9                ; SECTOR, because fpg_step's caller is the
                                ; per-sector loop in dsk_xfer and that is the
                                ; one report this widget gets for free. It is
                                ; NOT part of the interface - fpg_begin and
                                ; fpg_stepb both speak bytes (SPEC.md 12.8.1)

; -----------------------------------------------------------------------------
; fpg_begin - arm the widget and draw everything expensive about it
; in:  DX:CX = BYTES this operation expects (0 refuses: nothing to report);
;      DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved
;
; **The scale is BYTES, and the trough's units are the widget's own business**
; (SPEC.md 12.8.1). It took a SECTOR count until the file redirector arrived
; and had none to give: a DRVC_FILE volume has no sectors at all (SPEC.md
; 62.9), FSV_STAT answers a size in DX:CX, and a cable moves bytes. Every
; producer already holds a byte count - dskw_size32 divides one to get
; [dskw_rem] - so asking for the thing they have rather than the thing this
; module used to want costs nothing and makes the widget say something on a
; volume that has no geometry. DX:CX and not DX:AX because that is the file
; API's own 32-bit count convention (SPEC.md 18.4.1) and FSV_STAT's answer
; register pair, so the two arming sites that matter most pass it unmoved.
;
; Internally it is still 512-byte units, rounded UP, saturating at 65,535 -
; a 32MB job (the volume cap, SPEC.md 18.7) lands one unit short of exact and
; the trough is 62 pixels wide, so nothing about that is visible.
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

    mov ax, cx                  ; --- DX:CX bytes -> a 16-bit unit count
    add ax, FPG_UNIT - 1        ; round UP: a 1-byte job is one unit, not none
    adc dx, 0
    mov cx, FPG_USHR
.shr:
    shr dx, 1                   ; DX:AX >>= 9 (dskw_size32's idiom - the 8086
    rcr ax, 1                   ; has no 32-bit shift)
    loop .shr
    or dx, dx
    jz .fits
    mov ax, 0xFFFF              ; past 32MB of units: saturate rather than wrap
.fits:
    or ax, ax                   ; a 0-byte job has no progress to report and
    jz .out                     ; would be fpg_step's divisor: refuse it whole
    mov [fpg_total], ax
    mov word [fpg_done], 0
    mov word [fpg_px], 0
    mov word [fpg_frac], FPG_UNIT - 1
                                ; SEEDED, not zeroed, and that one word is what
                                ; makes fpg_stepb round the way the total above
                                ; just did: a running `(FPG_UNIT-1 + bytes) /
                                ; FPG_UNIT` IS ceil(bytes / FPG_UNIT) at every
                                ; point, so a 167-byte file reports one unit
                                ; out of the one it was armed for instead of
                                ; zero out of one. Zeroed, every file under
                                ; 512 bytes left the bar at 0% for the whole
                                ; transfer and no arithmetic anywhere was
                                ; wrong (SPEC.md 12.8.1). fpg_step never reads
                                ; it, so the sector path is untouched

    mov byte [fpg_on], 1        ; **ARMED BEFORE THE FIRST PIXEL, so that the
    mov byte [menu_bdirty], 1   ; TOAST CAN GET OUT OF THE WAY** (SPEC.md
    call menu_draw_bar          ; 59.9.2). A strip is placed where it fitted
                                ; when it went up, and SPEC.md 59.9's first
                                ; choice - the gap the menus leave - is the
                                ; same stretch of bar this widget is about to
                                ; be drawn on; measured on a 5150/CGA, Paint's
                                ; own `Saving...` lands on cells 43..53 and
                                ; the widget takes 43..53. Without this the
                                ; bed fill below simply destroys the message
                                ; and the operation runs with `Sa` and half a
                                ; document icon on the bar.
                                ;
                                ; Re-placing it costs one composition, which
                                ; is a compare per cell plus at most one
                                ; font_run, once per file operation - against
                                ; a disk this is not measurable. It is done
                                ; HERE and not by asking toast_kill to retire
                                ; the message, which is what this site used to
                                ; do: a refusal loses the message for good and
                                ; silently, and the messages worth saying are
                                ; exactly the ones about the operation that is
                                ; starting.
                                ;
                                ; [menu_bdirty] because the reservation
                                ; changed, not the text: nothing menu_bar_text
                                ; composes has moved, so the flag is the only
                                ; thing that can ask for the placement to be
                                ; taken again. menu_inval below re-sets it, so
                                ; fpg_end's own pass is not spent here

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
; fpg_step - one 512-byte unit done
; in:  nothing;  DS = KERNEL_SEG (every caller is kernel code)
; out: nothing - EVERY register and the flags preserved, because the hot
;      caller is the middle of dsk_xfer's per-sector loop, where CX is a loop
;      counter, AX/BX/SI/DI are live transfer state and CF is the routine's
;      return value in flight. spl_step's contract exactly, and for the same
;      reason.
;
; Unarmed this is a compare and a return, and it stays on the disk path for
; the life of the machine at that price (~13 us against a sector's ~24 ms).
; Armed, it draws only when the bar's pixel extent actually CHANGED - the
; trough is 62 pixels and an operation is usually more sectors than that, so
; most steps draw nothing at all.
; -----------------------------------------------------------------------------
fpg_step:
    pushf
    cmp byte [fpg_on], 0
    je .out
    push ax                     ; AX/BX/CX/DX/SI and no more: everything this
    push bx                     ; calls below - gfx_fill and fpg_boxr
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
; fpg_stepb - N BYTES done (SPEC.md 12.8.1)
; in:  AX = bytes moved since the last report;  DS = KERNEL_SEG, gfx lock held
; out: nothing - every register and the flags preserved, fpg_step's contract
;
; The entry point for a producer that moves bytes rather than sectors, and it
; IS slot 0x03C8 (OSAPI_FS_PROG) - an ordinary OSAPI_SLOT cell, so a
; DRVC_FILE driver inside FSV_READ/FSV_WRITE calls exactly this routine. That
; is the half fpg_begin cannot cover: the kernel arms the widget from
; FSV_STAT's size and then has NOTHING to say until the verb returns, because
; a redirected read is one far call and not a loop the kernel is standing in.
; Only the driver knows how far the file has got.
;
; A short count is the normal case and not an error - a wire delivers what it
; delivers - so this accumulates, and the remainder below one unit is carried
; in [fpg_frac] rather than rounded away. Report the SAME bytes twice and the
; bar advances twice, which is why the contract is "since the last report"
; and not "so far".
;
; Unarmed it is a compare and a return, so a driver may call it
; unconditionally, and one that never calls it at all is merely a driver whose
; bar sits at 0 - which is what every redirected read did before this existed.
; It draws, so it demands what every drawing path here demands: the gfx lock
; ALREADY held, true by construction inside a file operation (SPEC.md 18).
;
; It steps through fpg_step rather than repeating its arithmetic - the same
; discipline dskw_rdata/wdata's shared flush follows (SPEC.md 18.4.2), and
; for the same reason: two copies of "has the extent moved" is two answers
; that can drift. The loop runs at most 128 times per 64KB report and each
; turn is a compare that usually finds nothing to draw, which against the
; wire that made this necessary (3,741 B/s, PERFORMANCE.md Set 39) is not a
; cost anybody can measure.
; -----------------------------------------------------------------------------
fpg_stepb:
    pushf
    cmp byte [fpg_on], 0
    je .out
    push ax
    push cx
    push dx
    xor dx, dx                  ; DX:AX = the carried remainder + this report,
    add ax, [fpg_frac]          ; exact in 32 bits, so nothing is lost at a
    adc dx, 0                   ; chunk boundary
    mov cx, FPG_UNIT
    div cx                      ; AX = whole units (<= 256, so the quotient
    mov [fpg_frac], dx          ; cannot overflow), DX = the new remainder
    mov cx, ax
    jcxz .done
.each:
    call fpg_step
    loop .each
.done:
    pop dx
    pop cx
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

    mov byte [menu_bdirty], 1   ; **THE TEXT BAND IS OURS TO ASK FOR.** The
                                ; box's rows MENU_TEXT_Y..+7 are erased by
                                ; nothing here - the opaque run that rewrites
                                ; them is the erase (above) - and that run
                                ; only happens if the flag is set. fpg_begin's
                                ; menu_inval sets it, but anything composing
                                ; the bar in between SPENDS it, and then the
                                ; box frame stays on the glass for good. One
                                ; store makes the erase this routine's own
                                ; rather than a debt somebody else may settle
    call menu_draw_bar          ; the text band, as the ONE run that differs
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
fpg_done    dw 0                ; units stepped so far
fpg_total   dw 1                ; ...out of this many (never 0: it divides)
fpg_px      dw 0                ; pixels of trough currently lit
fpg_frac    dw 0                ; fpg_stepb's carry: bytes reported that have
                                ; not yet made a whole unit (SPEC.md 12.8.1)

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
