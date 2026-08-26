; =============================================================================
; os8088 - RAMPAGE.DRV
;
; The RAM disk's OTHER half (SPEC.md 62.9.9): its Control Panel page. It is
; read off the system volume the first time somebody selects `Ram Disk` in the
; panel's list, and its memory goes back when the panel closes.
;
; **IT IS NOT A DRIVER.** Same header, same org 0, same three-byte dispatcher,
; same one-claim load - and no class the kernel knows, no row in drv_tab, and
; no Drivers-page tick of its own. RAMDISK.DRV owns it. It could not be a
; driver even if that were wanted: publication is per CLASS (SPEC.md 51.2.1),
; so a second DRVC_FILE image would disconnect the volume the resident is
; serving.
;
; WHY THE SPLIT. Everything in here runs only while a human is standing at the
; machine clicking on it, and the RAM disk is a driver somebody may well tick
; once and never open the panel for again. The store, the verbs, the settings
; and the mount stay over there; the layout, the strings, the paint, the click
; and the file-dialog completion come here.
;
; HOW IT REACHES THE MACHINE. Everything the kernel offers, it calls directly:
; it is an ordinary os88api.inc client. What it does NOT have is the store, the
; settings or the volume - rp_svc is a thunk into RAMDISK.DRV (rdpage.inc), and
; RSV_STATE is what every paint and every click reads its world from.
;
; TEARDOWN IS THE EASY HALF, unlike HDDTOOL.DRV's: this page owns no window and
; draws only inside the panel's pane, so there is nothing of it left on screen
; when the panel has gone and RDT_BUSY has nothing to guard. What CAN outlive
; the panel is a file dialog, and that is the RESIDENT's flag, because the
; resident is who the kernel calls back.
; =============================================================================

%include "os88drv.inc"

    OS88_OVERLAY 'Ram Page', RD_ABI_VER, rp_entry

%include "rdabi.inc"

; -----------------------------------------------------------------------------
; rp_entry - the dispatcher's landing site
; in:  AL = an RDT_*; DS = CS = ours, ES = KERNEL_SEG
; out: per the verb
;
; The kernel never calls this: RAMDISK.DRV does, through the same
; `call bp / retf` the kernel would have used, with BP = this offset out of our
; own header. So the VERB is in AL, exactly as DRVV_* is on a driver's entry -
; BP is what the dispatcher jumps to and can never also be what it means.
; -----------------------------------------------------------------------------
rp_entry:
    cmp al, RDT_INIT
    je rp_init
    cmp al, RDT_PAINT
    je rp_v_paint
    cmp al, RDT_CLICK
    je rp_v_click
    cmp al, RDT_DLGDONE
    je rp_v_dlg
    cmp al, RDT_BUSY
    je rp_v_busy
    cmp al, RDT_KEY
    je rp_v_key
    cmp al, RDT_UP
    je rp_v_up
    cmp al, RDT_DRAG
    je rp_v_drag
    stc                         ; a verb from a newer resident than this image
    ret                         ; - refuse it rather than run another one

; -----------------------------------------------------------------------------
; rp_init - RDT_INIT: who loaded us, and how to call back
; in:  DX = the resident's segment, BX = rd_svc_page's offset in it
; out: CF = 0
;
; The ABI version was checked by the resident before this call (it is in our
; header at +10), so there is nothing left to refuse on. It is still a verb of
; its own rather than something inferred at the first service call, because a
; far pointer built lazily is one that can be built wrong once and then used
; for the life of the image.
; -----------------------------------------------------------------------------
rp_init:
    mov [rp_rseg], dx
    mov [rp_rsvc], bx
    mov word [rp_rdisp], PKG_DISP
    mov [rp_rdisp+2], dx
    clc
    ret

rp_v_paint:
    call rp_paint
    clc
    ret

rp_v_click:
    call rp_click
    clc
    ret

rp_v_up:
    call rp_onup
    clc
    ret

rp_v_drag:
    call rp_ondrag
    clc
    ret

rp_v_dlg:
    call rp_dlgdone
    clc
    ret

