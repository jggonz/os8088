; =============================================================================
; os8088 - deskmod.inc
;
; DESK.DRV (SPEC.md 11.103.10): the desk layer's code that runs only where a
; widget exists, as an on-demand module (SPEC.md 2.8) - OSAPI_WM_DESK's MAKE,
; DRAG and RELEASE, the hover edge's body, a desk window's show and the
; widget's key arm. Prefix `dkd_`. kern_big (and kern_emu) only: kern_small
; has no desk layer, and its cell is mod_gone (wm.inc).
;
; WHY IT IS A MODULE. Main's size pass left kern_big's `.cold` at exactly a
; rung boundary, so every cold byte of the Sticky App kernel half crossed it. The
; owner accepted ONE 512-byte cold step and this cut together (SPEC.md
; 11.103.10), so that the step is not spent at once.
;
; WHY IT QUALIFIES (docs/plans/completed/ONDEMAND-PLAN.md 1). Every Sticky App asks
; QUERY first, inside its ENTRY PROC, where it is about to read its own state
; file off the system disk - so the disk is already required, and QUERY is the
; load point. Nothing else loads it: DRAG and RELEASE only follow a MAKE that
; succeeded, the hover, show and key arms only ever act on a WF_DESK window,
; and only MAKE, in this image, sets WF_DESK. With no image every slot is
; mod_gone (CF = 1, registers kept), which is the right answer for each.
;
; THE LIFETIME: held from the first QUERY that loads it until restart. There
; is no mod_drop anywhere - widgets are session-long, and a drop check would
; cost resident bytes the cut exists to save. No entry calls anything that
; can destroy a window, so the image could never be freed under its own code
; even if a drop were added.
;
; DS = KERNEL_SEG on every entry (api_rsc, cur_shape_pass, wm_show_b,
; fdlg_top_x), so the kernel's data is plain references. Kernel code is
; reached by far calls through resident cells and shims (apic_*, cw_*), which
; is what lets the image carry no EXK device at all. os88ovlchk refuses a
; near call either way across the boundary (its MODS list names `.modj`).
; =============================================================================

%ifdef KERN_BIG
DKD_NENT    equ 4               ; DESK.DRV's mod_fp entries, in DKDFP:
DKD_VERB    equ 0               ;   MAKE / DRAG / RELEASE (AL = 1..3)
DKD_HOVER   equ 4               ;   the hover edge's body
DKD_SHOW    equ 8               ;   a desk window's show
DKD_KEY     equ 12              ;   fdlg_top_x's "does a widget own the bar"

