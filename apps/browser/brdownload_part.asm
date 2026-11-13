; Browser part 0: the Download Manager - its window, its Save dialog answer,
; its disk writes and the transfer engine (brdownload_engine.inc), its painter
; (brdownload_paint.inc) and its icons - plus Copy of a page selection and
; the rarely used UI (brcold.inc, SPEC.md 71.24).
; DS is always the primary Browser segment; every imported br_* symbol is an
; offset into that segment (tools/brdlsyms.py). The entry table leads the part
; for the primary's far-call vectors (br_dl_vec, brdownload_const.inc's BRDL_*
; order); each entry is a shim that makes the near call and returns far.
cpu 8086                        ; THE TARGET IS AN 8088 (SPEC.md 1). The part
                                ; is not an OS88_HEADER image, so it does not
                                ; get that macro's `cpu 8086` - and without it
                                ; nasm relaxes a far Jcc into the 386's 0F 8x
                                ; form, which an 8088 runs as POP CS: Copy
                                ; Selection jumped into segment 0 on the XT
bits 16
org 0

BR_UBUF     equ 160
BR_HOSTMAX  equ 64
BR_PATHMAX  equ 96

%include "os88api.inc"
%include "netpkg.inc"           ; the socket verbs; NET_CLASS is [net_cls]
%include "brdownload_const.inc"
%include "brdownload_part_symbols.inc"  ; $(BUILD)/, by -I

; A primary routine is called from the part far, `call far [br_dl_r_<name>]`,
; onto brdownload.inc's shim (br_dl_rs_<name>) - never near

    dw br_dlf_paint                 ; BRDL_PAINT
    dw br_dlf_copy                  ; BRDL_COPY
    dw br_dlf_open                  ; BRDL_OPEN
    dw br_dlf_link                  ; BRDL_LINK
    dw br_dlf_wake                  ; BRDL_WAKE
    dw br_dlf_close                 ; BRDL_CLOSE
    dw br_dlf_timer                 ; BRDL_TIMER
    dw br_dlf_up                    ; BRDL_UP
    dw br_dlf_drag                  ; BRDL_DRAG
    dw br_dlf_saved                 ; BRDL_SAVED
    dw br_dlf_main                  ; BRDL_MAIN
    dw br_dlf_step                  ; BRDL_STEP
    dw br_dlf_ctxshow               ; BRDL_CTXSHOW
    dw br_dlf_ctxclick              ; BRDL_CTXCLICK
    dw br_dlf_src                   ; BRDL_SRC
    dw br_dlf_submit                ; BRDL_SUBMIT
    dw br_dlf_bmcommit              ; BRDL_BMCOMMIT
    dw br_dlf_mapclick              ; BRDL_MAPCLICK
%if ($ - $$) != BRDL_N * 2
%error "part 0's entry table does not match brdownload_const.inc's BRDL_N"
%endif

br_dlf_paint:   call br_dlx_paint
                retf
br_dlf_copy:    call br_copyselection
                retf
br_dlf_open:    call br_dl_openwin
                retf
br_dlf_link:    call br_dlx_link
                retf
br_dlf_wake:    call br_dlx_onwake
                retf
br_dlf_close:   call br_dlx_close
                retf
br_dlf_timer:   call br_dlx_timer
                retf
br_dlf_up:      call br_dlx_onup
                retf
br_dlf_drag:    call br_dlx_ondrag
                retf
br_dlf_saved:   call br_dlx_saved
                retf
br_dlf_main:    call br_dlx_mainclose
                retf
br_dlf_step:    call br_dlx_step
                retf
br_dlf_ctxshow: call br_cx_ctxshow
                retf
br_dlf_ctxclick: call br_cx_ctxclick
                retf
br_dlf_src:     call br_cx_srctoggle
                retf
br_dlf_submit:  call br_cx_submit
                retf
br_dlf_bmcommit: call br_cx_bmcommit
                retf
br_dlf_mapclick: call br_cx_mapclick
                retf

%include "brdownload_paint.inc"
%include "brdownload_engine.inc"

; This optional UI command is called only by the foreground package task.
%include "brdocmarks.inc"
%include "brcopyselection.inc"
%include "brcold.inc"

; The three icons, read only by brdownload_paint.inc. They are BLIT1 bands
; the kernel reads at ES:SI, so br_dl_blit1 points ES at this part's DATA
; segment ([br_dl_partdseg], op_seg's answer) - not at CS, which under
; kern_mega is a code selector (SPEC.md 20.12). 660 bytes the primary
; segment no longer carries (docs/plans/BROWSER-1.7-ROADMAP.md S1).
%include "assets/brdownload_icons.inc"
