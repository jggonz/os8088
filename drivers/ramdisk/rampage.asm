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
;
; REGISTERS. Every verb arrives through one of rdpage.inc's trampolines, and
; every one of those banks the registers itself - all of them around a paint,
; a click, a release, a drag, a key (bar AX, which rp_key keeps) and the
; panel's close, and AX/BX/SI/BP around a dialog's completion, which is why
; rp_dlgdone keeps CX, DX and DI. So nothing BELOW the verbs preserves a
; register it is not documented to keep: a page that saved them twice paid
; for it in the claim (SPEC.md 62.9.9).
; =============================================================================

%include "os88drv.inc"

    OS88_OVERLAY 'Ram Page', RD_ABI_VER, rp_entry

%include "rdabi.inc"
%include "page.inc"             ; FIRST, straight after the header: its rect
                                ; table wants a 4-aligned address (RP_BLBL),
                                ; and offset 32 is one for nothing. The entry
                                ; is wherever the header's +6 says it is

; -----------------------------------------------------------------------------
; rp_entry - the dispatcher's landing site
; in:  AL = an RDT_*; DS = CS = ours, ES = KERNEL_SEG
; out: per the verb
;
; The kernel never calls this: RAMDISK.DRV does, through the same
; `call bp / retf` the kernel would have used, with BP = this offset out of our
; own header. So the VERB is in AL, exactly as DRVV_* is on a driver's entry -
; BP is what the dispatcher jumps to and can never also be what it means, and
; having done that it is free: the resident banked it, so it indexes the verb
; table here. A TABLE because the verbs are 0..7 with no gap.
; -----------------------------------------------------------------------------
rp_entry:
    cld                         ; rp_app's lodsb
    cmp al, RDT_DRAG
    ja .bad                     ; a verb from a newer resident than this image
    mov bp, ax                  ; - refuse it rather than run another one
    and bp, 0x00FF              ; AH is an ARGUMENT on RDT_DLGDONE
    shl bp, 1
    jmp [ds:bp+rp_vtab]
.bad:
    stc
    ret

rp_vtab:                        ; in RDT_* order, which rdabi.inc pins
    dw rp_init                  ; RDT_INIT    0
    dw rp_paint                 ; RDT_PAINT   1
    dw rp_click                 ; RDT_CLICK   2
    dw rp_dlgdone               ; RDT_DLGDONE 3
    dw rp_v_busy                ; RDT_BUSY    4
    dw rp_key                   ; RDT_KEY     5
    dw rp_onup                  ; RDT_UP      6
    dw rp_ondrag                ; RDT_DRAG    7
%if RDT_INIT != 0 || RDT_PAINT != 1 || RDT_CLICK != 2 || RDT_DLGDONE != 3 || RDT_BUSY != 4 || RDT_KEY != 5 || RDT_UP != 6 || RDT_DRAG != 7
  %error "rp_vtab is in RDT_* order and rdabi.inc has renumbered them"
%endif

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
    push di                     ; (the one register rd_page_need does not bank)
    push ds                     ; ...and the STATE comes up ZERO here, because
    pop es                      ; nothing else will put it there: see rp_bss.
    mov di, rp_bss              ; ES is ours for the store and rd_page_need
    mov cx, RP_BSS_SZ           ; banks the caller's
    xor al, al
    rep stosb
    pop di
    mov [rp_rsvc], bx
    mov word [rp_rdisp], PKG_DISP ; the offset is static, but WRITTEN: the
                                ; zero run behind it reaches its high byte
                                ; whenever RP_BSS_SZ + 3 is a multiple of 16,
                                ; os88drv.py strips the file there, and the
                                ; claim it is read into is not zeroed
    mov [rp_rdisp+2], dx        ; ...which is also the segment rp_svc loads

