; =============================================================================
; os8088 - desk.inc (SPEC.md 26)
;
; Desktop drive icons: detect the floppy drives at boot (int 11h), paint a
; 32x32 disk icon + "Disk A"/"Disk B" label per drive on the desktop, track
; selection, and open the Disk window on double-click via files_open_drive.
;
; Layout, per drive i (0 = A, 1 = B):
;   hit zone   x zx..zx+47, y (32+60*i)..(75+60*i)   (48 x 44 px), where
;              zx = [vid_desk_zx] = screen width - 56 (584 at 640 wide)
;   icon       ico_disk32 at (592, 32+60*i)          (32 x 32 px)
;   label      8px text at (584, 66+60*i), black on a white gap 2px around
;              the text, centered in the 48px zone (the 6-char label is
;              exactly 48px wide, so the white rect overhangs the zone by
;              2px each side: x 582..633, y (64+60*i)..(75+60*i))
;
; desk_paint runs under the caller's gfx lock (from wm_paint_all, right
; after the desktop fill, before any window). desk_click runs with NO lock
; held and takes gfx_lock/gfx_unlock itself around the zones it redraws.
; A deselected zone is repainted with gfx_fill_gray first - its dither is
; pixel-parity based, so it aligns with the surrounding desktop.
; =============================================================================

; The drive column hangs off the RIGHT edge of the screen, so its left
; column is derived at boot rather than pinned: [vid_desk_zx] = width - 56,
; which is 584 on both 640-wide adapters - exactly where it has always been -
; and 664 on a 720-wide Hercules, where a fixed 584 would leave the icons
; stranded 88px shy of the edge. Everything below derives from that ONE word,
; including the redraw-overlap test, which used to open-code 582 and 634.
DESK_ZW     equ 48              ; hit zone width (zx .. zx+47)
DESK_ZOVER  equ 2               ; label overhang each side
DESK_ZY0    equ 32              ; first zone top
DESK_ZSTEP  equ 60              ; zone-to-zone vertical pitch
DESK_ZH     equ 44              ; zone height (top..top+43)
DESK_COLW   equ 56              ; column-to-column pitch, leftwards
DESK_DBLT   equ 9               ; double-click window, ticks

; The column wrap (SPEC.md 26.1). Zones run DOWN a column and then start a
; NEW COLUMN TO THE LEFT, because on CGA they do not all fit: the strip is
; 640x200 there, the dock's top row is [vid_h]-24 = 176, and zone 2 would run
; 152..195 - straight through it. [desk_rows] is how many fit above the dock
; and is computed at boot from the live geometry, never from a constant, so
; VGA (7) and Hercules (4) keep every volume in one column and CGA (2) puts
; the hard disk beside the floppies instead of under them.

; -----------------------------------------------------------------------------
; desk_init - count floppy drives from the int 11h equipment word: if bit 0
;             is set, drives = ((AX >> 6) & 3) + 1, else 0; clamp to 2.
;             Initializes all module state (.bss is not auto-zeroed).
; ins:  none
; outs: none (all registers preserved)

section .ovl                    ; the boot overlay (SPEC.md 2.5): counting the
                                ; volumes and laying their zones out happens
                                ; once, before the first paint. desk_ord and
                                ; desk_zone_label below are called by the
                                ; RUNTIME zone painters and stay resident
; -----------------------------------------------------------------------------
desk_init:
    push ax
    push bx
    push cx
    push dx
    int 0x11                    ; AX = BIOS equipment word
    test al, 0x01               ; bit 0: any floppy drives at all?
    jz .none
    mov cl, 6
    shr ax, cl
    and ax, 3                   ; 0..3 = 1..4 drives
    inc ax
    cmp al, 2
    jbe .store
    mov al, 2                   ; clamp: the desktop shows at most A: and B:
.store:
    mov ah, al
    jmp .flags
.none:
    xor ah, ah
.flags:
    ; A floppy the machine does not have gets no zone - but its VOLUME ROW
    ; stays live, because [disk_drive] may still name it and a mount of an
    ; absent drive is an ordinary failed mount (SPEC.md 18.3).
    xor cl, cl
