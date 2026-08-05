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
DESK_DBLT   equ 9               ; double-click window, ticks

; -----------------------------------------------------------------------------
; desk_init - count floppy drives from the int 11h equipment word: if bit 0
;             is set, drives = ((AX >> 6) & 3) + 1, else 0; clamp to 2.
;             Initializes all module state (.bss is not auto-zeroed).
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_init:
    push ax
    push cx
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
    mov [desk_ndrives], al
    jmp .init
.none:
    mov byte [desk_ndrives], 0
.init:
    mov byte [desk_sel], 0xFF   ; no selection
    mov word [desk_clkt], 0
    pop cx
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
    mov al, 0
.next:
    cmp al, [desk_ndrives]
    jae .done
    call desk_draw_zone         ; AL = drive index
    inc al
    jmp .next
.done:
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
    xor ah, ah
    mov bl, DESK_ZSTEP
    mul bl                      ; AX = zone * pitch (zone <= 1, so AH stays 0)
    add ax, DESK_ZY0
    mov bx, ax                  ; y1 = zone top
    mov dx, ax
    add dx, DESK_ZH-1           ; y2 = top + 43
    mov ax, [vid_desk_zl]       ; x1, label overhang included
    mov cx, [vid_desk_zr]
    dec cx                      ; x2
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
    cmp al, [desk_ndrives]
    jae .done
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
    cmp cl, [desk_ndrives]
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
    push si

    cmp cx, [vid_desk_zx]       ; horizontal band first
    jb .miss
    mov bx, [vid_desk_zx]
    add bx, DESK_ZW-1
    cmp cx, bx
    ja .miss

    mov bx, DESK_ZY0            ; walk the zones: BX = zone top
    mov al, 0                   ; AL = zone index
.zone:
    cmp al, [desk_ndrives]
    jae .miss
    cmp dx, bx
    jb .zstep
    mov si, bx
    add si, DESK_ZH-1
    cmp dx, si
    jbe .hit
.zstep:
    add bx, DESK_ZSTEP
    inc al
    jmp .zone

.hit:                           ; AL = zone/drive index
    cmp al, [desk_sel]
    jne .select
    mov bx, [ui_click_t]        ; same zone: double-click? Compare click
    sub bx, [desk_clkt]         ; birth ticks (EV_C), wrap-safe - clicks
                                ; queued behind a slow mount must not
                                ; collapse into a double-click
    cmp bx, DESK_DBLT
    jae .restamp
    ; --- double-click: deselect + redraw under our lock, then open ----------
    mov byte [desk_sel], 0xFF
    call gfx_lock
    call desk_zone_redraw       ; AL = zone (now unselected -> no XOR)
    call gfx_unlock
    call files_open_drive       ; AL = drive; must be called with NO lock
    jmp .out                    ; held - it locks internally (SPEC 22)

.restamp:                       ; same zone, too slow: restart the timer
    mov bx, [ui_click_t]
    mov [desk_clkt], bx
    jmp .out

.select:                        ; new selection: redraw old + new zones
    mov ah, [desk_sel]          ; AH = old zone (0xFF = none)
    mov [desk_sel], al
    mov bx, [ui_click_t]
    mov [desk_clkt], bx
    call gfx_lock
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
    mov byte [desk_sel], 0xFF
    call gfx_lock
    call desk_zone_redraw       ; AL = the old zone
    call gfx_unlock

.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zone_redraw - flip one zone OUTSIDE wm_paint_all. The zone's drawn
; rect (the gray refill spans x 582..633 - the label overhangs the hit
; zone) may be overlapped by a visible window; a bare desk_draw_zone would
; paint desktop pixels over that window. So: if any visible window
; intersects the drawn rect (frame + 1px shadow), fall back to a full
; wm_paint_all; otherwise do the cheap single-zone redraw. (SPEC.md 26)
; in:  AL = zone index; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
desk_zone_redraw:
    push ax
    push bx
    push cx
    push dx
    push si

    push ax                     ; keep the zone index for the draw call
    mov ah, 0
    mov bl, DESK_ZSTEP
    mul bl                      ; AX = zone * pitch (8-bit mul: no DX)
    add ax, DESK_ZY0
    mov cx, ax                  ; CX = drawn-rect y1
    mov dx, ax
    add dx, DESK_ZH-1           ; DX = drawn-rect y2

    mov si, wm_wins
    mov bh, MAX_WIN