section .cold
; -----------------------------------------------------------------------------
; osapi_wm_desk_x - OSAPI_WM_DESK, the desk layer's one door (SPEC.md 11.103.9)
; far, DS = KERNEL_SEG (api_rsc). in: AL = the verb, BX = the window for 1-3.
; out: CF = 0 done / CF = 1 refused; every register preserved.
;
; QUERY IS THE LOAD (SPEC.md 11.103.10): AL = 0 is desk_need's answer - CF =
; 0 the layer is resident from here until restart, CF = 1 it is not (no
; DESK.DRV on the system disk, no heap, a stale stamp), and the Sticky App takes the
; titled path as on kern_small. Every other verb is the image's dkd_verb, and
; with no image the slot is mod_gone's refusal. A package carrying
; OS88_F_DESK has normally been given the image already, by the loader's
; step 5 (desk_need's banner says why there), so its QUERY is mod_need's
; `.already` and touches no disk.
; -----------------------------------------------------------------------------
osapi_wm_desk_x:
    or al, al
    jnz .v
    call desk_need              ; preserves every register; CF is the answer
    retf
.v:
    jmp far [DKDFP + DKD_VERB]  ; MAKE / DRAG / RELEASE: the image's

; -----------------------------------------------------------------------------
; desk_need - mod_need MOD_DESK, INSIDE whatever drv_vol bracket is open
;             (SPEC.md 11.103.10), near, resident .cold
; in:  nothing. out: CF = mod_need's answer; every register preserved
;
; TWO CALLERS: QUERY above, and the loader's step 5 for a package whose header
; carries OS88_F_DESK - BEFORE the package's region is claimed, so the 1 KB
; pinned claim lands ABOVE the widget's region and not under it. Loaded from
; the entry proc instead, the image sat directly under the widget, and a
; closed widget's region stayed stranded above a pin for the rest of the
; session (24 KB of contiguous heap on the measured boot, SPEC.md 11.103.10).
;
; THE PAIR IS SAVED BECAUSE THE BRACKET NESTS HERE. drv_vol_bank keeps ONE
; static pair (drv_vol_cwd / drv_vol_drv) on the rule that no two brackets
; ever nest - and both callers run inside one: a desktop shortcut's or
; stik_boot's launch is under sc_open's, and a widget's entry proc is reached
; from it. mod_need's own bracket would overwrite the outer pair with the
; widget's FOLDER, and sc_open's restore would then leave the machine there
; instead of where the user was. The pair is two adjacent words' worth of
; bytes (a dw and a db), so it goes on the stack as a word and a byte - never
; `push word [drv_vol_drv]`, whose high byte is unrelated .text.
; -----------------------------------------------------------------------------
desk_need:
    push ax
    push word [drv_vol_cwd]     ; the OUTER bracket's pair...
    mov al, [drv_vol_drv]
    push ax
    mov al, MOD_DESK
    call mod_need               ; preserves every register; CF is the answer
    pop ax                      ; ...put back for its restore (neither `pop`
    mov [drv_vol_drv], al       ; nor `mov` writes a flag)
    pop word [drv_vol_cwd]
    pop ax
    ret

; =============================================================================
; DESK.DRV - the module image
; =============================================================================
section .modj
modj_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER, MOD_DESK        ; MOD_H_VER, MOD_H_ID
    dw BUILD_NUM, MOD_STAMP     ; MOD_H_BUILD, MOD_H_STAMP (SPEC.md 2.8.2)
    dw modj_end, DKD_NENT       ; MOD_H_IMG, MOD_H_NENT
    dw dkd_verb, dkd_hover, dkd_show, dkd_key

; -----------------------------------------------------------------------------
; dkd_verb - OSAPI_WM_DESK's verbs past QUERY (SPEC.md 11.103.9), far
; in:  AL = the verb (never 0: the resident thunk answers QUERY), BX = the
;      window. out: CF = 0 done / CF = 1 refused; every register preserved.
;   AL = 1 MAKE     BX = a used, HIDDEN, not-FULL, not-SIZABLE window nobody
;                   owns yet, made by the package being loaded and asked for
;                   INSIDE ITS ENTRY PROC: WF_DESK|WF_NOANIM|WF_OWNBG on,
;                   WF_SAVEU|WF_1BPP off, the z entry moved to [0]. IF_DESK
;                   is the loader's step 9's, off the window it RETURNS
;   AL = 2 DRAG     post the kernel's outline drag of BX, the window being
;                   PRESSED: [ui_dragy] = BX, which the content press
;                   compares with the pressed window when W_ONCLICK returns
;                   and honours for a DESK window only (SPEC.md 11.103.6);
;                   posted anywhere else the next press's seed wipes it
;   AL = 3 RELEASE  [menu_win] = BX: the bar (and the keys) to wm_top, redrawn
;
; MAKE IS THE ENTRY PROC'S, and that is what lets it name its caller for
; nothing. wm_owner is published only at the loader's step 9, AFTER the entry
; proc returns (SPEC.md 21), so the window a package is setting up is the one
; with NO owner, and the load is [ld_base] - the loading package's segment
; from step 5 until step 9's show, 0 otherwise - so W_SEG = [ld_base] is "a
; window THIS load's package made". [ld_rec]'s I_STATE = 0 closes the last
; gap: step 9 publishes it before the show, whose repaint reaches the
; package's W_PAINT. A window that is owned, another package's, or a call with
; no load in progress is refused; the [ld_rec] test alone was satisfied by a
; FREED record, which [ld_rec] keeps naming after any load (SPEC.md 110.9).
; Package window pointers are trusted here as at every wm_* cell
; (wm_ptr2idx's own div); the USED bit is what guarantees the window is in
; wm_zord, which the rotation below relies on.
; -----------------------------------------------------------------------------
dkd_verb:
    cmp al, 1
    je .make
    cmp al, 3
    ja .no                      ; any verb nobody defined
    jb .drag                    ; 2, DRAG (0 never arrives: the thunk's)
    cmp bx, [menu_win]          ; --- RELEASE
    jne .ok                     ; not the bar's owner: nothing to give back
    push bx
    call KERNEL_SEG:apic_wm_top ; BX = the front APPLICATION window, 0 =
    call KERNEL_SEG:cw_menu_activate ; Locator (SPEC.md 11.103.2)
    call KERNEL_SEG:cw_menu_draw_bar
    pop bx
.ok:
    clc
    retf
.no:
    stc
    retf
.drag:                          ; --- DRAG: the caller's window over the
    mov [ui_dragy], bx          ; press's seed (SPEC.md 11.103.6). No test
    jmp short .ok               ; here: the pick-up asks whether it is the
                                ; window pressed, and a desk one
.ref:
    stc
.pop:
    pop di
    pop si
    pop cx
    pop ax
    retf
.make:
    push ax
    push cx
    push si
    push di
    mov al, [bx+W_FLAGS]        ; used, HIDDEN, not FULL, not SIZABLE
    and al, 1 | 2 | WF_FULL | WF_SIZABLE
    cmp al, 1
    jne .ref
    mov ax, [ld_base]           ; ...inside a load (0: none)...
    or ax, ax
    jz .ref
    cmp ax, [bx+W_SEG]          ; ...by the package being loaded...
    jne .ref
    mov di, [ld_rec]            ; ...from its entry proc: the record is
    cmp byte [di+I_STATE], 0    ; still unpublished
    jne .ref
    mov ax, bx
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = the window's index (< 128)...
    cbw                         ; ...so AH = 0, whatever the remainder was
    xchg ax, si
    cmp byte [wm_owner+si], 0FFh
    jne .ref                    ; owned: not the window being set up
    xchg ax, si                 ; AL = the index
    mov ah, al                  ; --- THE Z ENTRY TO [0] (SPEC.md 11.103.1) as
    mov si, wm_zord             ; one ROTATION: [0] takes the index and each
.rot:                           ; slot hands its old entry up one, until the
    xchg al, [si]               ; entry carried up IS the index - its old slot,
    inc si                      ; which the entry below has just overwritten.
    cmp al, ah                  ; Already at [0]: one step, nothing moved
    jne .rot
    and word [bx+W_FLAGS], 0FFFFh & ~(WF_SAVEU | WF_1BPP)
    or word [bx+W_FLAGS], WF_DESK | WF_NOANIM | WF_OWNBG
    clc                         ; (no cli round the two stores: the window is
    jmp short .pop              ; the entry proc's own and no worker of its
                                ; package can exist yet - SPEC.md 20.6)

; -----------------------------------------------------------------------------
; dkd_hover - the HOVER EDGE's body (SPEC.md 11.103.7), far, from
;             cur_shape_pass on the UI task, which has just asked wm_hit and
;             found its answer CHANGED
; in:  BX = the OLD window (0 = none); [cur_hovw] = the NEW one - the caller
;      exchanged them, so [cur_hovw] is current even when this image is not
;      loaded and the slot is mod_gone
; out: AX kept; BX, CX spent (the caller banks BX). Flags spent
;
; One loop, two passes: CL = 0 takes WF_HOVER OFF the old window, CL =
; WF_HOVER >> 8 puts it ON the one [cur_hovw] names now - each only if it is
; a desk window, and each gets a wake (the cell is the shim). SET or CLEAR,
; never toggle: a record that died has W_FLAGS = 0 and is no desk window, and
; one reborn as a widget gets a clear it already had. A Standard File chooser
; up makes the new window NOTHING (dkx_pass's refusal, SPEC.md 30.6): a handle
; revealed under it could not be pressed (SPEC.md 38.2)
; -----------------------------------------------------------------------------
dkd_hover:
    xor cx, cx                  ; CL = 0: the first pass, the clear
    cmp [fdlg_win], cx
    je .e
    mov [cur_hovw], cx          ; a chooser up: nothing is hovered
.e:
    or bx, bx
    jz .n
    test byte [bx + W_FLAGS + 1], WF_DESK >> 8
    jz .n
    and byte [bx + W_FLAGS + 1], ~(WF_HOVER >> 8) & 0xFF
    or [bx + W_FLAGS + 1], cl   ; ...and on again for the NEW one
    call KERNEL_SEG:apic_wm_wake    ; it tells the widget; preserves all
.n:
    xor cl, WF_HOVER >> 8       ; the first pass done: CL = the bit, ZF = 0
    mov bx, [cur_hovw]          ; ...and the second: CL = 0 again, ZF = 1
    jnz .e
    retf

; -----------------------------------------------------------------------------
; dkd_show - wm_show_b's desk arm (SPEC.md 11.103.3), far, from wm_show_dk
; in:  BX = the desk window, under wm_show's lock. out: every register kept
;
; A DESK window is shown by the DAMAGE PASS: visible, then wm_paint_dmg over
; its frame, which draws it in z-order - under every window above it, which
; the marking pass redraws where they overlap - with no precover, no zoom, no
; raise and no menu_activate. NOT wm_draw_win under the cull: wm_clip_test
; rounds OUTWARD, so a glyph straddling a covering window would be drawn whole
; over it, and outside wm_dmg_wins nothing repairs that. [wm_dmg_stwin] is 0
; here (a one-shot), so wm_dmg_stale marks the widget.
; -----------------------------------------------------------------------------
dkd_show:
    push ax
    push bx
    push cx
    push dx
    push si
    or byte [bx+W_FLAGS], 2
    mov si, bx
    call KERNEL_SEG:cw_wm_win_rect  ; AX..DX = its frame (no shadow: chromeless)
    mov byte [wm_dmg_npro], 1   ; ...and PROMOTES NOBODY: a desk window is
                                ; never wm_top, so the front window is the one
                                ; it was and its title is right (SPEC.md
                                ; 11.103.3) - or .promote re-titles it
    call KERNEL_SEG:cw_wm_paint_dmg ; under wm_show's lock
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; dkd_key - does a WIDGET own the bar? (SPEC.md 11.103.4, 12.6), far, from
;           fdlg_top_x with no chooser up
; out: CF = 0 and BX = [menu_win], a desk window: the key goes to IT;
;      CF = 1 "stand" - wm_top's answer (BX spent). AX and the rest kept
;
; Never a HIDDEN one: every hide repaints through menu_draw_bar, whose
; menu_check takes the bar off a window that is not visible. A stale
; [menu_win] is safe: wm_destroy zeroes W_FLAGS, and a reused record is born
; without WF_DESK.
; -----------------------------------------------------------------------------
dkd_key:
    mov bx, [menu_win]
    or bx, bx
    jz .s
    test byte [bx+W_FLAGS+1], WF_DESK >> 8
    jnz .y                      ; CF = 0 (test clears it): BX = the widget
.s:
    stc
.y:
    retf

section .text
%endif