rp_v_key:
    call rp_key                 ; ...and its CF straight back out, because an
    ret                         ; unclaimed key is the panel's to drop

; -----------------------------------------------------------------------------
; rp_v_busy - RDT_BUSY: is anything of ours still on screen?
; out: CF = 1 = do not unload me
;
; ALWAYS NO, and that is a fact about this page rather than a stub: it creates
; no window and draws only inside the panel's pane, which the panel's own
; teardown has already taken down. HDT_SHUT and HDT_BUSY exist over in the
; hard disk's tool because that one leaves windows whose W_SEG names the image
; about to be freed.
; -----------------------------------------------------------------------------
rp_v_busy:
    clc
    ret

; -----------------------------------------------------------------------------
; rp_svc - one far call into the resident
; in:  AL = an RSV_*; other registers per the verb
; out: whatever the verb answers, in CF and AL
;
; drv_call's shape (SPEC.md 51.2) in the other direction: BP is the
; resident's rd_svc_page offset because its dispatcher is `call bp / retf`,
; and the verb rides AL.
;
; DS becomes the resident's segment so its near code works. ES is left alone -
; rd_svc_page points it at OUR segment itself, which is where a name or a
; status block lives.
; -----------------------------------------------------------------------------
rp_svc:
    push ds
    push bp
    mov bp, [rp_rsvc]
    mov ds, [rp_rseg]
    call far [cs:rp_rdisp]
    pop bp
    pop ds
    ret

; -----------------------------------------------------------------------------
; rp_state - refresh rp_st from the resident
; in:  nothing
; out: nothing (all registers preserved)
;
; ASKED ON EVERY PAINT AND EVERY CLICK, not cached: the panel stays clickable
; underneath a dialog's completion, a boot restore may have failed before this
; image existed, and RDS_ERR is read-and-clear at the far end - so a stale
; block would report a verdict twice and miss a mount entirely.
; -----------------------------------------------------------------------------
rp_state:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, rp_st
    mov al, RSV_STATE
    call rp_svc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "page.inc"

; --- state -------------------------------------------------------------------
rp_rseg:    dw 0                ; the resident's segment...
rp_rsvc:    dw 0                ; ...its rd_svc_page offset...
rp_rdisp:   dw 0, 0             ; ...and a far pointer to its dispatcher
rp_px:      dw 0                ; the pane's origin, banked per paint or click
rp_py:      dw 0
rp_cx:      dw 0                ; ...and the click inside it
rp_cy:      dw 0
rp_dlgmode: db 0                ; which dialog came back
rp_ink:     db CBLACK           ; the ink rp_str letters in, banked by rp_pen
                                ; at the branch that decides SPEC.md 47's flag
                                ; - a driver cannot read [gfx_color] back
rp_kbshown: dw 0                ; WHAT THE GLASS IS SHOWING - the size the last
rp_eshown:  db 0                ; paint drew, and the verdict beside it. Both
                                ; are written by rp_paint and by nothing else,
                                ; so the record cannot fall out of step with
                                ; the pixels; they are what lets a size change
                                ; cost four characters (SPEC.md 62.9.11.1)
rp_st:      times RDS_SZ db 0   ; RSV_STATE's block: everything this page draws
rp_rect:    dw 0, 0, 0, 0       ; one control's rect, in SCREEN coordinates
rp_line:    times 40 db 0       ; a caption, built up
rp_tmp:     times 8 db 0        ; ...and one number inside it
rp_name:    times 14 db 0       ; the name a dialog came back with
rp_down:    dw 0                ; WHICH CONTROL IS BEING HELD (SPEC.md 13.8),
                                ; 0 = none. ONE id space: a button's id is its
                                ; own RECT POINTER - the hit test answers in
                                ; one and every painter takes one, so there is
                                ; no second numbering to keep in step - and a
                                ; radio, having no rect to be named by, is
                                ; 1..2, which no rect can collide with
rp_edit:    db 0                ; 1 = the size box has the caret (SPEC.md
                                ; 62.9.11.2)
rp_ebuf:    times 8 db 0        ; ...what has been typed into it
rp_elen:    db 0

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%include "os88ui.inc"

    OS88_DRV_END