.zloop:
    mov bl, cl
    call KERNEL_SEG:ovw_dsk_vol_slot  ; FAR: overlay -> resident
    mov byte [bx+DV_FLAGS], 0
    cmp cl, ah
    jae .zskip
    mov byte [bx+DV_FLAGS], 1
.zskip:
    inc cl
    cmp cl, 2
    jb .zloop

    call KERNEL_SEG:ovw_desk_rowcalc  ; FAR: overlay -> resident. The row
                                ; count is geometry, and geometry can change
                                ; after boot now (SPEC.md 39.11.2), so the
                                ; arithmetic lives in .text where vid_switch
                                ; can reach it too

    mov byte [desk_sel], 0xFF   ; no selection
    mov word [desk_clkt], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_ord - a volume's ORDINAL among the zones that are shown
; in:  AL = volume index
; out: CF = 0 and AH = the ordinal; CF = 1 = this volume has no zone
; clobbers: AH (the output), flags
;
; The zone list IS the volume table (SPEC.md 18.7/26), so there is no second
; array to keep in step - which is what makes a driver's C: appear by setting
; one flag and disappear by clearing it.

section .text

; -----------------------------------------------------------------------------
; desk_rowcalc - how many drive zones fit in one column (SPEC.md 26.1)
;
; in:       [vid_dock_y0]
; out:      [desk_rows]
; clobbers: flags
;
; 7 on VGA, 4 on Hercules, 2 on CGA - and at least one, always: a screen with
; no room for a single drive icon is not one this OS boots. It was inline in
; desk_init, which is boot-overlay code and therefore dead by the time the
; Control Panel can change the adapter under it (SPEC.md 39.11.2), so it is
; resident now and desk_init far-calls it like any other overlay->text step.
; -----------------------------------------------------------------------------
desk_rowcalc:
    push ax
    push cx
    push dx
    mov ax, [vid_dock_y0]
    sub ax, DESK_ZY0
    xor dx, dx
    mov cx, DESK_ZSTEP
    div cx
    or ax, ax
    jnz .rows
    mov ax, 1
.rows:
    mov [desk_rows], ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
desk_ord:
    push bx
    push cx
    xor ah, ah                  ; AH = the ordinal being counted
    xor cl, cl
.next:
    cmp cl, DVOL_MAX
    jae .no
    mov bl, cl
    call dsk_vol_slot
    test byte [bx+DV_FLAGS], 1
    jz .skip
    cmp cl, al
    je .hit
    inc ah
.skip:
    inc cl
    jmp short .next
.hit:
    pop cx
    pop bx
    clc
    ret
.no:
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; desk_zone_label - one volume's desktop caption
; in:  AL = volume index
; out: SI -> a NUL string in the kernel segment
; clobbers: SI (the output), flags
;
; The row's own inline label if it has one, else 'Disk X' derived from the
; index - which is what keeps a driver free to call its volume 'HDD C' while
; a floppy needs no label stored at all.
; -----------------------------------------------------------------------------
desk_zone_label:
    push ax
    push bx
    mov bl, al
    call dsk_vol_slot
    lea si, [bx+DV_LBL]
    cmp byte [si], 0
    jne .out                    ; the row named itself
    mov si, desk_lbl_gen        ; 'Disk ' or 'HDD ' + the letter, built in
    mov word [desk_lbl_gen+0], 'Di'     ; place. The PREFIX comes from the
    mov word [desk_lbl_gen+2], 'sk'     ; kind, so a driver that mounts four
    mov byte [desk_lbl_gen+4], ' '      ; partitions gets HDD C..HDD F for
    mov cx, 5                           ; nothing - it could not have named
    cmp byte [bx+DV_KIND], DVK_DRV      ; them itself anyway, since the
    jne .letter                         ; letter is the kernel's to assign
    mov word [desk_lbl_gen+0], 'HD'
    mov word [desk_lbl_gen+2], 'D '
    mov cx, 4
