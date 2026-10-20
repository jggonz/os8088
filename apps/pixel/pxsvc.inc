; =============================================================================
; os8088 - apps/pixel/pxsvc.inc
;
; THE UI SERVICES (SPEC.md 106.24): a LINKED part running on the UI task -
; the EDIT part's parameter cards, the WRITE part's Save As - calls a few of
; the resident package's own routines through ONE far gate, px_svgate: the
; part puts the service's number in [px_svi] and far-calls the gate, whose
; address the package keeps in [px_svgp] (pxlink.inc names bss and nothing
; else, so a part cannot name the routine itself). The gate calls the
; routine with every register as the part left it, DS the package's, and
; answers with its flags and registers. The decoder parts' worker services
; (PXK_SVC, SPEC.md 106.18) are the precedent; these are the UI task's.
;
; A routine reached so reads its strings through DS: a part copies its own
; words into the package's [px_cline] first (pv_cstr).
; =============================================================================

%ifndef PXSVC_INC
%define PXSVC_INC

SV_TBAND    equ 0                   ; px_tband: a text band through a slot
SV_BTN      equ 1                   ; px_btn: one button, SI
SV_GETRECT  equ 2                   ; px_getrect / px_setrect: SI's rect
SV_SETRECT  equ 3
SV_STRCPY   equ 4                   ; the string helpers (DI on the NUL)
SV_STRCAT   equ 5
SV_U32      equ 6
SV_U32N     equ 7
SV_FLAGS    equ 8                   ; px_flags
SV_REGPAINT equ 9                   ; px_regpaint: AL regions, whole
SV_LAYOUT   equ 10                  ; px_layout
SV_ESHOW    equ 11                  ; px_eshow: a changed picture shown
SV_DROP     equ 12                  ; os88ui_drop: BX a record
SV_TOAST    equ 13                  ; px_toast: SI words of the package's
SV_FILL     equ 14                  ; px_fillc: AX..DX in colour [px_pc]
SV_REGDRAW  equ 15                  ; px_regdraw: AL regions
SV_RINGCL   equ 16                  ; px_ringclaim: the ring
SV_SPAWN    equ 17                  ; px_spawn: the worker
SV_EPROG    equ 18                  ; px_eprog: the progress field
SV_UPDATE   equ 19                  ; px_update: the records' walk
SV_WALK     equ 20                  ; px_walk: the folder, walked again
SV_COMPOSE  equ 21                  ; px_compose: the fields from the record
SV_LVDO     equ 22                  ; px_lvdo: what "Save changes?" banked
SV_STRCMP   equ 23                  ; px_strcmp: SI against DI
SV_ASK      equ 24                  ; px_ask: AL a set, DI a completion
SV_KKEEP    equ 25                  ; px_kkeep: the decoder part, as shown
SV_KNEED    equ 26                  ; px_kneed: AL a decoder part, or none
SV_WFREE    equ 27                  ; px_wfree: the ring given back
SV_SETPAL   equ 28                  ; px_setpal: the mode's palette
SV_VIEWNEW  equ 29                  ; px_viewnew: the view at Fit
SV_SELOFF   equ 30                  ; px_seloff: no selection
SV_UFREE    equ 31                  ; px_ufree: undo forgets
SV_RIMG     equ 32                  ; px_rimg: the canvas's rect AX..CX x
                                    ; BX..DX rendered (an animation's frame,
                                    ; SPEC.md 106.25)
SV_MQHIDE   equ 33                  ; px_mqhide / px_mqshow: the selection's
SV_MQSHOW   equ 34                  ; outline off and back round it
SV_PUMPINIT equ 35                  ; px_pumpinit: a stream from [px_sbase]
SV_N        equ 36

; PSV n - call service n from a part (its far pointer pv_sv, set at the
; part's every entry from [px_svgp] and [cs:PXP_PKG])
%macro PSV 1
    mov byte [px_svi], %1
    call far [cs:pv_sv]
%endmacro

%endif
