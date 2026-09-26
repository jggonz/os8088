; Advanced Dock runtime, optional and pinned while a setting needs it.
; DS = KERNEL_SEG. The image publishes TWO entries and neither of them is an
; operation: dkx_hook points dock.inc's dkv at this image and dkx_unhook puts
; the kernel's own bodies back. After that the kernel reaches an operation by
; far-jumping straight at its landing pad, so there is no dispatcher here, no
; index to carry and no BP to bank - and the kernel keeps neither a second
; copy of the basic table nor a per-operation trampoline, which is where
; SPEC.md 30.5's routing bytes went.
%ifdef DOCK_OPT                 ; kern_big only (SPEC.md 30.5)
section .modk
modk_hdr:
    dw 0x384F
    db MOD_VER, MOD_DOCK
    dw BUILD_NUM, MOD_STAMP, modk_end, DK_NENT
    dw dkx_hook, dkx_unhook

; -----------------------------------------------------------------------------
; dkx_hook / dkx_unhook - take dock.inc's dkv over, and give it back
; in:  nothing; the graphics lock is held by dkf_apply's caller, so no
;      operation can be executing while the table is rewritten (SPEC.md 30.5)
; out: CF = 0; every register preserved
;
; THE TABLE IS WRITTEN FROM HERE BECAUSE THE BASIC ADDRESSES ARE KNOWN HERE.
; A module is refused unless it was assembled with this very kernel - the
; build number AND the layout stamp both (SPEC.md 2.8.2) - so dkb_tab below
; is not a guess about the kernel's offsets, it is the kernel's own offsets.
; The kernel therefore stores the basic table ONCE, as dkv's rest state, and
; never needs a copy to restore from.
dkx_hook:
    push bx
    push si
    mov bx, cs                  ; ...this image
    mov si, dkxf_tab
    jmp short dkx_vset
dkx_unhook:
    push bx
    push si
    call dkx_drop               ; an open strip is forgotten on the way out -
                                ; done HERE rather than through a resident
                                ; cw_ shim, since we are about to be freed
    mov bx, KERNEL_SEG          ; ...and back to the kernel's own
    mov si, dkb_tab
dkx_vset:
    push ax
    push cx
    push di
    mov di, dkv                 ; DS is KERNEL_SEG, so the WRITES are plain;
    mov cx, DKI_N               ; the source is ours and is read through CS
    cld
.l:
    cs lodsw
    mov [di], ax
    mov [di+2], bx
    add di, 4
    loop .l
    pop di
    pop cx
    pop ax
    pop si
    pop bx
    clc
    retf

; --- SPEC.md 30.5/30.6's ADVANCED GEOMETRY, in the image that uses it -------
; Every one of these is written by dkx_geom or dkx_live_set and read by
; nothing outside this file, so it lives HERE and not in the kernel's `.bss`:
; a machine with no DOCK.DRV mounted - which is every machine until somebody
; moves the strip - carries none of it. That is why the references above are
; spelled `[cs:...]`: DS is the kernel's throughout (SPEC.md 2.6), so ours is
; the segment that needs naming. The kernel keeps only what IT reads: the
; live rect, its two crosses, [dock_side], [dock_up], [dock_c0] and
; [dock_thk].
dock_sx1:   dw 0                ; the strip's whole rect - SEVEN CONSECUTIVE
dock_sy1:   dw 0                ; WORDS, which dkx_live_set copies over the
dock_sx2:   dw 0                ; kernel's live block in one rep movsw
dock_sy2:   dw 0
dock_frc:   dw 0                ; ...its rule, ACROSS the long axis
dock_ffc1:  dw 0                ; ...and its field, across
dock_ffc2:  dw 0
dock_ffar:  dw 0                ; its outer edge, where a hidden line stands
dock_la1:   dw 0                ; the long axis's extent: x on the bottom
dock_la2:   dw 0                ; strip, y on a side
dock_a0:    dw 0                ; tile 0's along coordinate
dock_astep: dw 0                ; along pitch
dock_alen:  dw 0                ; along length of one tile
dock_cap:   db 0                ; tiles the strip holds (SPEC.md 30.5.1)
dock_pos:   times INST_MAX db 0 ; which slot each position shows (dkx_map)
dock_hidden: db 0               ; auto and not open: the line is live
dock_auto:  db 0                ; non-0: the strip hides
dock_timer_on:   db 0           ; hover while hidden, linger while open
dock_timer_tick: dw 0           ; modular start tick; survives cancellation
%if dock_ffc2 - dock_sx1 != 12
  %error "dkx_live_set copies seven consecutive geometry words"
%endif

dkb_tab:                        ; the KERNEL's bodies, in dock.inc's DKI_* order
    dw dkb_geom, dkb_band, db_force, db_paint, db_px_hit
    dw db_click, db_rclick, db_tile_rect, dkb_force_r, dkb_ret
    dw dkb_ret, dkb_ret, dkb_clc, dkb_clc
dkxf_tab:                       ; ...and this image's landing pads, likewise
    dw dkxf_geom, dkxf_band, dkxf_force, dkxf_paint, dkxf_px_hit
    dw dkxf_click, dkxf_rclick, dkxf_slot_rect, dkxf_force_r, dkxf_drop
    dw dkxf_pass, dkxf_hole_arm, dkxf_hole_sub, dkxf_hole_win
%if ($ - dkxf_tab) != DKI_N * 2 || (dkxf_tab - dkb_tab) != DKI_N * 2
  %error "dkb_tab/dkxf_tab and dock.inc's DKI_* indices disagree"
%endif

; The landing pads. The kernel far-JUMPS here, so the caller's own near
; return address is still the top of the stack: the pad near-calls the body,
; which returns normally, and then goes home to a bare `ret` in the kernel -
; a far jump, because CS is this image's until it does. Nothing here touches
; a flag, so the CF an operation answers with arrives intact.
%macro DKXPAD 1
    call %1
    jmp short dkxf_home
%endmacro
dkxf_geom:      DKXPAD dkx_geom
dkxf_band:      DKXPAD dkx_band
dkxf_force:     DKXPAD dkx_force
dkxf_paint:     DKXPAD dkx_paint
dkxf_px_hit:    DKXPAD dkx_px_hit
dkxf_click:     DKXPAD dkx_click
dkxf_rclick:    DKXPAD dkx_rclick
dkxf_slot_rect: DKXPAD dkx_slot_rect
dkxf_force_r:   DKXPAD dkx_force_r
dkxf_drop:      DKXPAD dkx_drop
dkxf_pass:      DKXPAD dkx_pass
dkxf_hole_arm:  DKXPAD dkx_gfx_hole_arm
dkxf_hole_sub:  DKXPAD dkx_hole_sub
dkxf_hole_win:  DKXPAD dkx_hole_win
dkxf_home:
    jmp KERNEL_SEG:dkb_ret
dkx_force:
    push ax
    push cx
    mov ax, [cs:dock_la1]          ; THE WHOLE STRIP: its along range, whichever
    mov cx, [cs:dock_la2]          ; edge it stands on (SPEC.md 30.5)
    jmp dkx_force_r.union

; dkx_force_r - ...only the part of it inside rect AX,BX,CX,DX was
; in:  AX = x1, BX = y1, CX = x2, DX = y2, inclusive, VIRTUAL
; out: nothing (all registers preserved)
;
; SPEC.md 30.5. This was dock_force_x, which took an x span and trusted its one
; caller - wm_paint_dmg - to have asked `y2 >= [vid_dock_y0]` first. A strip on
; a SIDE is an x question and not a y one, so the caller now hands over the
; whole damage and the overlap test is here, against the LIVE rect: the 1px
; line while the strip is hidden, the strip otherwise. The along span is then
; clamped to the strip's own extent, which is also what keeps a damage rect on
; a second monitor off the primary's strip (SPEC.md 39.19.5) - the clamp
; dock_force_x carried under KERN_BIG for that reason is the intersection now.
dkx_force_r:
    push ax
    push cx
    cmp byte [dock_up], 0       ; an OPEN strip is ON TOP (SPEC.md 30.6.1):
    jne .out                    ; nothing under it drew over it, being clipped
                                ; round it, so the damage owes it nothing
    call dkx_live_hit          ; does the damage reach the strip at all?
    jnc .out