.letter:
    mov bx, cx
    add al, 'A'
    mov [bx+desk_lbl_gen], al
    mov byte [bx+desk_lbl_gen+1], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_paint - draw every drive's icon + label; the selected zone (desk_sel)
;              gets an XOR highlight. Called by wm_paint_all after the
;              desktop fill; caller holds the gfx lock.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_paint:
    push ax
    push bx
    mov al, 0
.next:
    cmp al, DVOL_MAX
    jae .done
    mov bl, al
    call dsk_vol_slot
    test byte [bx+DV_FLAGS], 1
    jz .skip
    call desk_draw_zone         ; AL = volume index
.skip:
    inc al
    jmp .next
.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zone_rect - one drive zone's DRAWN rect (not its hit zone)
; in:  AL = zone index
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; clobbers: the four outputs (AH included)
;
; The drawn rect is wider than the hit zone: the label's white box overhangs
; DESK_ZOVER px each side, which is what [vid_desk_zl]/[vid_desk_zr] carry
; (SPEC.md 26/39.2). desk_zone_redraw's own overlap test uses the same two.
; -----------------------------------------------------------------------------
desk_zone_rect:
    call desk_zone_xy           ; AX = zone left, DX = zone top
    mov bx, dx
    add dx, DESK_ZH-1           ; y2 = top + 43
    mov cx, ax
    add cx, DESK_ZW + DESK_ZOVER - 1    ; x2, label overhang included
    sub ax, DESK_ZOVER                  ; x1, likewise
    ret

; -----------------------------------------------------------------------------
; desk_zone_xy - one volume's zone origin (SPEC.md 26.1)
; in:  AL = volume index
; out: AX = the HIT zone's left column, DX = its top row
; clobbers: AX, DX (the outputs), flags
;
; Zones fill a column downwards and then start a new one to the LEFT. The
; row count is [desk_rows], measured at boot against the live dock position -
; so this is where CGA's third volume stops landing on the dock.
; -----------------------------------------------------------------------------
desk_zone_xy:
    push bx
    push cx
    call desk_ord               ; AH = the ordinal, CF=1 = not shown
    jc .off
    mov al, ah
    pop cx
    pop bx
    jmp desk_ord_xy             ; tail call, AL = the ordinal
.off:
    mov ax, [vid_desk_zx]       ; a hidden volume still needs a rect that is
    mov dx, -1000               ; nowhere: every caller draws or hit-tests it
    pop cx                      ; only after its own flag test, and this is
    pop bx                      ; the belt to that pair of braces
    ret

; -----------------------------------------------------------------------------
; desk_ord_xy - where the Nth SHOWN zone sits (SPEC.md 26.1)
; in:  AL = an ordinal, 0..DVOL_MAX-1
; out: AX = the hit zone's left column, DX = its top row
; clobbers: AX, DX (the outputs), flags
;
; Split out of desk_zone_xy so the grid's extent (desk_zones_dmg) can be asked
; for by ordinal without a volume to hang it on - the geometry stays in one
; place, which is what the two callers of the old combined routine relied on.
; -----------------------------------------------------------------------------
desk_ord_xy:
    push bx
    push cx
    xor ah, ah
    xor dx, dx
    div word [desk_rows]        ; AX = column, DX = row within it
    mov bx, dx                  ; BX = the row - and it has to leave DX
    mov cx, DESK_COLW           ; NOW, because both multiplies below write
    mul cx                      ; there and the second one would eat it
    mov cx, [vid_desk_zx]
    sub cx, ax                  ; CX = the column's left, leftwards from the
                                ; drive column (SPEC.md 26.1)
    mov ax, bx
    mov bx, cx                  ; ...banked again, for the same reason
    mov cx, DESK_ZSTEP
    mul cx
    add ax, DESK_ZY0
    mov dx, ax                  ; DX = the top row
    mov ax, bx                  ; AX = the left column
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; desk_zmark - a volume gained or lost its zone: post the repaint (SPEC.md 26.3)
; in:  the zone table already reflects an ADD, and does NOT yet reflect a
;      DELETE - either way the changing zone is still COUNTED
; out: nothing (all registers preserved)
;
; The two callers are dsk_vol_add and dsk_vol_del, and the asymmetry in when
; they call is the whole point. An add renumbers ordinals below the new count;
; a delete leaves stale pixels at what was the last ordinal. Counting with the
; changing zone included covers both, and taking the MAX covers a drain that
; coalesces several changes - unmounting a three-partition disk drops three
; volumes before ui_task looks. desk_zones_paint spends the mark and clears it.
; -----------------------------------------------------------------------------
desk_zmark:
    push ax
    call desk_zones_shown
    cmp al, [desk_zhw]
    jbe .post
    mov [desk_zhw], al