.win:
    test word [si+W_FLAGS], 2   ; visible?
    jz .next
    mov ax, [si+W_X]
    cmp ax, [vid_desk_zr]       ; window left past the rect's right edge?
    jae .next                   ; (zx + 47 + 2 + 1, the shadow included)
    add ax, [si+W_W]            ; right edge + the 1px drop shadow
    cmp ax, [vid_desk_zl]
    jb .next
    mov ax, [si+W_Y]
    cmp ax, dx                  ; top below the rect?
    ja .next
    add ax, [si+W_H]            ; bottom edge + shadow
    cmp ax, cx
    jb .next
    pop ax                      ; overlapped: repaint the world (zone included)
    call wm_paint_all
    jmp .done
.next:
    add si, WIN_SIZE
    dec bh
    jnz .win
    pop ax                      ; clear air: cheap single-zone redraw
    call desk_draw_zone
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov [desk_zdrv], ax         ; zone/drive index
    mov bx, DESK_ZSTEP
    mul bx                      ; AX = 60*i (DX clobbered)
    add ax, DESK_ZY0
    mov [desk_zy], ax           ; zone top

    ; --- gray desktop under the zone + the label's 2px side overhang --------
    mov bx, ax                  ; y1 = top
    mov dx, ax
    add dx, DESK_ZH-1           ; y2 = top+43
    mov ax, [vid_desk_zl]       ; covers the label overhang
    mov cx, [vid_desk_zr]
    dec cx
    call gfx_fill_gray

    ; --- 32x32 disk icon, 8px in from the zone's left ------------------------
    mov cx, [vid_desk_zx]
    add cx, 8
    mov dx, [desk_zy]
    mov si, ico_disk32
    call icon_draw

    ; --- label: white rect x 582..633, rows top+32..top+43 -------------------
    mov bx, [desk_zy]
    add bx, 32                  ; y1: just under the icon
    mov dx, bx
    add dx, 11                  ; y2: 12 rows = 2px gap + 8px text + 2px gap
    mov ax, [vid_desk_zl]
    mov cx, [vid_desk_zr]
    dec cx
    mov byte [gfx_color], CWHITE
    call gfx_fill

    ; --- "Disk A" / "Disk B", black, 48px wide = the whole zone width --------
    mov byte [gfx_color], CBLACK
    mov si, desk_lbl_a
    cmp byte [desk_zdrv], 0
    je .label
    mov si, desk_lbl_b
.label:
    mov cx, [vid_desk_zx]
    mov dx, [desk_zy]
    add dx, 34                  ; text rows top+34..top+41
    call font_str

    ; --- selection highlight: XOR the whole hit zone --------------------------
    mov al, [desk_sel]
    cmp al, byte [desk_zdrv]
    jne .done
    mov ax, [vid_desk_zx]
    mov bx, [desk_zy]
    mov cx, ax
    add cx, DESK_ZW-1
    mov dx, [desk_zy]
    add dx, DESK_ZH-1
    call gfx_xor_fill
.done:
    mov al, [desk_gcol]
    mov [gfx_color], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

desk_lbl_a  db "Disk A", 0
desk_lbl_b  db "Disk B", 0

; -----------------------------------------------------------------------------
section .bss
desk_ndrives resb 1             ; floppy drives shown on the desktop (0..2)
desk_sel     resb 1             ; selected zone index, 0xFF = none
desk_clkt    resw 1             ; click birth tick (EV_C) of the last zone click
desk_zy      resw 1             ; desk_draw_zone: current zone top
desk_zdrv    resw 1             ; desk_draw_zone: current zone index
desk_gcol    resb 1             ; desk_draw_zone: saved [gfx_color]
desk_dmask   resb 1             ; desk_dmg_zones: the mask under construction
desk_dzi     resb 1             ; desk_dmg_zones: its loop index
section .text
