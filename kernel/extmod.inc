; =============================================================================
; EXTD.DRV - the extended desktop's placement policy (SPEC.md 39.19.6), an
; on-demand module (SPEC.md 2.8) held for exactly as long as [vid_dmode] asks
; for Extend. kern_big's alone: kern_small has no second display at all.
;
; WHAT IS HERE is the half of SPEC.md 39.13-39.19 that runs on a DECISION -
; bringing display 1 up (vid_disp_init's Extend arm), where a window lands
; when it is put down on the other card or across the seam (39.16.3), which
; display a window is told it is on (39.16.4.1) and the fsx bracket's
; collapse and restore (39.18) - and, since the second wave, every other
; second-display ARM that runs at the rate of a user's action: the other
; card's ground in a whole-desktop repaint (39.14.4), the zoom and fullscreen
; boxes of a window's own display (39.17), the kind and geometry a window is
; told (wm_kind_now, OSAPI_WM_DISPLAY), a drag's floor on the other card
; (39.16.2) and fsx's caps and kind for the bracket's display (39.18.2).
; What is NOT here is everything that runs per
; primitive, per glyph cell or inside the mouse ISR - GFXDISP, font_ch_cut,
; cur_move_multi - because a far call under every primitive of an extended
; machine is the wrong trade, and that half is generalised in place rather
; than gated anyway.
;
; THE INVARIANT: [vid_ndisp] > 1 only while this image is mounted. The one
; routine that makes it 2 is ext_extend_b below (and vid_fsx_leave, also
; here, restoring it), so it cannot happen without the image; and exf_sync
; drops the image only once the relayout has put [vid_ndisp] back to 1. Every
; kernel caller therefore either sits behind a [vid_ndisp] test it already
; had, or is content with mod_gone's CF = 1, registers preserved, as the
; one-display answer - and the kernel calls the slot straight, with EXTCALL:
; no thunk, no mod_need on the path, nothing to hook or unhook.
;
; DS = KERNEL_SEG throughout. Kernel routines are reached through `EXK name`,
; five bytes a site and nothing per routine: exk_go at the foot reads the
; target out of the word after the call and builds the far frame, and the
; routine's own near `ret` lands on cw_kretf - dockmod.inc's device, so the
; image costs no resident shim. The two kernel routines that are a pointer
; walk (vid_ctx_ptr, vid_kind_of) have a near copy here instead, and the ones
; that have a resident far shim (cw_thm_desk, cw_dock_band,
; cw_vid_ctx_capture, and cw_vid_disp_of for the one on OSAPI_WM_DISPLAY's
; path) or a cell of their own (apic_wm_top) are far-called through it, at
; the same five bytes as an EXK and without exk_go's work. os88ovlchk
; refuses a near call either way across the boundary (its MODS list names
; `.modx`).
; =============================================================================
%ifdef KERN_BIG
section .cold
; -----------------------------------------------------------------------------
; exf_sync - EXTD.DRV follows [vid_dmode] (far)
; in:  [vid_dmode], [vid_ndisp]
; out: CF = 1 Extend was asked for and the image could not be had - no system
;      disk, no heap, a stamp - and [vid_dmode] is Single again, so the caller
;      owes the refusal (SPEC.md 2.8.4, 47). Registers preserved.
;
; The Control Panel's arrangement row calls it on BOTH sides of
; vid_disp_relayout: the first loads before anything moves, the second drops
; after the collapse - a DROP waits until [vid_ndisp] says the relayout has
; actually happened, which is the invariant above kept by construction. The
; boot settings reader calls it once, after SYSTEM.CFG's two arrangement bytes
; are stored and before the adapter switch that can extend (driver.inc
; drv_boot_x); a refusal there is Single, with the file left as it is.
; -----------------------------------------------------------------------------
exf_sync:
    push ax
    mov al, MOD_EXT
    cmp byte [vid_dmode], 0
    jne .need
    cmp byte [vid_ndisp], 1
    ja .out                     ; CF = 0: still extended, drop after relayout
    call mod_drop               ; (a drop of an empty row is a no-op)
    clc
    jmp short .out
.need:
    mov ah, [vid_avail]         ; ...but only on a machine that can EVER pair:
    not ah                      ; vid_dual_ok's fixed half, HERC and VGA|CGA -
    test ah, VID_A_HERC | VID_A_CGA ; a VGA sets the CGA bit unprobed
    jnz .out                    ; (vid_probe_avail), so HERC|CGA is the same
                                ; test. CF = 0 (test clears it) and [vid_dmode]
                                ; kept: a SYSTEM.CFG carried from a dual
                                ; machine costs this one no read and no claim
    call mod_need
    jnc .out
    mov byte [vid_dmode], 0     ; MOV keeps mod_need's CF = 1
.out:
    pop ax
    retf

section .modx
; EXK name - far-call the KERNEL routine `name` (a near proc in .text) from the
; image: the target rides in the word after the call and exk_go builds the
; frame. Five bytes a site where `push cs` + a ten-byte stub per routine was
; four, so it wins from the second routine on, and it preserves every
; register and the flags both ways exactly as the stubs did.
%macro EXK 1
    call exk_go
    dw %1
%endmacro
EXT_NENT    equ 12
modx_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER, MOD_EXT         ; MOD_H_VER, MOD_H_ID
    dw BUILD_NUM, MOD_STAMP     ; MOD_H_BUILD, MOD_H_STAMP (SPEC.md 2.8.2)
    dw modx_end, EXT_NENT       ; MOD_H_IMG, MOD_H_NENT
    dw ext_extend_b             ; EXT_EXTEND - far procs, the six whose only
    dw ui_drag_size             ; EXT_LAND     caller is their slot (or a
    dw ext_e_strads             ; EXT_STRADS   `push cs` + call in here) and
    dw ext_e_apply              ; EXT_APPLY    so end in their own retf
    dw wm_fit_box               ; EXT_FITBOX   rather than behind a
    dw ext_e_kindnow            ; EXT_KINDNOW  call/retf trampoline
    dw ext_e_fsx                ; EXT_FSX
    dw ext_e_display            ; EXT_DISPLAY
    dw wm_disp_span             ; EXT_SPAN
    dw ext_e_fsrect             ; EXT_FSRECT
    dw vid_disp_desk            ; EXT_DESK
    dw ext_e_ylow               ; EXT_YLOW

; THE TWO KINDS OF CALL SITE (SPEC.md 39.19.6). Two entries are GATED - reached
; only behind a resident `cmp byte [vid_ndisp], 1 / jbe` - because their paths
; are ones a one-display machine must not pay a far call on: EXT_DISPLAY is
; OSAPI_WM_DISPLAY, which Tracker asks at the top of every draw, and EXT_YLOW
; is the drag's release. Every other entry is UNGATED: far-called on every
; machine from a path that runs at the rate of a user's action - a window
; created, fitted, zoomed, dropped or made fullscreen, a whole-desktop repaint,
; an fsx bracket or one of its queries - where mod_gone's answer (CF = 1,
; registers kept) IS the one-display answer. The ungated ones that compute
; test [vid_ndisp] themselves as well, because the image stays mounted for as
; long as [vid_dmode] is Extend, and a primary that cannot pair (EGA)
; collapses the desktop to one display underneath it.
;
; WHAT STAYS RESIDENT of the same subject, and why: wm_disp_of and
; wm_disp_xw's arm, because wm_bord reaches them under every window's content
; rect (OSAPI_WM_CONTENT, again at the top of every draw) and wm_fs_vis under
; every bar and dock paint; fsx_surf's arm, which Tracker and DOT DELIRIUM ask
; per frame inside a bracket; ui_drag_dead and ui_drag_phase, on the drag; and
; the two secondary-click tests in ui_task, six bytes each on every press.