.post:
    mov byte [desk_zdirty], 1
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zones_shown - how many volumes have a zone right now (module internal)
; out: AX = the count, 0..DVOL_MAX
; clobbers: AX (the output), flags
;
; desk_ord IS this walk - it counts the shown zones ahead of the volume it was
; asked about - so asking it for an index no row can have runs the count to the
; end and reports it in AH. One definition of "which zones are shown, and in
; what order", which is the property the damage rect has to agree with.
; -----------------------------------------------------------------------------
desk_zones_shown:
    mov al, 0xFF                ; matches no volume, so the walk never stops
    call desk_ord               ; early: CF = 1 and AH = every shown zone
    mov al, ah
    xor ah, ah
    ret

; -----------------------------------------------------------------------------
; desk_zones_dmg - the rect the zones that MOVED could occupy (SPEC.md 26.3)
; out: AX/BX/CX/DX = x1/y1/x2/y2, inclusive, for wm_paint_dmg
; clobbers: the four outputs, flags
;
; NOT the whole grid, and that distinction is worth a paragraph because the
; grid is what this used to be. Sizing to DVOL_MAX made the answer independent
; of what is mounted, which is a real property - but the grid is six zones and
; a machine usually shows three, and the unused ones are not slack at the edge:
; columns fill downwards and wrap LEFT, so on a geometry with few rows the
; phantom zones are whole COLUMNS of desktop reaching back across the screen.
; On CGA (desk_rows = 2) three mounted volumes occupy x 526..633 and the grid
; claimed x 470..633 - 56 pixels of empty desktop that reached into the
; Control Panel window the Mount button lives in, so wm_paint_dmg's marking
; pass redrew that window WHOLE, W_PAINT and all, on every mount.
;
; What actually has to be covered is ordinals 0..n-1 where n is the largest
; number of zones the screen may currently be showing - [desk_zhw], the
; high-water mark desk_zmark keeps. An ADD renumbers only ordinals below the
; new count and a DELETE leaves stale pixels at the old last ordinal, so the
; mark is taken with the changing zone COUNTED in both cases: after the row
; goes live on an add, before its flag is cleared on a delete. That also
; survives coalescing, which "the current count plus one" does not - unmounting
; a three-partition disk drops three volumes into one drain.
;
; A mark of 0 means no change is pending, and the live count is the answer -
; which is what osapi_vol_paint needs, a driver being free to call it with
; nothing owed. There is no floor to apply when the mark IS set: an add stamps
; exactly the live count and a delete stamps one more, so a set mark is never
; below it. The count is floored at 1, so ordinal 0 is always a legal argument
; to desk_ord_xy.
;
; Ordinal 0 is the top of the rightmost column and ordinal n-1 the bottom of
; the leftmost, because columns fill downwards and then wrap LEFT - so the two
; corners are the two extremes, and desk_ord_xy is asked for both rather than
; the arithmetic being written out a second time.
; -----------------------------------------------------------------------------
desk_zones_dmg:
    push si
    push di
    push bp
    mov al, [desk_zhw]          ; BP = the number of zones to cover. A set mark
    xor ah, ah                  ; is never below the live count - an add stamps
    or ax, ax                   ; exactly it and a delete stamps one more - so
    jnz .have                   ; there is nothing to floor it against, and 0
    call desk_zones_shown       ; means no pending change at all: osapi_vol_paint