%ifdef REDRAWFULL
    mov ax, [cs:dock_la1]          ; REFERENCE BUILD: every damage is the whole
    mov cx, [cs:dock_la2]          ; strip, the behaviour this replaced
%else
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .clamp                   ; bottom: the along axis IS x
    mov ax, bx                  ; a side: it is y
    mov cx, dx
.clamp:
    cmp ax, [cs:dock_la1]
    jge .c1
    mov ax, [cs:dock_la1]
.c1:
    cmp cx, [cs:dock_la2]
    jle .union
    mov cx, [cs:dock_la2]
%endif
.union:                         ; dkx_force enters HERE with its own pushes
    cmp ax, [dock_dx1]          ; SIGNED, because empty is 07FFFh..08000h -
    jge .u2                     ; dkx_px_add's own compares, on the other span
    mov [dock_dx1], ax
.u2:
    cmp cx, [dock_dx2]
    jle .out
    mov [dock_dx2], cx
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dkx_live_hit - does rect AX,BX,CX,DX overlap the LIVE strip? (SPEC.md 30.6)
; out: CF = 1 yes; every register preserved
; -----------------------------------------------------------------------------
dkx_live_hit:
    cmp ax, [dock_lx2]
    jg .no
    cmp cx, [dock_lx1]
    jl .no
    cmp bx, [dock_ly2]
    jg .no
    cmp dx, [dock_ly1]
    jl .no
    stc
    ret
.no:
    clc
    ret

; dkx_live_rect - AX,BX,CX,DX = the live strip's rect. Clobbers the four.
dkx_live_rect:
    mov ax, [dock_lx1]
    mov bx, [dock_ly1]
    mov cx, [dock_lx2]
    mov dx, [dock_ly2]
    ret

; dkx_px_rect - AX,BX,CX,DX = the part of the live strip the last dkx_paint
; PAINTED (SPEC.md 30.3.3), as a rect. Clobbers the four.
dkx_px_rect:
    mov ax, [dock_px1]
    mov cx, [dock_px2]
    mov bx, [dock_lc1]
    mov dx, [dock_lc2]
    jmp short dkx_swap_side

; dkx_span_fill - fill ALONG AX..CX, ACROSS BX..DX in the current pen
; in:  AX = a1, CX = a2, BX = c1, DX = c2; gfx lock held
; out: nothing (all registers preserved)
dkx_span_fill:
    push ax
    push bx
    push cx
    push dx
    call dkx_swap_side
    call KERNEL_SEG:cw_gfx_fill
    jmp dkx_ret_dx

; dkx_swap_side - along/across <-> x/y: nothing on the bottom strip, where the
; along axis IS x, and the two exchanges on a side. Flags only otherwise.
dkx_swap_side:
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .out
    xchg ax, bx
    xchg cx, dx
.out:
    ret