; rp_v_busy - RDT_BUSY: is anything of ours still on screen? (CF = 1 = yes)
;
; ALWAYS NO, and that is a fact about this page rather than a stub: it creates
; no window and draws only inside the panel's pane, which the panel's own
; teardown has already taken down. HDT_SHUT and HDT_BUSY exist over in the
; hard disk's tool because that one leaves windows whose W_SEG names the image
; about to be freed.
;
; ...and it is the ALWAYS-LIVE predicate too (page.inc's rp_blbl), which is
; the same two instructions answering a different question.
rp_v_busy:
rp_live:
    clc
    ret

; -----------------------------------------------------------------------------
; rp_svc - one far call into the resident
; in:  AL = an RSV_*; other registers per the verb
; out: whatever the verb answers, in CF and AL; AX clobbered, everything else
;      preserved (rd_svc_page's contract)
;
; drv_call's shape (SPEC.md 51.2) in the other direction: BP is the
; resident's rd_svc_page offset because its dispatcher is `call bp / retf`,
; and the verb rides AL. BP is not banked: nothing of ours holds one.
;
; DS becomes the resident's segment so its near code works. ES is left alone -
; rd_svc_page points it at OUR segment itself, which is where a name or a
; status block lives.
; -----------------------------------------------------------------------------
rp_svc:
    push ds
    mov bp, [rp_rsvc]
    mov ds, [rp_rdisp+2]
    call far [cs:rp_rdisp]
    pop ds
    ret

; -----------------------------------------------------------------------------
; rp_state - refresh rp_st from the resident
; in:  nothing
; out: nothing (AX clobbered, everything else preserved)
;
; ASKED ON EVERY PAINT AND EVERY RELEASE, not cached: the panel stays
; clickable underneath a dialog's completion, a boot restore may have failed
; before this image existed, and RDS_ERR is read-and-clear at the far end - so
; a stale block would report a verdict twice and miss a mount entirely.
; -----------------------------------------------------------------------------
rp_state:
    push si
    mov si, rp_st
    mov al, RSV_STATE
    call rp_svc
    pop si
    ret


; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ARM              ; os88ui_arm/fire/armed: the press/release
%define OS88UI_NOGEST           ; ...and this page drives its own controls
                                ; through bfind/arm/fire/armed (rp_click,
                                ; rp_onup, rp_ondrag), so the record-based
                                ; gesture half is ~226 bytes nothing here
                                ; calls (SPEC.md 20.5.1.3.4) - HDDTOOL.DRV's
                                ; own spelling
%define OS88UI_NOGCHECK       ; radios only: no check picture (SPEC.md 13.15.3)
%include "os88ui.inc"

; --- state -------------------------------------------------------------------
rp_rsvc:    dw 0                ; the resident's rd_svc_page offset...
rp_rdisp:   dw PKG_DISP, 0      ; ...and a far pointer to its dispatcher, whose
                                ; offset every image has (SPEC.md 51.2) and
                                ; whose segment, filled by rp_init, is the
                                ; resident's DS too

; rp_bss - EVERYTHING FROM HERE TO THE END IS ZERO AT RDT_INIT BECAUSE rp_init
; ZEROES IT, and not because the bytes are zero in the file. They are LAST so
; that they are one run, and a run of trailing zeros is exactly what
; tools/os88drv.py takes off a driver image and records in byte +31 for
; drv_bss to zero at load (SPEC.md 51.1.2) - which this image never reaches:
; RAMDISK.DRV reads it with OSAPI_FILE_READ (rd_page_need), the claim is not
; cleared, and the stripped tail would arrive holding whatever the heap held.
; The claim still COVERS it - RAMPAGE_KB is cut from rampage.bin, zeros and
; all - so what the strip saves is floppy, and what this saves is the hazard.
rp_bss:
rp_px:      dw 0                ; the pane's origin, banked per paint or click
rp_py:      dw 0
rp_ink:     db 0                ; the ink rp_str letters in, banked by rp_pen
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
rp_line:    times 32 db 0       ; a caption, built up: the longest is 27
                                ; characters and the pane holds no more
rp_name     equ rp_line         ; ...and the name a dialog came back with, 8.3
                                ; and a NUL, which can share it: rp_dlgdone
                                ; stages it, the resident COPIES it inside the
                                ; one rp_svc it is handed to (rd_name_take),
                                ; and only the repaint after that letters a
                                ; caption over it
rp_tmp:     times 7 db 0        ; ...one number inside it, up to five digits,
                                ; built backwards from rp_tmp+6 - whose NUL is
                                ; never written, so it is always there
rp_down:    dw 0                ; WHICH CONTROL IS BEING HELD (SPEC.md 13.8),
                                ; 0 = none. ONE id space: a button's id is its
                                ; own RECT POINTER - the hit test answers in
                                ; one and every painter takes one, so there is
                                ; no second numbering to keep in step - and a
                                ; radio, having no rect to be named by, is
                                ; 1..2, which no rect can collide with
rp_edit:    db 0                ; 1 = the size box has the caret (SPEC.md
rp_elen:    db 0                ; 62.9.11.2), and how much has been typed -
                                ; ADJACENT, so one word store ends an edit
rp_ebuf:    times RP_SW - 1 db 0    ; ...and what: one cell short of the field,
                                    ; the caret taking the last
RP_BSS_SZ   equ $ - rp_bss
%if rp_elen != rp_edit + 1 || rp_ebuf != rp_elen + 1
  %error "rp_edit, rp_elen and rp_ebuf are one run: a word store ends an edit and rp_key reaches the buffer off the count"
%endif

    OS88_DRV_END