.have:                          ; asking for the zones that exist
    or ax, ax
    jnz .n
    mov ax, 1                   ; ordinal 0 is always a legal argument
.n:
    mov bp, ax
    xor al, al
    call desk_ord_xy            ; ordinal 0 = the top of the RIGHTMOST column
    mov si, ax                  ; SI = its left edge...
    mov di, dx                  ; DI = ...and its top
    mov ax, bp
    dec ax                      ; the last ordinal in use
    xor dx, dx
    div word [desk_rows]        ; AX = the LEFTMOST column's index...
    mul word [desk_rows]        ; ...as the ordinal at the TOP of it
    mov cx, bp                  ; the deepest row any column reaches - NOT the
    cmp cx, [desk_rows]         ; last ordinal's row, which on a geometry
    jbe .deep                   ; whose final column is short (Hercules: four
    mov cx, [desk_rows]         ; rows, two columns, six volumes) sits above
.deep:                          ; the bottom of the first column and would cut
    dec cx                      ; the rect short of two whole zones
    add ax, cx                  ; AX = the bottom-left corner's ordinal, one
                                ; row past what a short last column uses -
                                ; slack, and in the safe direction
    call desk_ord_xy
    pop bp
    mov cx, si
    add cx, DESK_ZW + DESK_ZOVER - 1    ; x2, label overhang included
    sub ax, DESK_ZOVER                  ; x1, likewise
    mov bx, di                          ; y1
    add dx, DESK_ZH-1                   ; y2
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; desk_dmg_zones - which drive zones does the damage rect touch?
; in:  [wm_dmg_*] (wm.inc); caller holds the gfx lock
; out: AL = bit n set = zone n is in the damage; the damage rect is GROWN to
;      cover every one of them. AH clobbered, everything else preserved.
;
; A zone is redrawn whole - gray fill, icon, label, highlight - so growing the
; rect to it is not slack: it is what makes wm_paint_dmg's marking pass notice
; a window sitting over that zone and redraw it too.
; -----------------------------------------------------------------------------
desk_dmg_zones:
    push bx
    push cx
    push dx
    mov byte [desk_dmask], 0
    mov byte [desk_dzi], 0      ; the loop index lives in .bss because
                                ; desk_zone_rect writes all four GP registers
.zone:
    mov al, [desk_dzi]
    cmp al, DVOL_MAX
    jae .done
    push bx
    mov bl, al
    call dsk_vol_slot
    test byte [bx+DV_FLAGS], 1
    pop bx
    jz .next
    call desk_zone_rect
    call wm_dmg_hit             ; preserves the rect
    jnc .next
    call wm_dmg_add
    mov cl, [desk_dzi]
    mov al, 1
    shl al, cl                  ; 8086: a variable shift goes through CL
    or [desk_dmask], al
.next:
    inc byte [desk_dzi]
    jmp .zone
.done:
    mov al, [desk_dmask]
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; desk_zones_paint - repaint the drive zones and what they disturb (26.2)
; in:  the gfx lock HELD
; out: nothing (all registers preserved)
;
; What a mount or an unmount actually owes the screen. It used to be
; wm_paint_all - the whole desktop dither, every visible window's frame and
; every W_PAINT - to make one icon appear at the right-hand edge.
;
; wm_paint_dmg (SPEC.md 11.91) is the right size of hammer: the dither
; clipped to the zones that moved, the zones themselves, the chrome, and only
; the windows that overlap. On a desktop with nothing near the right edge that
; is the zones and nothing else.
;
; The high-water mark is spent HERE and nowhere else: once these pixels are
; back, the screen shows exactly the zones that exist, so the next change
; starts counting again from what it can see.
; -----------------------------------------------------------------------------
desk_zones_paint:
    push ax
    push bx
    push cx
    push dx
    call desk_zones_dmg
    mov byte [desk_zhw], 0
    call wm_paint_dmg
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_paint_mask - draw the drive zones named by a bitmask
; in:  AL = bit n set = draw zone n; caller holds the gfx lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
desk_paint_mask:
    push ax
    push bx
    push cx
    mov bl, al                  ; BL = the mask for the duration
    xor cl, cl