ext_e_strads:                   ; EXT_STRADS: CF = 1 not straddling, which is
    call wm_strads              ; also mod_gone's answer and the one-display
    retf                        ; one
ext_e_apply:                    ; EXT_APPLY: wm_apply_wh's straddle arm, the
    call wm_strads              ; two calls in one entry. CF = 1: not
    jc .r                       ; straddling, the caller's .one
    call wm_strad_fit
    clc
.r: retf
ext_e_kindnow:                  ; EXT_KINDNOW: wm_kind_now, UNGATED. The caller
    call ext_one                ; has loaded AL = [vid_kind], so one display -
    jb .r                       ; mod_gone, or this image over a collapsed
    call wm_disp_now            ; desktop - keeps the primary's kind. The
    call ext_kind_of            ; display wm_disp_now names and not
.r: retf                        ; wm_disp_of's: this decides which size a
                                ; frame is clamped against, a question about
                                ; the corner the clamps are written from, and
                                ; wm_display asks wm_disp_now too - so a
                                ; window is sized for exactly the display it
                                ; is then told it is on
ext_e_display:                  ; EXT_DISPLAY: wm_display's second arm, behind
    push di                     ; its gate. CF = 1: the window is on display
    call wm_disp_now            ; 0, whose numbers the caller loads itself
    or al, al
    stc
    jz .p
    mov di, vid_ctx + VID_CTX_SZ ; DI = display 1's record, in line: AL is 1,
                                ; there being two displays at most (below),
                                ; and an exk_ hop here would be a second far
                                ; call on a package's paint path
    mov ax, [di+VID_CTX_CW]
    mov bx, [di+VID_CTX_CH]
    mov cx, bx                  ; NO DOCK, so the last row is the last row...
    xor si, si                  ; ...and NO MENU BAR, so the band starts at 0
    mov dl, [di+VID_CTX_KIND]   ; (and the xor cleared CF)
.p: pop di
    retf
ext_e_fsrect:                   ; EXT_FSRECT: wm_fs_setrect's arm, UNGATED.
    call ext_one                ; CF = 1: one display, and the caller writes
    jb ext_e_apply.r            ; the primary's rect itself (a bare retf)
    push ax
    push si
    push di
    push es
    call ext_wctx               ; DI = the record of the display it is on
    lea si, [di+VID_CTX_VX]     ; the record's VX, VY...
    lea di, [bx+W_X]            ; ...into W_X, W_Y
    push ds
    pop es
    cld
    movsw
    movsw
    sub si, VID_CTX_VX+4-VID_CTX_CW
    movsw                       ; ...and its CW, CH into W_W, W_H
    movsw
    pop es
    pop di
    pop si
    pop ax
    clc
    retf
ext_e_ylow:                     ; EXT_YLOW: ui_ylow's arm, behind its caller's
    push ax                     ; gate - ui_drag's release, the drag path
    push bx
    mov ax, [ui_curx]           ; the drag's PROPOSED x, not W_X
    mov bx, MBAR_H              ; any row the primary's band contains, so a
                                ; point on display 0 resolves to it
    call KERNEL_SEG:cw_vid_disp_of
    or al, al
    jz .done                    ; display 0 keeps the chrome's floor, which
                                ; the caller loaded into SI. Anything else is
                                ; display 1, there being two at most (below)
    mov si, [vid_ctx + VID_CTX_SZ + VID_CTX_VY]
.done:
    pop bx
    pop ax
    retf

; ==== the fsx bracket (SPEC.md 39.18), from fsx.inc ========================
; ext_e_fsx - EXT_FSX: AH = EXTF_*, fsx.inc's five. The bracket's three read
; no flags and are no-ops on one display; the two queries are UNGATED and
; answer mod_gone's way there. ENTER is a far proc of its own, reached by the
; jcc straight from here; LEAVE and UNBLANK share the one test that makes
; them no-ops ([fsx_vndisp] <= 1: no bracket collapsed two displays) and the
; AX they bank, so LEAVE's body is in line below and UNBLANK's is entered
; with AX already pushed.
;
; vid_fsx_leave - the desktop's geometry back (SPEC.md 39.18)
; in:       nothing
; out:      the union, the primary's chrome, the records unchanged
; clobbers: flags
;
; The records were never written inside the bracket, so this is vid_apply on
; the desktop's own adapter plus 39.16's union - and NOT an unblank, which
; owes the repaint first (39.18.1).
; -----------------------------------------------------------------------------
ext_e_fsx:
    cmp ah, EXTF_LEAVE
    jb vid_fsx_enter
    cmp ah, EXTF_KIND
    jae .query
    cmp byte [fsx_vndisp], 1    ; LEAVE and UNBLANK: nothing to undo unless a
    jbe .r                      ; bracket collapsed two displays
    push ax
    cmp ah, EXTF_LEAVE
    jne vid_fsx_unblank         ; (AX banked: its pop is there)
    mov al, [fsx_vkind]         ; --- vid_fsx_leave ---
    call ext_kapply_eq          ; 40:10 BACK TO THE PRIMARY'S KIND, because a
                                ; bracket on a COLOUR second display left it
                                ; saying colour: vid_text's own arm flips it so
                                ; that the ROM's mode set - and SPEC.md 96.33's
                                ; teletype under it - reach the card the app is
                                ; on (SPEC.md 39.19.4.1). ext_extend_b is this
                                ; same three-call pattern for the same reason,
                                ; and leaving the flag is what SPEC.md 39.20's
                                ; Restart then reads
    call ext_two                ; [fsx_vndisp] > 1 is 2, two at most
    call KERNEL_SEG:cw_dock_band ; ...and the strip where the user put it (30.5)
    pop ax
.r: retf

.query:
    call ext_one                ; ...both queries: one display keeps BL/DL,
    jb .r                       ; the caller's [vid_kind]
    push ax
    cmp ah, EXTF_KIND
    jne .caps
    mov al, [fsx_wdisp]         ; EXTF_KIND: BL = the bracket display's kind
    call ext_kind_of
    mov bl, al
    pop ax
    retf

; EXTF_CAPS - fsx_caps' second-display half (SPEC.md 53.8, 39.18.2)
; in:  BX = the window to ask ABOUT, or 0 for the frontmost; DL = [vid_kind]
; out: DL = the kind of the display that window is on; everything else kept
;
; ONE DISPLAY covers a mode-setting bracket too, which is what collapsed the
; machine (39.18.3) and moved [vid_kind] to the bracket's own card - so there
; is no second test for that, and a SAME-MODE bracket, which does not
; collapse, correctly falls through and answers about the window it was
; handed. BX = 0 is an entry proc, or a caller that has no window: the
; frontmost, and the primary if nothing is visible at all.
.caps:
    push bx
    or bx, bx
    jnz .win
    call KERNEL_SEG:apic_wm_top ; OSAPI_WM_TOP's own cell is the door: the
    or bx, bx                   ; same five bytes as an EXK and no exk_go
    jz .notop