; -----------------------------------------------------------------------------
; dkx_geom - [dock_cfg] and the primary's extent -> every word of geometry
; in:  [dock_cfg], [vid_pw]/[vid_ph] (the chrome's extent, SPEC.md 39.16)
; out: the strip rect, the tile layout, the band ([vid_dock_y0],
;      [vid_band_x0], [vid_band_xe]), [vid_desk_zx] and the live set.
;      All registers preserved.
;
; SPEC.md 30.5's table. vid_init calls it after publishing the chrome extent;
; adapter switches and display layout changes re-derive it, and
; dock_apply calls it when the setting itself moves. It closes the strip -
; [dock_up] = 0 - because a geometry that moved under an open one would make
; its clip hole describe a rect that is not there.
; -----------------------------------------------------------------------------
dkx_geom:
    push ax
    push bx
    push cx
    push dx
    call dkx_drop ; discard the old geometry's clip hole
    mov al, [dock_cfg]
    mov ah, al
    and al, DOCK_P_MASK
    cmp al, DOCK_P_RIGHT
    jbe .have
    xor al, al                  ; 3: a byte from a future version
.have:
    mov [dock_side], al
    and ah, DOCK_F_AUTO
    mov [cs:dock_auto], ah
    mov byte [cs:dock_cap], INST_MAX   ; every tile fits a bottom strip (SPEC.md
                                ; 30.5.1): 8 + 12*28 = 344 of 640
    mov bx, [vid_pw]
    mov dx, [vid_ph]
    mov cx, [vid_phm1]          ; every strip ends on the primary's last row
    mov [cs:dock_sy2], cx
    mov [cs:dock_la2], cx          ; ...which is a side strip's along end too
    or al, al
    jnz .side

    ; --- bottom: the geometry this module always had --------------------------
    xor ax, ax
    mov [cs:dock_sx1], ax
    mov [cs:dock_la1], ax
    mov [cs:dock_ffc2], cx         ; the field runs to the last row...
    mov [cs:dock_ffar], cx         ; ...which is where a hidden strip's line is
    mov ax, [vid_pwm1]
    mov [cs:dock_sx2], ax
    mov [cs:dock_la2], ax
    mov ax, dx
    sub ax, DOCK_H
    mov [cs:dock_sy1], ax
    mov [cs:dock_frc], ax          ; the rule is the strip's first row
    inc ax
    mov [cs:dock_ffc1], ax
    inc ax
    inc ax
    mov [dock_c0], ax           ; tile top = rule + 3
    mov word [cs:dock_a0], DOCK_X0
    mov word [cs:dock_astep], DOCK_STEP
    mov word [cs:dock_alen], DOCK_TILE_W
    mov ax, DOCK_H
    call .thick
    jmp .done

.side:
    mov ax, MBAR_H
    mov [cs:dock_sy1], ax
    mov [cs:dock_la1], ax
    add ax, DOCK_SY0
    mov [cs:dock_a0], ax
    mov word [cs:dock_astep], DOCK_SSTEP
    mov word [cs:dock_alen], DOCK_TILE_H
    mov ax, dx                  ; --- HOW MANY TILES THE SIDE HOLDS (SPEC.md
    sub ax, MBAR_H + 2*DOCK_SY0 + DOCK_TILE_H   ; 30.5.1): every one on VGA,
    jb .one                     ; eleven on Hercules and EGA, seven on CGA -
    mov bl, DOCK_SSTEP          ; and the tiles then pack rather than keeping
    div bl                      ; their slots (dkx_map). AX < 480 and the
    inc al                      ; step is 24, so the quotient is a byte
    cmp al, INST_MAX
    jbe .cap
    mov al, INST_MAX
    jmp short .cap
.one:
    mov al, 1
.cap:
    mov [cs:dock_cap], al
    mov bx, [vid_pw]            ; (the divide spent BX)
    mov ax, DOCK_SW
    call .thick                 ; AX = the reserved thickness
    mov cx, bx
    sub cx, DOCK_SW            ; right strip's first column
    mov dx, [vid_pwm1]
    cmp byte [dock_side], DOCK_P_LEFT
    jne .srect
    xor cx, cx                 ; left: the same cross geometry at column zero
    mov dx, DOCK_SW - 1
.srect:
    mov [cs:dock_sx1], cx
    mov [cs:dock_sx2], dx
    mov bx, cx
    add bx, DOCK_SX
    mov [dock_c0], bx
    mov [cs:dock_frc], cx         ; rule faces inward on the right strip
    mov [cs:dock_ffar], dx
    inc cx
    cmp byte [dock_side], DOCK_P_LEFT
    jne .field
    dec cx
    mov [cs:dock_ffar], cx        ; left: outer edge at zero, rule at the far end
    mov [cs:dock_frc], dx
    dec dx
.field:
    mov [cs:dock_ffc1], cx
    mov [cs:dock_ffc2], dx

.done:
    mov [dock_thk], ax          ; (wm_dock_snap's half-a-strip test)
    call dkx_band
    call dkx_live_set
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.thick:                         ; AX = the strip's width -> AX = what it
    cmp byte [cs:dock_auto], 0     ; RESERVES: all of it, or the line
    je .tout
    mov ax, 1
.tout:
    ret

dkx_band:
    push ax
    push bx
    push dx
    xor ax, ax
    mov [vid_band_x0], ax
    mov bx, [vid_pw]
    mov dx, [vid_ph]
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .bottom
    cmp byte [dock_side], DOCK_P_LEFT
    jne .right
    mov ax, [dock_thk]
    mov [vid_band_x0], ax
    jmp short .done
.right:
    sub bx, [dock_thk]
    jmp short .done
.bottom:
    sub dx, [dock_thk]
.done:
    mov [vid_band_xe], bx
    mov [vid_dock_y0], dx
    sub bx, 56
    mov [vid_desk_zx], bx
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dkx_live_set - the LIVE rect, rule, field and cross, from [dock_up]
; in:  the strip's geometry (dkx_geom), [cs:dock_auto], [dock_up]
; out: [cs:dock_hidden] and the live set; all registers preserved
;
; SPEC.md 30.6. Hidden is "auto and not open", and while it is the live strip
; is the outer edge's one line: its rect, a rule that IS the line, and no field
; (fc1 > fc2). Otherwise the live strip is the strip. Every painter and every
; overlap test reads the live set and none of them asks which mode it is in.
; -----------------------------------------------------------------------------
dkx_live_set:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    push ds
    pop es                    ; ES = the kernel's, where the LIVE block is
    push cs
    pop ds                    ; DS = ours, where the STRIP block is (30.5)
    mov si, dock_sx1
    mov di, dock_lx1
    mov cx, 7                 ; rect, rule, and field: identical word layouts
    cld
    rep movsw
    pop ds
    pop es
    pop di
    pop si
    mov byte [cs:dock_hidden], 0
    cmp byte [cs:dock_auto], 0
    je .cross
    cmp byte [dock_up], 0
    jne .cross
    inc byte [cs:dock_hidden]
    mov ax, [cs:dock_ffar]         ; the line: the outer edge
    mov [dock_rc], ax
    mov word [dock_fc1], 1      ; ...and no field: an empty cross
    mov word [dock_fc2], 0
    cmp byte [dock_side], DOCK_P_BOTTOM
    jne .sline
    mov [dock_ly1], ax
    mov [dock_ly2], ax
    jmp short .cross
.sline:
    mov [dock_lx1], ax
    mov [dock_lx2], ax
.cross:
    mov ax, [dock_ly1]          ; the live strip's extent ACROSS its long axis
    mov cx, [dock_ly2]
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .c
    mov ax, [dock_lx1]
    mov cx, [dock_lx2]
.c:
    mov [dock_lc1], ax
    mov [dock_lc2], cx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_apply - the SETTING moved: geometry, drive column, windows, strip
; in:  [dock_cfg] already written; gfx lock held by the caller, or at boot
; out: nothing (all registers preserved). THE CALLER OWES THE REPAINT, which
;      is vid_switch's own contract: the Control Panel posts [cp_dirty]
;
; SPEC.md 30.5. wm_refit moves every window back inside the new band, and the
; raise caches go because a window that did not move can have had the strip
; stand up or lie down behind it.
; -----------------------------------------------------------------------------
dkx_paint:
    push cs
    call dkk_wm_fs_vis              ; a visible fullscreen window is OVER the strip
    jnc .go                     ; (SPEC.md 11.2). Same reasoning as
    call dkx_force             ; menu_draw_bar's guard: whether the dock is on
    clc                         ; screen is a fact about the screen - and while
    ret                         ; one is up our pixels are being destroyed, so
                                ; the whole strip is owed when it goes. That is
                                ; how wm_paint_all stopped needing dkx_force:
                                ; its dither no longer reaches us (SPEC 30.3.2)
                                ; and this is the only other thing that can.
                                ; CF = 0, "nothing was drawn", which is what
                                ; wm_dock_under needs to hear
.go:
    push ax
    push bx
    push cx
    push dx
    push si                     ; SI IS ALREADY PRESERVED (nothing between
    push di                     ; here and the exit touches it) - it is pushed
                                ; to make ax..di the shared epilogue ladder's
                                ; own run, which is what dkx_ret_di wants

    push word [gfx_hole]        ; THE OPEN STRIP DRAWS ITSELF THROUGH ITS OWN
    push word [wm_clip_n]       ; HOLE (SPEC.md 30.6.1): the hole and any
    cmp byte [dock_up], 0       ; region armed round it are put away for the
    je .nohole                  ; paint and back at the one exit. Zeroed ONLY
    xor ax, ax                  ; while it is open - a region armed under a
    mov [gfx_hole], ax          ; strip that is under windows is one this
    mov [wm_clip_n], ax         ; paint must keep honouring
.nohole:
    mov al, [gfx_color]         ; byte-only read: gfx_lock_flag lives next
    push ax                     ; door. ON THE STACK, not in .bss, and that
                                ; rests on dkx_paint having EXACTLY ONE EXIT
                                ; below `.go` - the five pops at the bottom.
                                ; The wm_fs_vis refusal returns ABOVE this, and
                                ; the CF answer is after the pops. A second
                                ; early return added below here leaks a word
                                ; onto the UI task's stack on every dkx_paint,
                                ; which is a slow death nothing in the tree
                                ; would report. See the matching `pop ax`.
    mov byte [dock_drew], 0
    mov word [dock_px1], 07FFFh ; ...and nothing of the strip is painted YET
    mov word [dock_px2], 08000h ; (SPEC.md 30.3.3): [dock_px1]/[dock_px2] are
                                ; this pass's answer, not the last one's, and
                                ; EMPTY is the inverted bounding box 07FFFh..
                                ; 08000h so that dkx_px_hit's signed compares
                                ; refuse every rect rather than accepting all
                                ; of them - the same idiom wm_dmg_bands uses.
                                ; Inlined: dkx_px_add and dkx_px_tile grow
                                ; this pair and had one caller each, but this
                                ; had ONE caller in the whole tree

    push cs
    call dkk_wm_top                 ; who owns the frontmost visible window? Its
    xor di, di                  ; tile gets the active mark (0 = nobody), and
    or bx, bx                   ; every tile's key depends on it, so it is
    jz .noact                   ; resolved before the first one is tested.
    call KERNEL_SEG:cw_inst_win_owner         ; DI = its instance record, or 0 - and the
.noact:                         ; `or bx,bx` guard must stay, because
    mov [dock_act], di          ; inst_of_win compares I_WIN against BX and
                                ; BX = 0 would match a live instance with no
                                ; window. ONE store, both ways round

.full:                          ; somebody drew over us: the rule and field
                                ; they covered, and the tiles they reached.
                                ; THE SPAN IS SPENT HERE, at the top, where
                                ; [dock_full]'s clear used to be - a dkx_force
                                ; arriving mid-paint then unions onto the fresh
                                ; sentinel and re-arms the NEXT pass, where a
                                ; reset at the BOTTOM would union into the live
                                ; span and be wiped by it: a lost repaint.
                                ;
                                ; The exchange is BRACKETED because two words
                                ; are two instructions: with only one of them
                                ; reset, a force unioning in has its other end
                                ; overwritten by our second store and the pair
                                ; comes out empty either way round - the same
                                ; lost repaint through a window one instruction
                                ; wide instead of a whole paint. Three bytes
                                ; make it a construction rather than a smaller
                                ; argument; the gfx lock is believed to close
                                ; the window as well, and this does not depend
                                ; on believing it. `pushf`/`popf`, never
                                ; `cli`/`sti` - the caller's IF is its own.
    mov ax, 07FFFh
    mov cx, 08000h
    pushf
    cli
    xchg ax, [dock_dx1]
    xchg cx, [dock_dx2]
    popf
    cmp ax, cx                  ; empty (x1 > x2) is "nothing drew over us"
    jg .scan
    inc byte [dock_drew]        ; ...and it cannot wrap: this byte is zeroed at
                                ; the top of every pass and reaches at most
                                ; 1 + INST_MAX = 13, while the test below is
                                ; `cmp x,1 / cmc`, which is exact for all 256
    call dkx_px_add            ; ...over exactly this span, and it is the
                                ; span a window has to overlap to be damaged
                                ; by it (SPEC.md 30.3.3)

    ; FOUR REGISTER CONTRACTS CARRY THE NEXT TWELVE LINES, and they are
    ; named here because the day one of them is loosened this reads as a
    ; drawing bug rather than as what it is. AX and CX still hold
    ; [dock_dx1]/[dock_dx2] from the call above; DX comes back from gfx_hline
    ; still holding [vid_dock_y0]. The four:
    ;   dkx_px_add   "every register preserved"          (its header above)
    ;   thm_ink/thm_bg "clobbers: nothing (flags preserved)" (vga12.inc)
    ;   gfx_hline     "clobbers: flags"                   (vga12.inc)
    ;   gfx_fill      "clobbers: flags"                   (vga12.inc)
    ; Four memory reads off this path, and it is the [dock_full] path - the
    ; one somebody drawing over the strip takes.
    push cs
    call dkk_thm_ink                ; the rule, over the damaged span only - the
    mov bx, [dock_rc]           ; LINE, while the strip is hidden (SPEC.md
    mov dx, bx                  ; 30.6), and it is one fill on any edge
    call dkx_span_fill

    mov bx, [dock_fc1]          ; ...and the field beside it, which a hidden
    mov dx, [dock_fc2]          ; strip does not have (fc1 > fc2)
    cmp bx, dx
    jg .nofield
    push cs
    call dkk_thm_bg
    call dkx_span_fill
.nofield:

    mov dx, ax                  ; ...and the tiles it actually covered: a
    mov si, cx                  ; tile outside the damage still has its
                                ; pixels, so its key is still the truth.
                                ; DX/SI = the SPAN, in registers, because the
                                ; xchg above has already put the sentinel back
                                ; in [dock_dx1]/[dock_dx2] - AX and CX have
                                ; carried it since `.full` (dkx_px_add is
                                ; "every register preserved", gfx_hline and
                                ; gfx_fill are "clobbers: flags"). Net zero
                                ; bytes and two register compares an iteration
                                ; instead of two memory ones: the two `mov`s
                                ; here cost exactly the four the body saves
    mov ax, [cs:dock_a0]           ; ALONG the strip, whichever edge it is on
    mov di, dock_ck
    mov cx, INST_MAX
.zap:
    mov bx, ax
    add bx, [cs:dock_alen]
    dec bx
    cmp bx, dx
    jb .znext                   ; wholly left of the damage
    cmp ax, si
    ja .scan                    ; wholly right of it - AND SO IS EVERY TILE
                                ; AFTER IT, because AX walks left to right by
                                ; DOCK_STEP. Free: the same 2-byte `ja`, and
                                ; it skips up to eleven passes of this body on
                                ; the commonest damage shape (SPEC.md 30.3.1's
                                ; own example, a window closing in one corner)
    mov word [di], 0
.znext:
    add ax, [cs:dock_astep]
    inc di
    inc di
    loop .zap

.scan:
    cmp byte [cs:dock_hidden], 0   ; a hidden strip has the line and no tiles
    jne .tdone                  ; (SPEC.md 30.6): the keys keep what they were,
                                ; and the reveal forces the whole strip
    call dkx_map               ; which record each POSITION shows (30.5.1)
    mov si, dock_pos            ; SI walks the positions - OURS, so the read
                                ; below carries the override (SPEC.md 30.5)
    mov bx, dock_ck             ; ...and ITS KEY BESIDE IT: BX walks the side
                                ; table rather than indexing it, which spends
                                ; no shift and no 16-bit displacement per
                                ; iteration. BX costs no new push (dkx_paint
                                ; already banks it) and every callee in this
                                ; loop preserves it - dkx_key touches only AX,
                                ; and dkx_px_tile, dkx_xor_ring,
                                ; dkx_xor_body, dkx_erase_tile and
                                ; dkx_draw_tile all `push bx` and leave
                                ; through dkx_ret_dx / dkx_ret_si
    xor cx, cx                  ; CX = the POSITION
.tile:
    xor di, di                  ; DI = the record it shows, 0 = none
    mov al, [cs:si]
    cmp al, 0FFh
    je .key
    call KERNEL_SEG:cw_inst_ptr
.key:
    call dkx_key               ; AX = what this tile should look like, or 0
    mov dx, [bx]                ; for no tile at all (SPEC.md 29.2 rule 3)
    cmp ax, dx
    je .next                    ; already drawn that way
    mov [bx], ax
    inc byte [dock_drew]        ; ...and cannot wrap - see `.full`
    call dkx_px_tile           ; ...and this tile's columns join the painted
                                ; span, wherever it stands - a tile outside the
                                ; forced span still puts pixels down, so the
                                ; span is the UNION of the two (SPEC.md 30.3.3)

    xor ax, dx                  ; AX = what MOVED (SPEC.md 30.3), DX = the key
                                ; it WAS. The diff lives in the ACCUMULATOR
                                ; because `test ax,imm16` is 3 bytes where
                                ; `test dx,imm16` is 4, and `test al,imm8` 2
                                ; where `test dl,imm8` is 3 - three sites
%ifdef REDRAWFULL
    jmp short .rebuild          ; REFERENCE BUILD: erase and rebuild any tile
%endif                          ; that changed at all, the behaviour this
                                ; replaced
    test ax, DOCK_K_ID
    jnz .rebuild                ; the icon changed, or the tile came or went

    test al, DOCK_K_ACT         ; --- only the marks moved: the ICON IS NOT
    jz .mini                    ; TOUCHED, and neither is the frame or the
    call dkx_xor_ring          ; white around them. Both marks are their own
.mini:                          ; inverse, so putting one on and taking it off
    test al, DOCK_K_MINI        ; are the same operation - which is the whole
    jz .next                    ; reason the erase existed
    call dkx_xor_body
    jmp short .next

.rebuild:
    call dkx_erase_tile        ; a new icon is not a superset of the old one,
                                ; so this one does start from white. It
                                ; preserves AX itself (push ax .. dkx_ret_dx), so
                                ; the bracket that used to be here was dead
    cmp ax, dx                  ; new == diff XOR old, so "new == 0" is
    je .next                    ; "diff == old": the tile went away, and
                                ; erased is all it needed
    call dkx_draw_tile         ; in: CX = index, DI = record
.next:
    inc si
    inc bx
    inc bx
    inc cx
    cmp cx, INST_MAX
    jb .tile

.tdone:
    pop ax                      ; the pen, saved at `.go` - see there for the
    mov [gfx_color], al         ; one-exit invariant this rests on. AH is dead
                                ; (AX itself is popped by the rung below).
    pop word [wm_clip_n]        ; ...and the hole and its region, back
    pop word [gfx_hole]
    cmp byte [dock_up], 0       ; AN OPEN STRIP DAMAGES NO WINDOW: it is on
    je .answer                  ; top, so "something drew" must not send
    mov byte [dock_drew], 0     ; wm_dock_under off to repaint what is under it
.answer:
    cmp byte [dock_drew], 1     ; THE CF ANSWER BEFORE THE POPS - `pop` spends
    cmc                         ; no flag, which twenty other sites in this
                                ; kernel already depend on (font.inc:629
                                ; writes the property down, and disk.inc:5725
                                ; puts a FAR CALL between the `clc` and its
                                ; rung). `cmp x,1` borrows exactly when x == 0,
                                ; so after the `cmc` CF is "something drew" -
                                ; and that is EXACT FOR ALL 256 VALUES, not
                                ; only for the 0/1 this byte happens to hold
    jmp dkx_ret_di                 ; pops di, si, dx, cx, bx, ax: `.go`'s push run

; -----------------------------------------------------------------------------
; dkx_px_add  - grow the painted span to cover x AX..CX as well (inclusive)
; dkx_px_tile - ...and to cover tile CX's own columns
; out: nothing; every register preserved (dkx_px_add spends flags, which no
;      caller of it needs)
;
; SPEC.md 30.3.3. [dock_px1]/[dock_px2] is what dkx_paint PUT PIXELS ON in
; the pass that has just run, which is not [dock_dx1]/[dock_dx2]: that pair is
; what somebody drew OVER US and is only half the answer, because a tile whose
; key moved is repainted wherever it stands, damage or no damage.
;
; **Empty is 0x7FFF..0x8000**, so an untouched span refuses every rect through
; dkx_px_hit's signed compares rather than accepting all of them - the same
; empty-bounding-box idiom wm_dmg_bands uses, and now the damage span's too
; (see dock_force_x). There is no dock_px_none: emptying the span is two
; immediate stores and it had exactly ONE caller in the tree, so it is written
; out at the top of dkx_paint where the reason for it is.
; -----------------------------------------------------------------------------
dkx_px_add:
    cmp ax, [dock_px1]
    jge .x2
    mov [dock_px1], ax
.x2:
    cmp cx, [dock_px2]
    jle .out
    mov [dock_px2], cx
.out:
    ret

dkx_px_tile:
    push ax
    push bx                     ; dkx_tile_rect eats BX and DX where
    push cx                     ; dock_tile_x ate only BX - and DX IS LIVE in
    push dx                     ; dkx_paint's tile loop (it carries the old
                                ; key across the `xor dx, ax`), so this push
                                ; is not tidiness. CX is the instance index
                                ; and AX a key.
    call dkx_tile_rect         ; AX..DX = the tile
    call dkx_swap_side         ; ...and its ALONG pair into AX/CX, which is
    call dkx_px_add            ; all dkx_px_add reads (SPEC.md 30.5)
    jmp dkx_ret_dx

; -----------------------------------------------------------------------------
; dkx_px_hit - does this rect overlap the strip dkx_paint just painted?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - inclusive, VIRTUAL coordinates
; out: CF = 1 = the strip was drawn under it, so it owes a redraw;
;      all registers preserved
;
; SPEC.md 30.3.3. wm_dmg_wins asked `y + h >= [vid_dock_y0]` and marked the
; window on that alone, which is wrong in two directions at once and both of
; them cost a whole window repaint, W_PAINT included.
;
;   - The strip is painted over a SPAN (30.3.1) and the test had no x in it,
;     so a window over one end of the strip was redrawn because something
;     moved at the other end.
;   - The strip is the PRIMARY's and this rect is virtual (39.19.5), so on an
;     extended desktop a window on the second monitor - which has no dock at
;     all - was marked for reaching the primary's dock ROW. That is the
;     expensive one: a window across the seam can hold no raise cache
;     (39.14.8), so its redraw is a full W_PAINT rather than a ~10 ms blit.
;
; So it is an ordinary rect overlap against the strip that was drawn: the
; painted span in x, and [vid_dock_y0]..[vid_phm1] in y, the primary sitting
; at the virtual origin by construction (39.16). A one-display machine reads
; [vid_phm1] = [vid_hm1] and the y half is exactly what it always was.
; -----------------------------------------------------------------------------
dkx_px_hit:
%ifdef REDRAWFULL
    jmp dkx_live_hit           ; THE REFERENCE BUILD (SPEC.md 30.3.3): every
                                ; window whose rect reaches the strip, wherever
                                ; along it the paint was
%else
    push ax
    push bx
    push cx
    push dx
    call dkx_swap_side         ; AX/CX = ALONG, BX/DX = ACROSS (SPEC.md 30.5)
    cmp ax, [dock_px2]
    jg .no
    cmp cx, [dock_px1]
    jl .no
    cmp bx, [dock_lc2]          ; ...and across it, the live strip's own
    jg .no                      ; extent: which is also what keeps a window on
    cmp dx, [dock_lc1]          ; a second monitor off the primary's strip
    jl .no                      ; (39.19.5) - the display bound the old test
    stc                         ; spelt out is the across test on any edge
    jmp dkx_ret_dx
.no:
    clc                         ; SIGNED throughout: [dock_px1]/[dock_px2]
    jmp dkx_ret_dx                 ; carry the 07FFFh/08000h empty sentinel
%endif

; -----------------------------------------------------------------------------
; dkx_key - what tile i should look like, in one word
; in:  CX = instance index, DI = its record
; out: AX = the key, 0 = this slot has no tile
; clobbers: AX (the output) and flags - which is strictly LESS than it used to
;           (it banked BX and CX for two scratch values it no longer needs), so
;           no caller can be surprised. There is one caller.
;
; Everything dkx_draw_tile reads: the icon it blits, and the two bits that
; decide the marks around it. The icon pointer is rotated rather than added so
; that its high bits reach the low end of the word, which spreads the identity
; over the whole word instead of leaving it in the high bits alone.
;
; **That rotation is NOT what makes a freed-and-refilled slot safe, and the
; sentence that used to say so was wrong**: every package's I_ICON is
; I_ICO_PKG (loader.inc:265) or 0 (:261), so two different PACKAGES in one slot
; key identically whatever the rotation does. What actually protects the case
; is SPEC.md 30's own guarantee - every dock-state transition rides a wm_show,
; wm_front, wm_hide or wm_destroy repaint and all four call dkx_paint
; unconditionally, so the slot goes to key 0 and back, and the intermediate
; paint is the redraw.
;
; **The marks own bits 0..2 and the identity owns the rest** (SPEC.md 30.3).
; The rotation used to XOR the icon's top three bits straight into the mark
; bits, which is fine for "did anything change" and useless for "what
; changed" - so those three are FOLDED into the high byte instead of landing
; on top of the marks. Nothing is lost and one thing is gained: old XOR new
; now names the difference, and a key of 0 means 'no tile' by construction
; rather than by the bump this used to need.
; -----------------------------------------------------------------------------
dkx_key:
    xor ax, ax
    or di, di                   ; a position that shows no record (dkx_map)
    jz .out
    cmp byte [di+I_STATE], 1    ; live only - dying and free records have no
    jne .out                    ; tile (SPEC.md 29.2 rule 3)
    mov ax, [di+I_ICON]
    rol ax, 1                   ; x3 - the pointer's high bits reach the low
    rol ax, 1                   ; end. THREE 1-BIT ROTATES: 6 bytes against
    rol ax, 1                   ; `mov cl,3` + `rol ax,cl` + the push/pop of
                                ; CX they need, which is also 6 - and 6 clocks
                                ; against 20 on an 8088
    xor ah, al                  ; the WHOLE low byte folds up, not just its
                                ; bottom three - so no BL and no push bx. The
                                ; five extra bits fold into a byte that is
                                ; pure identity, which changes the hash's
                                ; spread and not its meaning: over all 65,536
                                ; I_ICON values x the four mark combinations
                                ; the collision CLASSES are bit-for-bit the
                                ; old ones (8,192 distinct keys, max class 8),
                                ; so a tile the old code noticed as changed is
                                ; noticed by this one and vice versa
    and al, (DOCK_K_ID & ~DOCK_K_LIVE) & 0FFh
    inc ax                      ; = or al, DOCK_K_LIVE: bit 0 is clear after
                                ; the AND, so this cannot carry out of AL
    test byte [di+I_FLAGS], 1
    jz .nomini
    or al, DOCK_K_MINI          ; minimized: the interior inverts
.nomini:
    cmp di, [dock_act]
    jne .out
    or al, DOCK_K_ACT           ; active: the border doubles
.out:
    ret

; -----------------------------------------------------------------------------
; dkx_tile_xy - tile i's top-left corner, on whichever edge the strip stands
; in:  CX = instance index
; out: AX = x, BX = y
; clobbers: the outputs and flags
;
; ALONG the strip by [cs:dock_astep] from [cs:dock_a0], and at [dock_c0] ACROSS it
; (SPEC.md 30.5): x and y on the bottom strip, y and x on a side.
; -----------------------------------------------------------------------------
dkx_tile_xy:
    push dx
    mov ax, cx
    mul word [cs:dock_astep]       ; index <= 11 and a step <= 28: no DX
    add ax, [cs:dock_a0]
    mov bx, [dock_c0]
    pop dx
    jmp dkx_tile_swap          ; bottom: AX along = x. A side: exchange

dkx_tile_swap:
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .out
    xchg ax, bx
.out:
    ret

; -----------------------------------------------------------------------------
; dkx_tile_rect - tile i's rect: dkx_erase_tile's own fill, dkx_px_tile's
;                  span, and SPEC.md 11.99's minimize zoom
; in:  CX = instance index
; out: AX/BX/CX/DX = x1,y1,x2,y2 inclusive
; clobbers: the outputs and flags
;
; Lifted out of dkx_erase_tile rather than open-coded a second time, for
; dkx_hit's reason (SPEC.md 30.2): where a tile IS has one answer here, not
; one per caller that can drift from the others.
;
; dkx_px_tile reads it as a SPAN, so on a side strip it hands back the along
; pair in AX/CX - see there.
; -----------------------------------------------------------------------------
dkx_tile_rect:
    call dkx_tile_xy           ; AX = tile left, BX = tile top
dkx_rect_ab:                   ; ...and the arithmetic ALONE, for a caller
                                ; that already holds the corner in AX/BX -
                                ; dkx_draw_tile's frame, and dkx_mark_ab
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov dx, bx
    add dx, DOCK_TILE_H-1
    ret

; -----------------------------------------------------------------------------
; dkx_erase_tile - white out tile i's rect
; in:  CX = instance index; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; A tile is redrawn from white because its marks are not nested: going from
; active to plain has to remove the inner frame, and going from minimized to
; plain has to un-invert an interior that a second XOR would only re-invert
; if the tile had not also been redrawn underneath it.
; -----------------------------------------------------------------------------
dkx_erase_tile:
    push ax
    push bx
    push cx
    push dx
    call dkx_tile_rect         ; AX/BX/CX/DX = the whole tile
    push cs
    call dkk_thm_bg                 ; ("clobbers: nothing", so the rect survives)
    call KERNEL_SEG:cw_gfx_fill
    jmp dkx_ret_dx

; -----------------------------------------------------------------------------
; dkx_xor_ring - toggle tile CX's ACTIVE mark   (the inner frame)
; dkx_xor_body - toggle tile CX's MINIMIZED mark (the whole interior)
; in:  CX = instance index; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; Both are exactly the rect dkx_draw_tile draws that mark in, and both are
; XOR, so they compose in either order and each undoes itself (SPEC.md 30.3).
;
; The ring is sound because the inner frame's four edges never touch the icon
; - the body is 16x16 at +4,+2 inside a 24x20 tile, clearing the frame by 2px
; and 1px - so every pixel it writes is plain background, white normally and
; black under an inverted interior. XOR is right in both cases, which is why
; the minimized state needs no branch here.
; -----------------------------------------------------------------------------
dkx_xor_ring:
    push ax
    push bx
    push cx
    push dx
    call dkx_mark_rect
    push cs
    call dkk_gfx_xor_rect
    jmp short dkx_xor_pop

dkx_xor_body:
    push ax
    push bx
    push cx
    push dx
    call dkx_mark_rect
    call KERNEL_SEG:cw_gfx_xor_fill
dkx_xor_pop:                   ; ONE epilogue: the two routines differ in a
    jmp dkx_ret_dx

; dkx_mark_rect - the tile interior both marks live in
; in:  CX = instance index
; out: AX = x1, BX = y1, CX = x2, DX = y2
dkx_mark_rect:
    call dkx_tile_xy           ; AX = tile left, BX = tile top
dkx_mark_ab:                   ; ...and the same rect from a caller ALREADY
                                ; holding the tile's corner in AX/BX, which is
                                ; dkx_draw_tile's two inner rects. No NASM
                                ; local in the body below, so nothing is
                                ; re-parented by the label.
    call dkx_rect_ab           ; THE MARK RECT IS THE TILE RECT INSET 1px on
    inc ax                      ; every side: (L+1, T+1, L+22, T+18) either
    inc bx                      ; way, and it was written out from scratch
    dec cx                      ; here rather than derived
    dec dx
    ret

; -----------------------------------------------------------------------------
; dkx_draw_tile - one tile: frame + icon (+ XOR invert when minimized)
; in:  CX = instance index, DI = instance record (I_STATE = 1); gfx lock
;      held by the caller
; out: nothing (all registers preserved; module internal)
; -----------------------------------------------------------------------------
dkx_draw_tile:
    push ax
    push bx
    push cx
    push dx
    push si

    call dkx_tile_xy           ; AX = tile left, BX = tile top
    mov si, ax                  ; SI = tile left for the duration...
    push bx                     ; ...and the top on the stack, popped below

    push cs
    call dkk_thm_ink                ; "clobbers: nothing (flags preserved)", so AX
    call dkx_rect_ab           ; and BX are STILL the corner here: the 1px
    call KERNEL_SEG:cw_gfx_frame              ; tile frame IS the tile rect, and gfx_frame
                                ; is "clobbers: flags", so the rect survives it

    mov cx, ax                  ; icon at (left+4, top+2): 16x16 body, off the
    add cx, 4                   ; rect's own x1/y1, which gfx_frame gave back
    mov dx, bx
    inc dx
    inc dx
    push si
    push cs
    call dkk_inst_icon_ptr          ; SI -> 64 bytes in OUR segment, staged if
    jnc .icon                   ; this instance's body lives in LOW_SEG
    mov si, ico_app16 + 2       ; generic fallback: skip the library
.icon:                          ; record's 2-byte header (SPEC.md 25/30)
    call KERNEL_SEG:cw_icon_draw16
    pop si
    pop bx                      ; BX = the tile top again

    cmp di, [dock_act]          ; active: a second frame just inside the
    jne .mini                   ; first, which reads as a heavier border and
    push cs
    call dkk_thm_ink                ; costs no colour (SPEC.md 39.4). The icon
    push bx                     ; body is at +4,+2 and 16x16, so it clears
    mov ax, si                  ; both new edges by 2px and 1px
    call dkx_mark_ab
    call KERNEL_SEG:cw_gfx_frame
    pop bx

.mini:
    test byte [di+I_FLAGS], 1   ; minimized: invert the tile interior
    jz .done
    mov ax, si
    call dkx_mark_ab
    call KERNEL_SEG:cw_gfx_xor_fill

.done:
    jmp dkx_ret_si

; -----------------------------------------------------------------------------
; dkx_hit - which tile is under a point? (SPEC.md 30/30.2)
; in:  CX = x, DX = y (absolute)
; out: CF = 1 the point is not in the strip at all;
;      CF = 0 the strip owns it, and DI = the LIVE instance record under the
;      pointer with AL = its slot - or DI = 0 for the bare strip, the gap
;      between two tiles, a slot past the last, and an empty or dying one.
; clobbers: AX, DI (the outputs)
;
; Lifted whole out of dkx_click when the right button wanted the same find
; (SPEC.md 30.2). Factored rather than copied because "which records have a
; tile" is one question - SPEC.md 29.2 rule 3 - and a second copy of the
; answer is a second place for a dying record to be offered a menu.
; -----------------------------------------------------------------------------
dkx_hit:
    push bx
    mov bx, dock_lx1
    call dkx_point
    jc .miss

    xor di, di                  ; in the strip, no tile yet
    cmp byte [cs:dock_hidden], 0
    jne .in                     ; the line: consumed, and no tile on it
    mov ax, cx                  ; the ALONG coordinate (SPEC.md 30.5): x on the
    cmp byte [dock_side], DOCK_P_BOTTOM
    je .along                   ; bottom strip, y on a side
    mov ax, dx
.along:
    sub ax, [cs:dock_a0]           ; before the first tile (unsigned borrow)
    jb .in
    mov bl, [cs:dock_astep]        ; slot = (along - a0) / step
    div bl                      ; AL = slot, AH = within-slot offset
    cmp ah, [cs:dock_alen]         ; in the gap between tiles?
    jae .in
    cmp al, INST_MAX            ; past the last position?
    jae .in
    call dkx_map               ; ...and which SLOT it shows (SPEC.md 30.5.1)
    mov bl, al
    xor bh, bh
    mov al, [cs:dock_pos+bx]
    cmp al, 0FFh
    je .in                      ; none
    call KERNEL_SEG:cw_inst_ptr               ; DI = record (in: AL = index; AX survives)
    cmp byte [di+I_STATE], 1
    je .in                      ; live: DI is the answer
    xor di, di                  ; empty or dying: no tile here
.in:
    clc
    pop bx
    ret
.miss:
    stc
    pop bx
    ret

; -----------------------------------------------------------------------------
; dkx_act_slot - do what tile AL's own mark says is not true yet (SPEC.md 30.4)
; in:  AL = the slot; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; **The tile is a TOGGLE**: minimized (inverted interior) -> inst_restore;
; ACTIVE (doubled border - this instance owns the frontmost visible window) ->
; inst_minimize; anything else -> wm_front. The active tile used to call
; wm_front on a window that was already frontmost, which repaints no window at
; all (SPEC.md 11.90) - a click that consumed itself and did nothing.
;
; It takes a SLOT and re-resolves the record under the lock the caller holds,
; which is where a read of a record the lock protects belongs - and it is what
; lets SPEC.md 30.6.1's tracker call it after the strip has been put away,
; with the record the press found possibly gone in between.
;
; It asks wm_top HERE rather than reading [dock_act], which is dkx_paint's
; scratch - fdlg_sel_bar's rule about [fdlg_shown] (SPEC.md 38.3). And it
; compares WINDOWS where dkx_key compares records: inst_bind_win writes I_WIN
; and wm_owner together, so the two cannot disagree.
; -----------------------------------------------------------------------------
dkx_act_slot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call KERNEL_SEG:cw_inst_ptr               ; DI = the record
    cmp byte [di+I_STATE], 1
    jne .out                    ; gone since the press: nothing to do
    test byte [di+I_FLAGS], 1
    jz .front
    push cs
    call dkk_inst_restore           ; minimized: restore (clears bit0, wm_show)
    jmp short .out
.front:
    mov cx, [di+I_WIN]          ; visible: front it - or minimize it, if it is
    jcxz .out                   ; already the front one (SPEC.md 30.4)
    push cs
    call dkk_wm_top                 ; BX = the frontmost VISIBLE window, or 0
    cmp bx, cx
    mov bx, cx                  ; mov does not touch flags: BX = our window for
    je .fmin                    ; whichever of the two calls is taken
    push cs
    call dkk_wm_front
    jmp short .out
.fmin:
    push cs
    call dkk_inst_minimize          ; sets I_FLAGS bit0 and wm_hides, so the tile
                                ; inverts (SPEC.md 30.3)
.out:
    jmp dkx_ret_di

; -----------------------------------------------------------------------------
; dkx_click - hit-test a click against the strip (SPEC.md 30)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the click was consumed (anywhere in the live strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
; -----------------------------------------------------------------------------
dkx_click:
    push ax
    push bx
    push cx
    push dx                     ; DX IS NOT SPENT HERE - it is pushed so that
    push di                     ; dkx_rclick can share the tail below, whose
                                ; pop run is ax,bx,cx,dx,di for both

    call dkx_hit               ; DI = the live instance under the pointer
    jc .miss
    or di, di
    jz .consumed                ; bare strip, gap or dead slot: consumed no-op

    call KERNEL_SEG:cw_gfx_lock
    call dkx_act_slot          ; AL = the slot dkx_hit found
    call KERNEL_SEG:cw_gfx_unlock

.consumed:                      ; --- THE SHARED TAIL. dkx_rclick jumps in
    stc                         ; HERE and at `.miss`, and both push
    jmp .out                    ; ax,bx,cx,dx,di in that order - which is now
.miss:                          ; this label's CONTRACT, enforced by nothing
    clc                         ; but the comment at both ends
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dkx_rclick - right-press on the strip: a tile's context menu (SPEC.md 30.2)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the press was consumed (anywhere in the live strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
;
; One item, 'Close': the close box of SPEC.md 13 reached from the dock. The
; strip's own background is consumed and drops NOTHING.
;
; **The record pointer does not survive the tracker; the slot does.**
; menu_popup holds the button and yields (SPEC.md 12.4), so a package worker
; can reach inst_task_die and free the very record this menu is about. So the
; SLOT is carried across it, and dkx_close_slot re-resolves it and re-tests
; I_STATE = 1 before anything is done.
;
; Unbilled, exactly like menu_track and fm_rclick (SPEC.md 13).
; -----------------------------------------------------------------------------
dkx_rclick:
    push ax                     ; the same push run dkx_click has, in the same
    push bx                     ; order, because this routine ENDS IN ITS TAIL
    push cx
    push dx
    push di

    call dkx_hit               ; DI = the live instance under the pointer
    jc dkx_click.miss
    or di, di
    jz dkx_click.consumed      ; bare strip: consumed, and no menu

    call KERNEL_SEG:cw_gfx_lock
    call dkx_ctx               ; CF = 1 nothing chosen; AL = the slot still
    jc .rdone
    call dkx_close_slot
.rdone:
    call KERNEL_SEG:cw_gfx_unlock
    jmp dkx_click.consumed     ; ...and out through dkx_click's tail, whose
                                ; five pops are this routine's five pushes

; -----------------------------------------------------------------------------
; dkx_ctx - drop the tile's one-item menu beside the strip (SPEC.md 30.2)
; in:  CX = the press x, DX = the press y, AL = the slot; gfx lock held
; out: CF = 1 nothing was chosen, CF = 0 'Close' was. Every register preserved.
;
; ANCHORED OFF THE STRIP, so it does not land on the tile it is asking about -
; an open hidden strip included, which the menu draws OVER (menu_drop puts the
; hole away while it is down):
; above a bottom strip, right of a left one and left of a right one. menu_popup
; shifts and floors whatever it is handed (SPEC.md 12.4), so DOCK_CTX_H and
; DOCK_CTX_W decide placement and nothing else.
; -----------------------------------------------------------------------------
dkx_ctx:
    push ax
    push bx
    push cx
    push dx
    cmp byte [dock_side], DOCK_P_LEFT
    je .left
    ja .right
    mov dx, [dock_ly1]          ; bottom: above the strip, at the press x
    sub dx, DOCK_CTX_H
    jmp short .drop
.left:
    mov cx, [dock_lx2]          ; left: just right of it, at the press y
    inc cx
    jmp short .drop
.right:
    mov cx, [dock_lx1]          ; right: just left of it
    sub cx, DOCK_CTX_W
.drop:
    mov bx, dock_ctx_items
    mov ax, DOCK_CTX_N
    call KERNEL_SEG:cw_menu_popup             ; CF = 1 nothing chosen
    pop dx                      ; POP spends no flag
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dkx_close_slot - 'Close' on tile AL, if its instance is still there
; in:  AL = the slot; gfx lock held, UI task (SPEC.md 29.4)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dkx_close_slot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call KERNEL_SEG:cw_inst_ptr               ; the tracker yielded: find the record again,
    cmp byte [di+I_STATE], 1    ; and take it only if it is still live
    jne .out
    mov bx, [di+I_WIN]
    or bx, bx
    jz .out
    push cs
    call dkk_app_close_win          ; in: BX; lock held, UI task (SPEC.md 29.4)
.out:
    jmp dkx_ret_di

; -----------------------------------------------------------------------------
; dkx_map - which instance SLOT each tile POSITION shows (SPEC.md 30.5.1)
; in:  nothing
; out: [cs:dock_pos] = INST_MAX bytes, a slot or 0FFh; all registers preserved
;
; A strip that holds every tile keeps SPEC.md 30's stable mapping - position i
; IS slot i, holes and all, so quitting one instance never moves another's
; tile. A side strip on a short screen holds fewer ([cs:dock_cap]: seven on CGA),
; and there the live instances PACK in slot order instead, because a hole
; would cost a tile the strip does not have. When there are still more live
; instances than tiles, the MINIMIZED ones are taken first: the strip is the
; only way back to a minimized window, and an open window can be clicked.
; -----------------------------------------------------------------------------
dkx_map:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx
.fill:                          ; identity, which is the whole answer when
    mov [cs:dock_pos+bx], bl       ; every slot has a tile
    inc bx
    cmp bx, INST_MAX
    jb .fill
    mov dl, [cs:dock_cap]
    cmp dl, INST_MAX
    jae .out

    mov al, 0FFh                ; --- packed: nothing yet
    xor bx, bx
.none:
    mov [cs:dock_pos+bx], al
    inc bx
    cmp bx, INST_MAX
    jb .none

    xor cx, cx                  ; CL = live, CH = live and minimized
    mov si, inst_tab
    xor bx, bx
.count:
    cmp byte [si+I_STATE], 1
    jne .cnext
    inc cl
    test byte [si+I_FLAGS], 1
    jz .cnext
    inc ch
.cnext:
    add si, I_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .count

    mov dh, 0FFh                ; DH = minimized still wanted, AH = others
    mov ah, 0FFh
    cmp cl, dl
    jbe .take                   ; they all fit
    mov dh, ch                  ; ...or the minimized first, up to the cap,
    cmp dh, dl                  ; and the open ones in what is left
    jbe .mq
    mov dh, dl
.mq:
    mov ah, dl
    sub ah, dh
.take:
    mov si, inst_tab
    xor bx, bx                  ; BL = the slot
    mov di, dock_pos            ; DI = the next position, in OUR image
.walk:
    cmp byte [si+I_STATE], 1
    jne .wnext
    test byte [si+I_FLAGS], 1
    jz .open
    or dh, dh
    jz .wnext
    dec dh
    jmp short .put
.open:
    or ah, ah
    jz .wnext
    dec ah
.put:
    mov [cs:di], bl
    inc di
.wnext:
    add si, I_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .walk
.out:
    jmp dkx_ret_di

; -----------------------------------------------------------------------------
; dkx_slot_rect - the rect of the tile slot CX is SHOWN at (SPEC.md 11.99's
;                  minimize zoom). A slot with no tile on a packed strip flies
;                  to the last one there is.
; in:  CX = instance slot; out: AX/BX/CX/DX = the tile; clobbers flags
; -----------------------------------------------------------------------------
dkx_slot_rect:
    push si
    call dkx_map
    xor si, si
.find:
    cmp [cs:dock_pos+si], cl
    je .have
    inc si
    cmp si, INST_MAX
    jb .find
    mov si, [cs:dock_cap]          ; (a byte, and the word's high half is masked)
    and si, 0FFh
    dec si
.have:
    mov cx, si
    pop si
    jmp dkx_tile_rect

; -----------------------------------------------------------------------------
; dkx_pass - the hidden strip's hover, and the open strip's linger
; in:  nothing; UI task, NO lock held - ui_task's tick passes (SPEC.md 13.12)
; out: nothing (all registers preserved)
;
; SPEC.md 30.6. One byte compare on a machine that does not hide its strip.
; NOTHING HERE WAITS: opening draws the strip and returns, and closing puts the
; pixels back and returns, so every task goes on running - and drawing - while
; the strip is open, round the hole the clip region makes for it (30.6.1).
; -----------------------------------------------------------------------------
dkx_pass:
    cmp byte [cs:dock_auto], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    cmp byte [dock_up], 0
    jne .open
    cmp byte [mouse_btn], 0
    jne .off
%ifdef KERN_BIG
    cmp byte [blk_sv], 0
    jne .off
%endif
    cmp word [fdlg_win], 0
    jne .off
    push cs
    call dkk_wm_fs_vis
    jc .off
    call dkx_hit
    jc .off
    mov bx, DOCK_HOVER_T
    jmp short .count
.open:
    push cs
    call dkk_wm_fs_vis
    jc .close
%ifdef KERN_BIG
    cmp byte [blk_sv], 0
    je .nosv
    call dkx_drop             ; the saver owes the repaint
    jmp short .out
.nosv:
%endif
    call dkx_in_strip
    jnc .off
    mov bx, DOCK_LEAVE_T
.count:
    mov ax, [ticks]
    cmp byte [cs:dock_timer_on], 0
    jne .elapsed
    mov [cs:dock_timer_tick], ax
    inc byte [cs:dock_timer_on]
    jmp short .out
.elapsed:
    sub ax, [cs:dock_timer_tick]
    cmp ax, bx
    jb .out
    cmp byte [dock_up], 0
    jne .close
    call dkx_open
.off:
    mov byte [cs:dock_timer_on], 0
    jmp short .out
.close:
    call KERNEL_SEG:cw_gfx_lock
    call dkx_shut
    call KERNEL_SEG:cw_gfx_unlock
.out:
    jmp dkx_ret_di
.ret:
    ret

; Open over windows without holding the lock between UI passes. All registers
; preserved. Closing repaints, so neither a save-under nor stale flag is needed.
dkx_open:
    call KERNEL_SEG:cw_gfx_lock
    mov byte [dock_up], 1
    call dkx_live_set
    call dkx_force
    call dkx_paint
    mov word [gfx_hole], 1
    jmp dkx_out_gfx_unlock

; gfx lock held; preserve the old rect before making the hidden line live.
dkx_shut:
    cmp byte [dock_up], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    call dkx_live_rect
    call dkx_drop
    call KERNEL_SEG:cw_wm_paint_dmg
    jmp dkx_ret_dx
.ret:
    ret

; Forget without drawing: geometry changes, fsx and the saver repaint later.
dkx_drop:
    mov byte [cs:dock_timer_on], 0
    cmp byte [dock_up], 0
    je .ret
    mov word [gfx_hole], 0
    mov word [wm_clip_n], 0
    mov byte [dock_up], 0
    jmp dkx_live_set
.ret:
    ret

; No region is armed. Seed the virtual desktop and subtract the dock with
; the existing rectangle splitter. At most four pieces, so this cannot overflow.
; Every register preserved; ZF = 0 (the desktop outside the dock is nonempty).
dkx_gfx_hole_arm:
    push ax
    push cx
    push dx
    push si
    call KERNEL_SEG:cw_cur_unlazy
    xor ax, ax
    xor dx, dx
    mov cx, [vid_wm1]
    mov si, [vid_hm1]
    push cs
    call dkk_wm_clip_seed
    call dkx_hole_sub
    pop si
    pop dx
    pop cx
    pop ax
    cmp word [wm_clip_n], 0
    ret

; Subtract the open strip from an existing region. wm_clip_subr already tests
; intersections and accepts an empty list. Preserve all registers; CF = overflow.
dkx_hole_sub:
    cmp word [gfx_hole], 0
    je .no
    push ax
    mov ax, [dock_lx1]
    mov [wm_cl_ox1], ax
    mov ax, [dock_ly1]
    mov [wm_cl_oy1], ax
    mov ax, [dock_lx2]
    mov [wm_cl_ox2], ax
    mov ax, [dock_ly2]
    mov [wm_cl_oy2], ax
    pop ax
    jmp dkx_out_wm_clip_subr
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; dkx_hole_win - is window DI (or BX, for dkx_hole_winb) under the open strip?
; out: CF = 1 yes - its pixels there are the strip's, so no cache may be taken
;      from or restored to them; every register preserved
; -----------------------------------------------------------------------------
dkx_hole_winb:
    push di
    mov di, bx
    call dkx_hole_win
    pop di
    ret
dkx_hole_win:
    cmp word [gfx_hole], 0
    je .no
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, di
    call KERNEL_SEG:cw_wm_win_rect
    call dkx_live_hit
    jmp dkx_ret_si                ; pops preserve the overlap answer in CF
.no:
    clc
    ret

; dkx_in_strip - CX,DX on the strip's whole rect? CF = 1 no. Preserves all.
dkx_in_strip:
    push bx
    push ds                     ; the STRIP block is OURS and the hidden
    push cs                     ; line's is the kernel's, so dkx_point reads
    pop ds                      ; whichever DS its caller hands it
    mov bx, dock_sx1
    call dkx_point
    pop ds
    pop bx
    ret

; CX,DX against the four consecutive words at DS:BX. CF = 1 outside;
; registers preserved. Used for both the hidden line and the whole strip.
dkx_point:
    cmp cx, [bx]
    jl .no
    cmp cx, [bx+4]
    jg .no
    cmp dx, [bx+2]
    jl .no
    cmp dx, [bx+6]
    jg .no
    clc
    ret
.no:
    stc
    ret


dkx_out_gfx_unlock:
    call KERNEL_SEG:cw_gfx_unlock
    ret

dkx_out_wm_clip_subr:
    push cs
    call dkk_wm_clip_subr
    ret

dkx_ret_di: pop di
dkx_ret_si: pop si
dkx_ret_dx: pop dx
    pop cx
    pop bx
    pop ax
    ret

; Kernel routines only this module calls. `push cs` + a near call to one of
; these builds the far return frame; the kernel routine's near `ret` lands on
; cw_kretf, which returns here - so no resident cw_* shim is needed.
dkx_kretf: dw cw_kretf
dkk_app_close_win:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:app_close_win
dkk_gfx_xor_rect:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:gfx_xor_rect
dkk_inst_icon_ptr:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:inst_icon_ptr
dkk_inst_minimize:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:inst_minimize
dkk_inst_restore:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:inst_restore
dkk_thm_bg:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:thm_bg
dkk_thm_ink:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:thm_ink
dkk_wm_clip_seed:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:wm_clip_seed
dkk_wm_clip_subr:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:wm_clip_subr
dkk_wm_front:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:wm_front
dkk_wm_fs_vis:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:wm_fs_vis
dkk_wm_top:
    ; STKBALANCE-NET: -1
    push word [cs:dkx_kretf]
    jmp KERNEL_SEG:wm_top
section .text
%endif