.next:
    cmp cl, DVOL_MAX
    jae .done
    mov al, bl
    shr al, cl
    test al, 1
    jz .skip
    mov al, cl
    call desk_draw_zone         ; preserves every register
.skip:
    inc cl
    jmp .next
.done:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_click - desktop hit test, called by ui.inc when wm_hit found no
;              window under an EVT_MDOWN. NO lock held on entry; selection
;              flips redraw only the affected zones under an internally
;              acquired gfx_lock/gfx_unlock.
;
;              Zone hit: same zone as desk_sel and click-tick delta < 9
;              -> double-click: clear the selection (redraw under lock,
;              release), then files_open_drive AL=drive (locks internally):
;              it fronts a window already showing that drive's root, or
;              opens one, or at the cap moves the front one (SPEC.md 22.1).
;              Else select it, stamp desk_clkt. Miss: clear any selection.
; ins:  CX = x, DX = y (absolute screen px)
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, cx                  ; DI/SI = the click, and CL = the volume:
    mov si, dx                  ; desk_zone_xy ANSWERS in AX and DX, so the
    xor cl, cl                  ; index cannot live in either
.zone:
    cmp cl, DVOL_MAX
    jae .miss
    mov bl, cl
    call dsk_vol_slot
    test byte [bx+DV_FLAGS], 1
    jz .zstep
    mov al, cl
    call desk_zone_xy           ; AX = zone left, DX = zone top
    cmp di, ax
    jb .zstep
    mov bx, ax
    add bx, DESK_ZW-1
    cmp di, bx
    ja .zstep
    cmp si, dx
    jb .zstep
    mov bx, dx
    add bx, DESK_ZH-1
    cmp si, bx
    jbe .hit
.zstep:
    inc cl
    jmp .zone

.hit:
    mov al, cl                  ; AL = the volume index
    cmp al, [desk_sel]
    jne .select
    mov bx, [ui_click_t]        ; same zone: double-click? Compare click
    sub bx, [desk_clkt]         ; birth ticks (EV_C), wrap-safe - clicks
                                ; queued behind a slow mount must not
                                ; collapse into a double-click
    cmp bx, DESK_DBLT
    jae .restamp
    ; --- double-click: deselect + redraw under our lock, then open ----------
    call gfx_lock               ; the store is INSIDE the lock, here and in
    mov byte [desk_sel], 0xFF   ; both paths below: desk_zone_redraw's partial
    call desk_zone_redraw       ; case flips the highlight rather than redrawing
    call gfx_unlock             ; it, so no other painter may see the new
                                ; selection and act on it in between
    call files_open_drive       ; AL = drive; must be called with NO lock
    jmp .out                    ; held - it locks internally (SPEC 22)

.restamp:                       ; same zone, too slow: restart the timer
    mov bx, [ui_click_t]
    mov [desk_clkt], bx
    jmp .out

.select:                        ; new selection: redraw old + new zones
    mov bx, [ui_click_t]
    mov [desk_clkt], bx
    call gfx_lock
    mov ah, [desk_sel]          ; AH = old zone (0xFF = none)
    mov [desk_sel], al
    cmp ah, 0xFF
    je .selnew
    xchg al, ah                 ; deselect redraw of the old zone
    call desk_zone_redraw
    xchg al, ah
.selnew:
    call desk_zone_redraw       ; selected redraw of the new zone
    call gfx_unlock
    jmp .out