.win:                           ; (NOT ext_wctx: that needs DI banked
    EXK wm_disp_of              ; round it, and this is the one entry a
    call ext_kind_of            ; WORKER reaches - four bytes deeper on its
    mov dl, al                  ; slice for two bytes of image. The CENTRE,
                                ; then the origin, then the primary (SPEC.md
                                ; 39.17) - so a straddling window belongs to
                                ; the display holding more of it, the same
.notop:                         ; rule both fullscreens and wm_zoom ask
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; vid_fsx_unblank - the dark monitor back, AFTER the repaint (SPEC.md 39.18.1)
; in:       nothing; ENTERED FROM ext_e_fsx with AX pushed and [fsx_vndisp]
;           already known to be 2
; out:      every card lit again; [fsx_vndisp] spent
; clobbers: flags
;
; AFTER wm_paint_all AND NOT BEFORE IT. Blanking gates the video SIGNAL and
; not memory, so a blanked card's framebuffer still holds the desktop as it
; was when the bracket started - minutes stale, on a monitor the user has not
; been looking at. Unblanking first shows that frame and then paints over it;
; repainting first means the card is only ever shown what is current.
;
; NOT A LOOP: [fsx_vndisp] > 1 is 2 (VID_NDISP_MAX, the %if below), so the
; one card that is not the bracket's is display ([fsx_vdisp] xor 1).
; -----------------------------------------------------------------------------
vid_fsx_unblank:                ; (no DI: ext_kind_of banks it and the blank
    dec byte [fsx_vndisp]       ; 2 -> 1, one-shot: a bracket that set no mode and a
    call ext_okind              ; second restore must not write these ports
    EXK vid_unblank_kind        ; pair never names it)
    pop ax
    retf

; -----------------------------------------------------------------------------
; vid_fsx_enter - one display, and the other dark (SPEC.md 39.18)
;
; in:       AL = the display the bracket runs on
; out:      the machine describes itself as having ONE display, that one, at
;           the virtual origin; the other card is blanked
; clobbers: flags
;
; INSIDE A BRACKET THE MACHINE IS SINGLE-DISPLAY AGAIN, and that is the whole
; design: every SPEC.md 53 contract - fsx_mode, the FSI block, fsx_wait,
; FSXF_KEEPWORKER, FSXF_FASTTICK - is then exactly what it is today, and no
; fsx app needs a line.
;
; [vid_kind] MOVES TO THAT DISPLAY'S ADAPTER, which is what makes it work
; rather than merely look right: fsx_caps, fsx_mode and vid_setmode all read
; it, and vid_apply then republishes the live block, the desktop words and
; 39.16's chrome extent from vid_tab - one call, no second opinion.
;
; The other is BLANKED because nothing can maintain it: the kernel does not
; run inside the bracket (53.1), so what a live card would show is a frozen
; desktop. 39.11.4's argument, unchanged. Two displays at most, so "every card
; but the bracket's" is display (AL xor 1) and no loop.
; -----------------------------------------------------------------------------
vid_fsx_enter:
    call ext_one
    jb .out                     ; one display: nothing here applies
    push ax                     ; (no DI, as vid_fsx_unblank)
    mov [fsx_vdisp], al         ; [fsx_vdisp] = AL, and in ONE word store
    mov ah, 2                   ; [fsx_vkind] = the DESKTOP's adapter, to come
    mov al, [vid_kind]          ; back to, and [fsx_vndisp] = 2 = [vid_ndisp]:
    mov [fsx_vkind], ax         ; more than one, and two at most

    call ext_okind              ; ...dark the card that is not the bracket's
    EXK vid_blank_kind

    mov al, [fsx_vdisp]
    call ext_kind_of            ; ...and become that one: the live block, the
    call ext_kapply             ; desktop words and the chrome, all of it that
                                ; display's
    xor ax, ax                  ; consistent with the lie: at the virtual
    mov [vid_ox], ax            ; origin, one display, display 0 current (the
    mov [vid_oy], ax            ; pair is one word, vidsel.inc's %error)
    inc ax
    mov [vid_ndisp], ax
    pop ax
.out:
    retf

; ==== which display, from wm.inc, fsx.inc and vidsel.inc ===================
%if VID_NDISP_MAX != 2
  %error "ext_e_display names display 1's record in line: one secondary"
%endif
%if fsx_vndisp - fsx_vkind != 1
  %error "vid_fsx_enter banks [fsx_vkind] and [fsx_vndisp] in ONE word store"
%endif
; -----------------------------------------------------------------------------
; ext_kind_of - AL = the adapter kind of display AL (vid_kind_of's near copy)
; ext_ctx_ptr - DI = the record of display AL (vid_ctx_ptr's), AX and DX kept
; in:       AL = display index, 0 or 1
; clobbers: flags; ext_kind_of banks DI, as vid_kind_of does
;
; NEAR COPIES of two kernel routines rather than far hops to them: each is a
; pointer walk shorter than the frame a hop builds, with seventeen sites here
; between them, and the copy is what keeps OSAPI_WM_DISPLAY's path (EXT_DISPLAY
; -> wm_disp_now -> wm_strads, wm_disp_rest) free of a far frame per display.
; -----------------------------------------------------------------------------
; ext_one - CF = 1 when there is ONE display ([vid_ndisp] < 2); every register
; kept. The test the entries open with, at two bytes a site less than the
; compare it stands for; wm_strads, on OSAPI_WM_DISPLAY's path, keeps its own.
ext_one:
    cmp byte [vid_ndisp], 2
    ret

; ext_okind - AL = the kind of the display that is NOT the bracket's
; ([fsx_vdisp] xor 1, two displays at most): vid_fsx_enter's blank and
; vid_fsx_unblank's unblank, falling into ext_kind_of
ext_okind:
    mov al, [fsx_vdisp]
    xor al, 1
ext_kind_of:
    push di
    call ext_ctx_ptr
    mov al, [di+VID_CTX_KIND]
    pop di
    ret
; ext_wctx - DI = the record of the display window BX is on, and AL = that
; display (wm_disp_of, then ext_ctx_ptr fallen into); clobbers AX and flags
ext_wctx:
    EXK wm_disp_of
ext_ctx_ptr:
    mov di, vid_ctx             ; two displays (the %if above): the index is
    or al, al                   ; 0 or 1, so the multiply is a branch
    jz .out
    add di, VID_CTX_SZ
.out:
    ret

; ext_org_disp - AL = the display window BX's ORIGIN is on (vid_disp_of of
; W_X, W_Y), and ZF = 1 when that is the primary; every other register
; preserved. The question wm_fit_box, wm_sf_lim and wm_disp_now's
; one-display arm all ask.
ext_org_disp:
    push bx
    mov ax, [bx+W_X]
    mov bx, [bx+W_Y]
    call KERNEL_SEG:cw_vid_disp_of
    pop bx
    or al, al
    ret

; -----------------------------------------------------------------------------
; wm_disp_span - that display's standard-state box (SPEC.md 39.17.2)
; in:       BX = the window; AX/CX/SI/DX = the primary's (x0, width, y0,
;           band height), already loaded
; out:      AX = x0, CX = width, SI = y0, DX = band height (one pixel short,
;           wm_fit's drop-shadow rule)
; clobbers: AX, CX, SI, DX, flags
; -----------------------------------------------------------------------------
wm_disp_span:                   ; (far: EXT_SPAN is its only caller)
    call ext_one
    jb .out                     ; one display: the caller's numbers stand
    push di
    call ext_wctx
    or al, al                   ; (the movs leave the flags for the jz)
    mov ax, [di+VID_CTX_VX]
    mov cx, [di+VID_CTX_CW]
    jz .prim                    ; the PRIMARY keeps the caller's band: bar to
                                ; dock, which its context record cannot know
    mov si, [di+VID_CTX_VY]     ; a secondary has no bar and no dock, so its
    mov dx, [di+VID_CTX_CH]     ; whole height is desktop - wm_fit_box's own
    dec dx                      ; rule (SPEC.md 39.16.1), one short like it
.prim:
    pop di
.out:
    retf

; -----------------------------------------------------------------------------
; vid_disp_desk - the desktop ground of every display but the primary
;                 (SPEC.md 39.14.4)
; in:       nothing
; out:      CF = 0 it painted one; CF = 1 there is one display and nothing to
;           paint - mod_gone's answer too, which wm_paint_all's fullscreen
;           arm reads in place of the [vid_ndisp] test it used to make
; clobbers: flags
;
; A secondary display gets its ground WHOLE, because SPEC.md 39.14's chrome -
; the menu bar, the dock, the drive column - lives on the primary alone.
;
; **IT IS thm_desk AND NOT gfx_fill_gray, AND THE NAME IS WHY IT WAS NOT**
; (SPEC.md 76.2.1). This routine was called vid_disp_dither, SPEC.md 76.1's own
; list of the four themed desktop sites NAMES it, and 76.2 says thm_desk
; replaced gfx_fill_gray at all of them - and this one kept the dither, so
; switching to Dark left the second monitor in Bright's ground for ever. A
; routine whose name says DITHER does not read as a themed site to whoever is
; converting them.
;
; wm_paint_all draws display 0's band itself - menu bar to dock, the call that
; has always been there - and this is the rest: ONE ground fill in VIRTUAL
; coordinates, and GFXDISP is what puts it on the right card. This is the only
; caller in the kernel that draws in virtual coordinates. Everything else is
; inside the chrome or inside a window, and both are on display 0, where
; virtual and local are the same numbers (39.14.4).
; -----------------------------------------------------------------------------
vid_disp_desk:
    call ext_one
    jb .r                       ; CF = 1: one display
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, 1                   ; NOT A LOOP: VID_NDISP_MAX is 2 (the %if at
                                ; its equ), the guard above has already
                                ; returned for [vid_ndisp] <= 1, and display 0
                                ; is wm_paint_all's own - so the general loop
                                ; ran exactly once by construction
    EXK vid_ctx_rect            ; AX/BX/CX/DX = its inclusive virtual rect
    call KERNEL_SEG:cw_thm_desk ; the THEME's ground (SPEC.md 76.2), not a
                                ; hard-coded dither
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
.r: retf                        ; (far: EXT_DESK is its only caller)

; ==== the drop, the landing and the straddle (SPEC.md 11.100.3/.4, 39.16.3) ==
; -----------------------------------------------------------------------------
; ui_drag_size - EXT_LAND: what size should a dropped window have, and has the
;                adapter under it changed? (SPEC.md 11.100.3/.4, 39.16.3.3)
; in:       BX = the window, W_X/W_Y already clamped to where it was dropped
; out:      CF = 0 if W_X, W_Y, W_W or W_H changed, CF = 1 if none did - the
;           INVERSE of the natural sense, so that mod_gone's CF = 1 reads as
;           "not moved" and ui_drag's `cmc` after the EXTCALL turns it back.
;           Every register preserved. (Far: EXT_LAND is its only caller.)
;
; It is what was wm_land_fit (with wm_apply_size_wh and wm_land_snap) inside
; the comparison ui_drag_size made round it, in one body: the four words the
; answer compares against are on the stack, so every register is free.
;
; TWO THINGS, and they are for the two halves of the tree. Every window gets
; 11.100.3: the size it WANTS comes back out of the natural bank, so the clamps
; below re-derive from that rather than from whatever the last drag's clamp
; left behind - which is what makes a walk across the seam and home again
; reversible instead of costing sixty rows a trip.
;
; A window WITHOUT a grow box also gets 11.100.4: landing wholly on a display of
; a different adapter takes its own declared size for that adapter. One WITH a
; grow box does not, and that is the rule rather than an optimisation - its
; size is one the user chose, and a user outranks an application.
;
; **THE RULE IS ABOUT ADAPTER IDENTITY AND NOT ABOUT GEOMETRY**, which is the
; whole of docs/plans/completed/WINDOW-SIZING-PLAN.md 12 and the correction of
; everything above it. What a window wants is not "a size that fits the box the
; drag left me in" but "my size for adapter kind Y, and I am standing on
; adapter kind Y" - so the TRIGGER is a change of kind and nothing else. A move
; within one adapter changes no size at all, however far it goes and however
; much of the frame ends up past the bottom of the screen.
;
; Three separate mechanisms used to share this job and each produced its own
; defect. ui_drag restored the NATURAL BANK on every release, so a drag from
; one place to another inside the CGA handed the window its HERCULES size; the
; straddle cut geometrically to the rows both displays have, which is an
; arbitrary number rather than a designed one and turned Missile Command into
; a letterbox; and a landing wholly on the short display was clamped by
; nothing at all, and then (11's withdrawn 3.6) by a rule derived from the
; union's BOUNDING BOX, which reads rows no display has as a hole in the
; desktop.
;
; wm_kind_now's answer decides - including the straddle, where it is the MORE
; RESTRICTIVE of the two displays, which is what makes a Hercules-to-CGA round
; trip exactly two resizes instead of four: one entering the straddle and one
; leaving it on the far side. The straddle test is why this is not simply
; "the kind changed": a frame still over both cards belongs to 39.16.3, and a
; preference applied there would be cut by it in the same breath.
;
; THE SNAP AND THE HANG ARE THE SAME MECHANISM SEEN FROM TWO SIDES (12.4). A
; window whose floor beats the room has its origin moved up until the frame
; ends on the last row the display has - because a user cannot SEE a window's
; floor while dragging it, so a placement that floors out crossing a seam was
; never an informed one. A move within one adapter never reaches here, so it
; hangs, and that is informed.
;
; AND A KERNEL-INITIATED RESIZE ALWAYS ASKS (SPEC.md 11.1). wm_ask_size is put
; the size this arrives at and answers with the one the window will accept -
; per axis, so a shrink that would lose columns and not rows still gets its
; rows - and a REFUSAL means the window keeps its size and HANGS rather than
; being cropped. apps/paint is the consumer and it answers out of its own
; canvas state rather than the record, which is why the ask can follow the
; write instead of having to precede it.
; -----------------------------------------------------------------------------
ui_drag_size:
    call ext_one                ; one display - mod_gone's case, or this image
    jb vid_disp_desk.r          ; over a collapsed desktop: a drag can never
                                ; change a kind, and CF = 1 is "not moved"
    push ax
    push cx
    push dx
    push si
    push di
    push word [bx+W_H]          ; what it had, so the answer is a comparison
    push word [bx+W_W]          ; rather than a promise - the ORIGIN too,
    push word [bx+W_Y]          ; because the snap below can move a window
    push word [bx+W_X]          ; that kept its size
    call wm_disp_now            ; **THE DISPLAY IS DECIDED ONCE, HERE** (SPEC.md
    mov [wm_lf_disp], al        ; 39.16.4.2). Armed before the first question is
    mov [wm_lf_win], bx         ; asked and spent at .fin, so the resize this
                                ; routine applies cannot change the answer under
                                ; the steps that follow it
    call ext_kind_of            ; the kind it stands on now (wm_kind_now's own
    cmp al, [bx+W_PKIND]        ; answer, the latch being armed) against the
    je .fin                     ; kind it was last SIZED for
    call wm_strads
    jc .land
    call wm_strad_fit           ; ...across the seam: the constrained axis
    jmp short .ask              ; ONLY, or wm_fit's x clamp would yank the frame
                                ; wholly onto one display and undo the drag

    ; --- landed on one display: the size half of wm_apply_size -------------
    ; wm_apply_size restores the natural rect WHOLE, which is right when the
    ; screen changed under a window nobody has touched and wrong the moment a
    ; hand is on it: the origin is the user's and only the size is in
    ; question. The third row of 12.5's table is here rather than in wm_fit -
    ; a window that is not sizable and has published nothing for this adapter
    ; is left ALONE, because the kernel cannot see a layout and shortening a
    ; fixed one buys nothing and costs a rect nothing will repaint (39.16.3.1)
.land:
    call ext_wants              ; the size it ASKED for, not what a clamp left,
    jnc .fit                    ; or the one it published for this adapter
    call wm_reflows
    jnc .ask
.fit:
    EXK wm_fit
.ask:
    mov cx, [bx+W_W]
    mov dx, [bx+W_H]
    EXK wm_ask_size             ; the window's own veto (SPEC.md 11.1)
    mov [bx+W_W], cx
    mov [bx+W_H], dx

    ; --- ...and the origin follows whatever it ended up with ---------------
    ; The floor outranks the box (11.100.2), so a window whose minimum is
    ; taller than the room left below the pointer cannot honour both - and
    ; this is the half that keeps it whole: pos = min(pos, limit - size),
    ; floored at the near edge of the band. A frame too big for the band at
    ; all lands at the near edge and hangs from there, which is the best
    ; available answer for a window that REFUSED to shrink (apps/paint with a
    ; picture in it): nothing is cropped and as much of it as the display has
    ; is on screen. The limit is the nearer of the two far edges while the
    ; frame straddles and the landing display's own otherwise (wm_sf_lim);
    ; wm_fit already did this once with the pre-ask size, and it is done again
    ; because the ask may have given the size back. ui_ylow answers the same
    ; question and may not be used: it reads the DRAG's proposed x, which is
    ; meaningless on the ui_grow path.
    mov ax, [vid_band_x0]       ; the NEAR edges of the display it is on -
    mov si, MBAR_H              ; the primary's band, unless wm_fit_box says
    BLOBCALL wm_fit_box         ; the origin is on the other card, whose own
                                ; vx/vy they are then (39.16.2). BLOBCALL is
                                ; `push cs` + a near call - the far frame its
                                ; RETF wants, in the spelling os88ovlchk reads
    mov cx, si                  ; (the y0, across the limit)
    call wm_sf_lim              ; DL = the axis, SI = one past the last row
                                ; (column) available, DI = the record moved
                                ; on to the y words for the y axis (W_Y =
    jz .sx                      ; W_X+2, W_H = W_W+2), and AX = that axis's
    mov ax, cx                  ; near edge
.sx:
    mov cx, si
    sub cx, [di+W_W]
    cmp [di+W_X], cx
    jle .sfl
    mov [di+W_X], cx            ; may go NEGATIVE for a frame bigger than the
.sfl:                           ; band, and the floor below takes it straight
    cmp [di+W_X], ax            ; back - which is the REFUSAL case landing at
    jge .snapped                ; the near edge and hanging from there
    mov [di+W_X], ax
.snapped:
    EXK wm_kind_bank
    EXK wm_sz_notify            ; TOLD EVEN IF THE BOX DID NOT MOVE: the ADAPTER
                                ; did, and half the tree caches facts about it
                                ; (SPEC.md 11.98). wm_refit has always done this
                                ; and the drag path never did. IT IS INSIDE THE
                                ; LATCH: the handler asks OSAPI_WM_DISPLAY and
                                ; has to be told the card the kernel just sized
                                ; it for, not the one its new frame sits on
.fin:
    xor ax, ax                  ; ...and 0 is no window, wm_wins starting well
    mov [wm_lf_win], ax         ; above it
    push es                     ; ZF = 1 exactly when all four words are what
    push ss                     ; they were: the four on the stack (X lowest,
    pop es                      ; the record's own order) against the record
    mov di, sp
    cld                         ; BEFORE the scasw, which steps DI by DF too
    scasw                       ; (DI past the ES just banked; AX = 0 is not
    lea si, [bx+W_X]            ; read for anything)
    mov cx, 4
    repe cmpsw
    pop es
    pop cx                      ; the four words, spent (pop touches no flag)
    pop cx
    pop cx
    pop cx
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    stc                         ; CF = 1: NOT moved
    jz .r
    clc
.r: retf

; -----------------------------------------------------------------------------
; wm_sf_axis - DL = 1 the displays tile SIDE BY SIDE (y is what has to come
;              in), 0 STACKED (x does)
; out:      DL; every other register preserved
; clobbers: DL, flags
;
; Side by side (39.19.2's Right) they are contiguous in x and overlap in y - so
; spanning x is the whole point of extending and must stay free, while y is
; what has to come in. Stacked (Below) it is the other way round. The test is
; geometric rather than a read of [vid_dlay]: display 0 is at the virtual
; origin by construction, and ext_extend_b - the only writer of display 1's
; VX/VY - puts display 1 at VX = 0 exactly when it is Below (and at VY =
; display 0's height, which is what this compared until it compared VX with
; one instruction instead of three).
; -----------------------------------------------------------------------------
wm_sf_axis:
    cmp word [vid_ctx + VID_CTX_SZ + VID_CTX_VX], 1
    sbb dl, dl                  ; DL = -1 Below (VX = 0), 0 Right...
    inc dl                      ; ...so 0 Below and 1 Right
    ret

; -----------------------------------------------------------------------------
; wm_sf_lim - SI = one past the last row (column) BOTH displays have
; in:       BX = the window
; out:      SI; DL = the axis (it calls wm_sf_axis itself, which both its
;           callers wanted first); DI = the window's record, moved on two
;           bytes for the y axis (W_Y = W_X+2, W_H = W_W+2), and ZF = 1 for
;           the x axis - the pointer and the test BOTH callers made next;
;           every other register preserved
; clobbers: SI, DL, DI, flags
;
; The nearer of the two far edges while the frame straddles, and the display
; the window is ON otherwise - so this one routine bounds a straddle and a
; landing alike. A record's far edge is VX + CW, or VY + CH for the y axis -
; and VY = VX+2, CH = CW+2, so DI is moved on two bytes and one pair of
; instructions reads either.
; -----------------------------------------------------------------------------
%if VID_CTX_VY != VID_CTX_VX+2 || VID_CTX_CH != VID_CTX_CW+2
  %error "wm_sf_lim reads a record's y words two bytes past its x words"
%endif
wm_sf_lim:
    call wm_sf_axis             ; DL = the axis
    push ax
    mov di, vid_ctx
    or dl, dl
    jz .ax
    inc di
    inc di
.ax:
    mov ax, [di+VID_CTX_SZ+VID_CTX_VX] ; the nearer of the two far edges, if
    add ax, [di+VID_CTX_SZ+VID_CTX_CW] ; it straddles (wm_strads keeps AX)
    call wm_strads
    jnc .edge
    call ext_org_disp           ; not straddling: the display its origin is on
    jz .one
    add di, VID_CTX_SZ
.one:
    mov ax, -1                  ; ...and nothing to be nearer than
.edge:
    mov si, [di+VID_CTX_VX]
    add si, [di+VID_CTX_CW]
    cmp ax, si
    jae .out
    mov si, ax
.out:
    pop ax
    mov di, bx                  ; ...and the record, at that axis's words
    or dl, dl
    jz .r                       ; ZF = 1: x
    inc di                      ; ZF = 0: a record pointer plus two is never
    inc di                      ; zero
.r: ret

; -----------------------------------------------------------------------------
; wm_strads - does this window's frame reach BOTH displays?
; in:       BX = the window
; out:      CF = 0 it straddles, CF = 1 it does not (a one-display machine
;           included); every register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_strads:
    push ax
    push di
    mov di, vid_ctx             ; (a MOV: the cmp below decides the flags)
    cmp byte [vid_ndisp], 2     ; (its own compare and not ext_one: this is
    jb .out                     ; OSAPI_WM_DISPLAY's path. CF = 1: one display
    call wm_sf_reach
    jc .out
    add di, VID_CTX_SZ
    call wm_sf_reach
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_strad_fit - a window across the seam takes the size for the MORE
;                RESTRICTIVE of the two displays (SPEC.md 39.16.3)
; in:       BX = the window, known to straddle and known to have changed kind
; out:      nothing; every register preserved
; clobbers: flags (its two callers set their own CF after it)
;
; WHAT IS WRONG WITH A STRADDLING WINDOW is not the straddle - SPEC.md 39.14's
; split draws one fragment per display and that works. It is that the virtual
; desktop is a UNION and not a rectangle (39.2.1), so beyond the seam the two
; displays stop agreeing about which rows exist: a 720x348 Hercules beside a
; 640x200 CGA leaves rows 220..347 present on the left and absent on the
; right. A window tall enough to use them, dragged until its right-hand edge
; is over the CGA, has its bottom-right corner in the DEAD ZONE - drawn
; nowhere, clickable nowhere, and containing whatever the app put there.
;
; **THE SIZE COMES FROM THE ADAPTER AND THE CLAMP MERELY BOUNDS IT.** This
; routine used to do only the second half, which is why it produced a number
; nobody had designed: the size it arrived at was whatever the geometry left,
; so a game whose playfield IS its rules came out a letterbox. Now the natural
; size or the published preference is taken FIRST, and the clamp is what stops
; the answer reaching into the dead zone.
;
; IT NEVER MOVES THE ORIGIN - ui_drag_size's snap owns that, after the window
; has had its say about the size. "The more restrictive size" is a statement
; about size.
;
; AND IT ONLY SHORTENS A WINDOW THAT WILL LAY ITSELF OUT AGAIN (39.16.3.1).
; The frame is not the pixels: the gfx primitives clip to the SCREEN and not
; to a window (SPEC.md 11.3), so a fixed layout drawn against remembered
; offsets puts exactly the same pixels on the glass whatever this writes into
; W_H - the rows below the shortened frame are still drawn, they are simply no
; longer inside anything the window manager thinks the window owns. So the
; shrink buys nothing at all there and costs a rect nothing will ever repaint.
; wm_reflows is the question, and it is a NARROWER question than it was: an
; 11.98 handler means "tell me when my box moved", not "you may pick any box
; for me" (12.5), so a non-sizable window opts in by PUBLISHING A PREFERENCE
; instead - a promise about three boxes rather than about every box.
;
; ONE CLAMP FOR BOTH AXES: DI is the record, moved on two bytes for y (W_Y =
; W_X+2, W_H = W_W+2), and wm_min_ax reads the axis out of DL itself.
; -----------------------------------------------------------------------------
%if W_Y != W_X+2 || W_H != W_W+2
  %error "ui_drag_size and wm_strad_fit read a record's y words two bytes on"
%endif
wm_strad_fit:
    push ax
    push cx
    push dx
    push si
    push di

    call ext_wants              ; the size it ASKED for, or the one it
%ifndef NOSIZESNAP              ; published for this adapter
    pushf                       ; ...AND BACK ONTO A BYTE BOUNDARY (SPEC.md
    mov ax, [bx+W_W]            ; 39.16.3.4). Both calls above hand back an ASK
    EXK wm_snap_w               ; - a bank holds what the window wanted, never
    mov [bx+W_W], ax            ; a snap - so the width that arrives here is
    popf                        ; 11.94.5-unrounded. Every other path that
%endif                          ; settles a size ends in wm_snap_win; this one
                                ; deliberately does not call wm_fit, which is
                                ; how it inherited the clamp and not the snap
                                ; and landed a straddling Disk window 2px thin.
                                ; BEFORE the clamp, so that a STACKED straddle
                                ; can still narrow x afterwards - rounding up
                                ; after an x clamp hands back dead zone. The
                                ; pushf/popf is wm_pref_now's CF, which the
                                ; jnc below owes
    jnc .clamp
%ifndef STRAD_ALL               ; ...and `make STRAD=all` is the A/B: no test,
    call wm_reflows             ; so every straddling window is shortened,
    jnc .done                   ; which is exactly what 39.16.3.1 reports
%endif                          ; rather than an imitation of it
.clamp:
    call wm_sf_lim              ; DL = the axis that has to come in, SI = one
                                ; past the last row (column) both displays
                                ; have, DI = the record at that axis's words
    mov ax, si                  ; AX = the floor's cap: the limit less the
                                ; TOP OF THE BAND on y rather than less W_Y,
    jz .x                       ; which is what makes it provable - a frame
                                ; that big can only start at the top, and the
                                ; snap floors it there. On x, the limit
    sub ax, MBAR_H
.x:
    mov cx, si                  ; the room from where the window stands...
    sub cx, [di+W_X]
    jg .room
    xor cx, cx
.room:
    cmp [di+W_W], cx
    jbe .fl
    mov [di+W_W], cx
.fl:
    EXK wm_min_ax               ; ...and the floor cannot be cut through
    cmp [di+W_W], ax
    jae .done
    mov [di+W_W], ax
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; ext_wants - wm_nat_take_wh then wm_pref_now: the size the window ASKED for,
; or the one it published for the adapter it stands on; CF as wm_pref_now's.
; The pair both the landing and the straddle start from.
ext_wants:
    EXK wm_nat_take_wh
    EXK wm_pref_now
    ret

; -----------------------------------------------------------------------------
; wm_sf_reach - does window BX's frame overlap the display DI describes?
; in:       BX = the window, DI = the display record
; out:      CF = 0 it overlaps, CF = 1 it does not
; clobbers: AX, flags - CX and DX are LIVE in the caller, and AX is the one
;           register each test needs: the window's far edge against the
;           record's near one, then the record's far edge against the
;           window's near one
; -----------------------------------------------------------------------------
wm_sf_reach:
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    cmp ax, [di+VID_CTX_VX]
    jbe .no                     ; wholly left of it
    mov ax, [di+VID_CTX_VX]
    add ax, [di+VID_CTX_CW]
    cmp [bx+W_X], ax
    jae .no                     ; wholly right of it
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    cmp ax, [di+VID_CTX_VY]
    jbe .no                     ; wholly above it
    mov ax, [di+VID_CTX_VY]
    add ax, [di+VID_CTX_CH]
    cmp [bx+W_Y], ax
    jae .no                     ; wholly below it
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wm_reflows - will this window DRAW ITSELF INTO a box the kernel shortens?
;              (SPEC.md 39.16.3.1)
; in:       BX = the window
; out:      CF = 1 yes, it re-derives its layout from the live content box;
;           CF = 0 no, so its height is not to be reduced. Every register
;           preserved.
;
; THE KERNEL CANNOT SEE A LAYOUT, so it can only ask what the window has
; already said, and two things say it. WF_SIZABLE is a grow box, which moves
; the content box on the user's whim - an app that takes one has no choice but
; to lay out from the live one, and every one in this tree does (apps/telnet
; says so in as many words). SPEC.md 11.98's size-CHANGED handler is the other
; and covers the app that is NOT resizable and still follows: apps/calc is not
; sizable at all and re-folds its history pane on the notice, which is exactly
; the case this rule must not throw away.
;
; WF_KEEPH OUTRANKS BOTH, because it is the window saying the opposite in the
; first person (SPEC.md 11.93) - and apps/browser is both sizable and KEEPH,
; so the order is not academic.
;
; It is a question about the WINDOW and not about the geometry, so it is asked
; before any of wm_strad_fit's arithmetic: the cheapest way to not shorten a
; window is to not work out by how much.
; -----------------------------------------------------------------------------
wm_reflows:
    test byte [bx+W_FLAGS], WF_KEEPH
    jnz .no                     ; "not the kernel's to reduce" - and it is the
                                ; FIRST test for browser's sake, which is
                                ; sizable AND keeps its height
    test byte [bx+W_FLAGS], WF_SIZABLE
    jz .no                      ; (TEST clears CF: .no is CF = 0 already)
    stc
.no:
    ret

; -----------------------------------------------------------------------------
; wm_fit_box - EXT_FITBOX: the box for the display this window's ORIGIN is on
;              (SPEC.md 39.16.1)
; in:       BX = the window; AX/CX/SI/DI/DX = the primary's box and hard floor
; out:      the same five, for that display
; clobbers: AX, CX, DX, SI, DI, flags. FAR: its slot and ui_drag_size's
;           `push cs` + call are its callers.
;
; The ORIGIN and not the centre, deliberately: wm_disp_of's centre test is for
; deciding which monitor an app OWNS (docs/plans/completed/DUAL-DISPLAY-PLAN.md
; 6.5.1), and this is deciding which edges a frame may not pass - which is a
; question about the corner the clamps are written from. A window whose origin
; is in the dead zone gets the primary, the only answer that cannot leave it
; unreachable.
;
; ON THE PRIMARY THE CALLER'S FIVE ARE THE ANSWER, and it is handed them back
; untouched: menu bar to dock, the band's x extent a side strip narrows
; (SPEC.md 30.5) and its own height as the floor. A secondary has no menu bar
; and no dock, so its whole extent is desktop, and DX IS AN OUTPUT TOO (SPEC.md
; 11.93) - the bottom a window that will not fit the band may hang down to,
; which there IS ye. Two displays at most, so "not the primary" is display 1.
; -----------------------------------------------------------------------------
wm_fit_box:
    push ax                     ; (NO ext_one: with one display - a collapsed
    call ext_org_disp           ; desktop under this image - vid_disp_of asks
                                ; display 0 alone and answers 0 either way, so
                                ; the test bought nothing but its five bytes on
                                ; a fit). ZF = the origin is on the primary
    pop ax
    jz .out                     ; display 0: the primary's, as loaded
    mov ax, [vid_ctx + VID_CTX_SZ + VID_CTX_VX]
    mov cx, ax
    add cx, [vid_ctx + VID_CTX_SZ + VID_CTX_CW]
    mov si, [vid_ctx + VID_CTX_SZ + VID_CTX_VY]
    mov di, si
    add di, [vid_ctx + VID_CTX_SZ + VID_CTX_CH]
    mov dx, di
.out:
    retf

; -----------------------------------------------------------------------------
; wm_disp_now - WHICH DISPLAY IS THIS WINDOW'S? (SPEC.md 39.16.4.1)
; in:       BX = the window
; out:      AL = the display index; every other register preserved
; clobbers: AX, flags
;
; **ONE ANSWER, because two of them disagreed and the field found it.** A
; straddling window is SIZED for the more restrictive of the two displays
; (WINDOW-SIZING-PLAN 12.2) and was TOLD about the one its origin sits on - so
; the Task Manager straddling a Hercules and a CGA was handed the CGA's frame
; and the Hercules' geometry, worked out that one column fits, and stayed one
; column in a window twice that wide. Both callers come here now.
;
; Not straddling - or in the dead zone, where nothing does - the origin is the
; only answer that cannot leave a window unreachable (39.17).
; -----------------------------------------------------------------------------
wm_disp_now:
    cmp [wm_lf_win], bx         ; **A LANDING DECIDES THIS ONCE** (SPEC.md
    jne .ask                    ; 39.16.4.2): ui_drag_size arms the latch with
    mov al, [wm_lf_disp]        ; the answer it acted on, because the RESIZE it
    ret                         ; then applies can stop the straddle being
.ask:                           ; true - and every step after it re-derived
    push dx                     ; the answer from the new geometry
    call wm_strads
    jnc .rest                   ; (taken only when it straddles: the common
    call ext_org_disp           ; case falls through, where the `jc .one`
    pop dx                      ; this replaced was a taken branch)
    ret

; wm_disp_rest, IN LINE (its one caller is the line above) - which of the two
; displays is the MORE RESTRICTIVE? out AL = 0 or 1, the display index.
;
; A STRADDLING WINDOW READS AS THE SMALLER CARD, which is what makes the seam
; one transition in each direction rather than two: dragging on from the
; straddle to the small display is then not a change of kind at all, so
; nothing resizes twice (WINDOW-SIZING-PLAN 12.2).
;
; The comparison is the USABLE BAND and not the raw extent, because the
; primary carries the menu bar and the dock and a secondary carries neither -
; a 348-row Hercules primary offers 304 rows where a 348-row secondary would
; offer 348. A tie goes to the primary, which is where windows are born.
.rest:
    call wm_sf_axis             ; DL = the axis they tile on
    push cx
    or dl, dl
    jz .x
    mov cx, [vid_dock_y0]
    sub cx, MBAR_H
    cmp [vid_ctx + VID_CTX_SZ + VID_CTX_CH], cx
    jmp short .cmp
.x:
    mov cx, [vid_band_xe]       ; the primary's USABLE width (SPEC.md 30.5)
    sub cx, [vid_band_x0]
    cmp [vid_ctx + VID_CTX_SZ + VID_CTX_CW], cx
.cmp:
    sbb al, al                  ; AL = -1 display 1 is SMALLER, 0 a tie or
    neg al                      ; better (the primary) - so 1 or 0
    pop cx
    pop dx
    ret

; ==== bringing display 1 up (SPEC.md 39.13, 39.19), from vidsel.inc ========
; -----------------------------------------------------------------------------
; ext_extend_b - EXT_EXTEND: vid_disp_init's Extend arm: set the second card's
;                mode, bank it as display 1, place it Right or Below, and make
;                the desktop the union
; in:  AL = the other display's adapter kind, AH = the primary's (vid_disp_init
;      .paired's registers, which the far call leaves as they were)
; out: CF = 0 and [vid_ndisp] = 2 - the ONLY place it becomes 2, the invariant
;      this image exists on (see the top of this file). mod_gone's CF = 1 is
;      the caller's `jc .single`, which is the refusal path. Clobbers AX
;      (the caller banks it). Far: its slot is its only caller.
; -----------------------------------------------------------------------------
ext_extend_b:
    call ext_kapply             ; the live block is the SECONDARY's now...
    EXK vid_setmode             ; ...which is what points this at the right
                                ; card, and what makes its 32KB/16KB clear
                                ; land on the right memory
    mov al, 1
    call KERNEL_SEG:cw_vid_ctx_capture ; bank it as display 1

    mov al, ah                  ; ...and back. No vid_setmode: the primary is
    call ext_kapply_eq          ; already in its mode with the splash on it;
                                ; 40:10 back to the primary's KIND, because
                                ; vid_setmode's CGA path flipped it to colour
                                ; and nothing else ever would (SPEC.md 39.11.2)
    xor al, al
    call KERNEL_SEG:cw_vid_ctx_capture ; a no-op today - vid_apply has just republished
                                ; what vid_ctx_init banked - and 39.12's rule
                                ; followed mechanically rather than reasoned
                                ; about one instance at a time

    ; --- and WHERE display 1 sits (SPEC.md 39.19.2). The primary is at the
    ; origin either way, so one of the two words is 0 and the other is the
    ; primary's own extent - which vid_apply has just republished into the
    ; live block. Each arm writes BOTH words, so there is no pre-zero
    mov ax, [vid_cw]                  ; RIGHT: x = the primary's width...
    mov di, MBAR_H                    ; ...AND ITS TOP ROW IS THE DESKTOP'S,
                                      ; not the screen's (SPEC.md 39.19.3).
                                      ; The second monitor carries no menu
                                      ; bar, so aligning its row 0 with the
                                      ; primary's row 0 spends the first
                                      ; MBAR_H of it on a band no straddling
                                      ; window may enter - 20 of a CGA's 200
                                      ; rows, 10%. Aligning it with the
                                      ; DESKTOP BAND instead gives those rows
                                      ; back and costs nothing: the dead zone
                                      ; moves from below the card to above it
                                      ; and does not grow, mou_clamp is
                                      ; written against the union rather than
                                      ; against a rectangle (39.15.4), and
                                      ; ui_ylow already reads this very word
    cmp byte [vid_dlay], VID_L_BELOW
    jne .placed
    xor ax, ax                        ; BELOW: x = 0, y = the primary's height
    mov di, [vid_ch]
.placed:
    mov [vid_ctx + VID_CTX_SZ + VID_CTX_VX], ax
    mov [vid_ctx + VID_CTX_SZ + VID_CTX_VY], di

    call ext_two                ; the live block is display 0's again, and
    clc                         ; the DESKTOP is both of them now
    retf

; ext_kapply_eq - [vid_kind] = AL, vid_apply, and 40:10 back to that kind
; ext_kapply    - [vid_kind] = AL and vid_apply
; ext_two       - two displays, display 0 current, and the union
; The three sequences the Extend arm and the bracket's restore both make.
ext_kapply_eq:
    call ext_kapply
    EXK vid_equip
    ret
ext_kapply:
    mov [vid_kind], al
    EXK vid_apply
    ret
ext_two:
    mov word [vid_ndisp], 2     ; ndisp = 2, cur = 0: one word (vidsel.inc)
    EXK vid_desk_union
    ret

; ==== the way back into the kernel ==========================================
; exk_go - the far call EXK names, built by hand (dockmod.inc's dkk_* device
; made generic): the call left the address of `dw target` on the stack, and
; this turns that one word into a far return to just past it with cw_kretf's
; near return on top - the frame `push cs` + a near call to a per-routine stub
; used to build - and then RETFs into KERNEL_SEG:target, whose near `ret` lands
; on cw_kretf and its `retf` back here. Every register and the flags are
; preserved both ways (no ADD and no INC: LEA), so a kernel routine sees exactly
; what the site handed it.
;
; It is the cost of a COLD path: ~300 cycles a hop more than a stub, which is
; ~60 us on a 4.77MHz 8088 and a few dozen of them on a drag release that then
; repaints for tens of milliseconds. The two hops that are not cold do not come
; here: vid_ctx_ptr and vid_kind_of are near copies above, and vid_disp_of,
; which wm_disp_now's one-display arm makes on OSAPI_WM_DISPLAY's path, is
; far-called through its resident shim, cw_vid_disp_of. Its stack peak is 16
; bytes above the call, and every routine reached through it goes at least
; that deep once it is entered.
; -----------------------------------------------------------------------------
exk_go:                         ; ovlchk: builds its own far frame
    push ax                     ; STKBALANCE-OK: a far frame built by hand, its
    push ax                     ; four slots spent by the RETF below and by the
    push ds                     ; target's own return through cw_kretf. The
    push ax                     ; segment slot is PUSHED: DS is KERNEL_SEG
    push bp                     ; throughout the image
    mov bp, sp                  ; [bp+2] offset, [bp+4] segment, [bp+6] the
    push bx                     ; near return, [bp+8] the far return's offset
    mov bx, [bp+10]             ; and [bp+10] the call's own return, which
    mov [bp+10], cs             ; becomes the far return's CS
    mov word [bp+6], cw_kretf
    lea bx, [bx+2]              ; past the `dw`
    mov [bp+8], bx
    mov bx, [cs:bx-2]           ; ...which names the target
    mov [bp+2], bx
    pop bx
    pop bp
    retf
section .text
%endif