.miss:
    mov al, [desk_sel]          ; empty desktop click clears the selection
    cmp al, 0xFF
    je .out
    call gfx_lock
    mov byte [desk_sel], 0xFF
    call desk_zone_redraw       ; AL = the old zone
    call gfx_unlock

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zone_redraw - flip one zone's SELECTION outside wm_paint_all
; in:  AL = zone index, whose selection state has just changed; caller holds
;      the gfx lock
; out: nothing; preserves all registers
;
; The zone's drawn rect (the gray refill spans x [vid_desk_zl]..[vid_desk_zr]
; - the label overhangs the hit zone) may be overlapped by a visible window,
; and the gfx_* primitives clip to the SCREEN, so a bare desk_draw_zone would
; paint desktop pixels straight over that window. This used to answer that
; with a full wm_paint_all, which cost the whole screen twice for one click:
; picking a new icon redraws the old zone and the new one, and each of them
; repainted the world. wm_clip_rect (SPEC.md 11.3) is the region arithmetic
; that replaces it, and there are four answers (SPEC.md 26.3):
;
;   overflow    - more than 16 fragments; nothing about the region is known,
;                 so the old full repaint stays as the last resort
;   covered     - not one pixel of the zone is on screen: draw NOTHING
;   clear air   - the whole drawn rect is inside one fragment: the cheap
;                 unclipped single-zone redraw, exactly as before
;   partial     - draw only what actually changed, which is the highlight:
;                 gfx_xor_fill clips per fragment, so it lands on the visible
;                 part of the icon and nowhere else. Everything else in the
;                 zone - the gray, the icon, the label - is already on screen
;                 and identical either way, and redrawing it clipped would
;                 lose the icon anyway (ico_core clips whole-shape).
;
; That last case is why this is a FLIP and not a repaint, and why the caller
; must have changed [desk_sel] under the same lock hold: XOR is self-inverse,
; so it is exact only against pixels that still carry the previous state.
; -----------------------------------------------------------------------------
desk_zone_redraw:
    push ax
    push bx
    push cx
    push dx

    push ax                     ; keep the volume index for the draw call
    call desk_zone_rect         ; AX/BX = x1/y1, CX/DX = x2/y2 (inclusive)
    call wm_clip_rect           ; the zone less every visible window; the rect
                                ; survives in AX/BX/CX/DX for the test below
    jc .full                    ; too many fragments: repaint the world
    jz .none                    ; wholly covered: nothing of it is on screen
    call wm_clip_test           ; is the WHOLE zone inside one fragment?
    jc .part
    call wm_clip_clear          ; yes - clear air, so the region has no work
    pop ax                      ; to do and the icon draws whole
    call desk_draw_zone
    jmp short .done
.part:
    pop ax                      ; cut by a window: flip the visible part of
    call desk_zone_hilite       ; the highlight and touch nothing else
    jmp short .done
.full:
    pop ax
    call wm_paint_all
    jmp short .done
.none:
    pop ax                      ; the list is already empty, i.e. disarmed
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zone_hilite - XOR one zone's hit rect: the selection highlight, and
;                    the ONLY thing that differs between a selected zone and
;                    an unselected one. Self-inverse, so it both applies and
;                    removes. Internal.
; ins:  AL = volume index; caller holds the gfx lock (a clip region may be
;       armed - gfx_xor_fill clips per fragment)
; outs: none; clobbers AX, BX, CX, DX - both call sites have them pushed and
;       dead, and this is the last thing either does
; -----------------------------------------------------------------------------
desk_zone_hilite:
    call desk_zone_xy           ; AX = zone left, DX = zone top
    mov bx, dx
    mov cx, ax
    add cx, DESK_ZW-1
    add dx, DESK_ZH-1
    jmp gfx_xor_fill            ; tail call: it preserves everything and this
                                ; frame has nothing left to restore

; -----------------------------------------------------------------------------
; desk_draw_zone - repaint one drive zone from scratch: gray desktop fill
;                  (parity dither aligns with the surroundings), disk icon,
;                  label, and the XOR highlight when the zone is selected.
;                  Used by desk_paint and desk_click. Internal.
; ins:  AL = drive index (0/1); caller holds the gfx lock
; outs: none (all registers preserved; [gfx_color] restored)
; -----------------------------------------------------------------------------
desk_draw_zone:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ah, [gfx_color]
    mov [desk_gcol], ah         ; byte only - gfx_lock_flag lives next door

    mov ah, 0
    mov [desk_zdrv], ax         ; the volume index
    call desk_zone_xy           ; AX = zone left, DX = zone top
    mov [desk_zx], ax
    mov [desk_zy], dx

    ; --- gray desktop under the zone + the label's 2px side overhang --------
    mov bx, dx                  ; y1 = top
    add dx, DESK_ZH-1           ; y2 = top+43
    sub ax, DESK_ZOVER
    mov cx, ax
    add cx, DESK_ZW + 2*DESK_ZOVER - 1
    call gfx_fill_gray

    ; --- 32x32 icon, 8px in from the zone's left -----------------------------
    mov cx, [desk_zx]
    add cx, 8
    mov dx, [desk_zy]
    mov si, ico_disk32
    mov ax, [desk_zdrv]
    mov bl, al
    call dsk_vol_slot
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .icon
    mov si, ico_hdd32           ; a driver-backed volume is a HARD DISK to
.icon:                          ; look at, not a floppy (SPEC.md 26)
    call icon_draw

    ; --- label: white rect, rows top+32..top+43 ------------------------------
    mov bx, [desk_zy]
    add bx, 32                  ; y1: just under the icon
    mov dx, bx
    add dx, 11                  ; y2: 12 rows = 2px gap + 8px text + 2px gap
    mov ax, [desk_zx]
    sub ax, DESK_ZOVER
    mov cx, ax
    add cx, DESK_ZW + 2*DESK_ZOVER - 1
    mov byte [gfx_color], CWHITE
    call gfx_fill

    ; --- the caption, black, centred in the 48px zone ------------------------
    mov byte [gfx_color], CBLACK
    mov ax, [desk_zdrv]
    call desk_zone_label        ; SI -> it, the row's or a derived 'Disk X'
    push si
    call font_width             ; AX = its pixel width
    mov cx, [desk_zx]
    add cx, DESK_ZW/2
    shr ax, 1
    sub cx, ax
    pop si
    mov dx, [desk_zy]
    add dx, 34                  ; text rows top+34..top+41
    call font_str

    ; --- selection highlight: XOR the whole hit zone --------------------------
    mov al, [desk_sel]
    cmp al, byte [desk_zdrv]
    jne .done
    mov ax, [desk_zdrv]         ; one place owns the highlight's geometry, so
    call desk_zone_hilite       ; the full repaint and the partial flip cannot
.done:                          ; disagree about which pixels it is
    mov al, [desk_gcol]
    mov [gfx_color], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

desk_lbl_gen db "Disk X", 0, 0  ; scratch for desk_zone_label's derived form:
                                ; a volume with no label of its own is named
                                ; from its KIND and its index, in place

; -----------------------------------------------------------------------------
section .bss
desk_rows    resw 1             ; zones that fit in one column above the dock
                                ; (SPEC.md 26.1) - 7 on VGA, 4 on Hercules,
                                ; 2 on CGA, which is why the wrap exists
desk_zdirty  resb 1             ; 1 = a volume was added or removed and the
                                ; drive zones owe a repaint (SPEC.md 26.3)
desk_zhw     resb 1             ; ...and the largest number of zones the screen
                                ; may be showing while that debt stands, which
                                ; is what sizes the damage rect. desk_zmark
                                ; raises it, desk_zones_paint spends it. 0 is
                                ; "no pending change", and .bss arrives zeroed
desk_sel     resb 1             ; selected VOLUME index, 0xFF = none
desk_clkt    resw 1             ; click birth tick (EV_C) of the last zone click
desk_zy      resw 1             ; desk_draw_zone: current zone top
desk_zx      resw 1             ; ...and its left column (SPEC.md 26.1)
desk_zdrv    resw 1             ; desk_draw_zone: current volume index
desk_gcol    resb 1             ; desk_draw_zone: saved [gfx_color]
desk_dmask   resb 1             ; desk_dmg_zones: the mask under construction
desk_dzi     resb 1             ; desk_dmg_zones: its loop index
section .text
