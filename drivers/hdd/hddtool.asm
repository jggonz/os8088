; =============================================================================
; os8088 - HDDTOOL.DRV
;
; The hard-disk driver's OTHER half (SPEC.md 52.11): the partitioner, the
; formatter and the installer. It is read off the system volume when somebody
; clicks Format or Install on the Control Panel's Hard Drive page, and its
; memory goes back when the driver detaches.
;
; **IT IS NOT A DRIVER.** Same header, same org 0, same three-byte dispatcher,
; same one-claim load - and no class the kernel knows, no row in drv_tab, and
; no Drivers-page tick of its own. HDD.DRV owns it. It could not be a driver
; even if that were wanted: publication is per CLASS (SPEC.md 51.2.1), so a
; second DRVC_DISK image would disconnect the transport that this one needs.
;
; WHY THE SPLIT. Everything in here runs only while a human is standing at the
; machine clicking on it, and it was 57% of a driver that is resident from the
; moment the Drivers page is ticked. The transport, the volume table and the
; page stay over there; the disk tool, the FAT formatter, the installer, the
; partition-table WRITE half and the two incbin'ed boot sectors come here.
;
; HOW IT REACHES THE MACHINE. Everything the kernel offers, it calls directly:
; it is an ordinary os88api.inc client, it creates its own windows (wm_create
; takes W_SEG from the caller's segment, so their callbacks dispatch back into
; this image through the header's dispatcher exactly as a package's do), and
; it claims its own heap. What it does NOT have is the disk: hd_raw is a thunk
; into HDD.DRV (hdsvc.inc), and because it keeps the resident's register
; contract, every routine above it is one copy of one source assembled into
; whichever image needs it.
;
; TEARDOWN IS THE SHARP EDGE. HDT_SHUT must leave nothing of this image on
; screen, because the resident frees the claim the moment it returns and a
; window whose W_SEG names freed memory is a paint into whatever takes it
; next.
; =============================================================================

%define HD_TOOL                 ; hdsec.inc gives us hd_sec0 as well as hd_mbr

%include "os88drv.inc"

    OS88_OVERLAY 'HD Tool', HD_ABI_VER, hd_tentry

%include "hddabi.inc"
%include "hdcom.inc"
%include "hdsvc.inc"
%include "partw.inc"
%include "fmt.inc"
%include "tool.inc"
%include "inst.inc"

; -----------------------------------------------------------------------------
; hd_tentry - the dispatcher's landing site
; in:  AL = an HDT_*; DS = CS = ours, ES = KERNEL_SEG
; out: per the verb
;
; The kernel never calls this: HDD.DRV does, through the same `call bp / retf`
; the kernel would have used, with BP = this offset out of our own header. So
; the VERB is in AL, exactly as DRVV_* is on a driver's entry - BP is what the
; dispatcher jumps to and can never also be what it means.
; -----------------------------------------------------------------------------
hd_tentry:
    cmp al, HDT_INIT
    je hd_tinit
    cmp al, HDT_FORMAT
    je hd_tfmt
    cmp al, HDT_INSTALL
    je hd_tinst
    cmp al, HDT_SHUT
    je hd_tshut
    cmp al, HDT_BUSY
    je hd_tbusy
    stc                         ; a verb from a newer resident than this image
    ret                         ; - refuse it rather than run another one

; -----------------------------------------------------------------------------
; hd_tinit - HDT_INIT: who loaded us, and how to call back
; in:  DX = the resident's segment, BX = hd_svc's offset in it
; out: CF = 0
;
; The ABI version was checked by the resident before this call (it is in our
; header at +10), so there is nothing left to refuse on. It is still a
; separate verb rather than something inferred at the first service call,
; because a far pointer built lazily is a far pointer that can be built wrong
; once and then used for the life of the image.
; -----------------------------------------------------------------------------
hd_tinit:
    mov [hd_rseg], dx
    mov [hd_rsvc], bx
    mov word [hd_rdisp], PKG_DISP
    mov [hd_rdisp+2], dx
    clc
    ret

; -----------------------------------------------------------------------------
; hd_tfmt / hd_tinst - HDT_FORMAT / HDT_INSTALL: open a window
; in:  the gfx lock is HELD (we are inside a Control Panel page's click)
; out: CF = 0
;
; The device table is pulled over FIRST and on every open, not once at load:
; the panel stays clickable underneath and the user can retype a geometry
; there between one open and the next.
; -----------------------------------------------------------------------------
hd_tfmt:
    call hd_sync
    call hd_tw_open
    cmp word [hd_twin], 0       ; CF = 1 = no window went up, and the page is
    je .no                      ; the only thing that can say so
    clc
    ret
.no:
    stc
    ret

hd_tinst:
    call hd_sync
    call hd_iw_open
    cmp word [hd_iwin], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hd_tshut - HDT_SHUT: nothing of ours left on screen, nothing of ours held
; in:  nothing
; out: CF = 0
;
; DESTROY AND NOT HIDE, which is what OSAPI_WM_DESTROY (slot 0x0398) was added
; for. Hiding takes the pixels down and leaves the RECORD, holding a W_SEG that
; names this image - inert while nothing re-shows it, and a loaded gun once the
; image is freed and something else claims the memory. It also costs a window
; slot per load, and MAX_WIN is 12: with the image now reclaimed every time the
; tool is finished with, an open/close cycle that leaked one would run the
; table out inside a session.
;
; hd_iw_shut runs FIRST for whatever else the installer holds; destroying a
; window it has already hidden is fine - wm_destroy tests the visible bit and
; passes an empty damage rect when it is clear.
; -----------------------------------------------------------------------------
hd_tshut:
    push ax
    push bx
    call hd_iw_shut
    mov bx, [hd_twin]
    or bx, bx
    jz .inst
    call OSAPI_WM_DESTROY
    mov word [hd_twin], 0
.inst:
    mov bx, [hd_iwin]
    or bx, bx
    jz .done
    call OSAPI_WM_DESTROY
    mov word [hd_iwin], 0
.done:
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_tbusy - HDT_BUSY: is a window of ours still on screen?
; in:  nothing
; out: CF = 1 = yes, do not unload me
;
; VISIBLE, not merely allocated. Closing one of our windows is a hide, so
; [hd_twin] stays non-zero for the rest of the load and testing it would pin
; this image until detach - which is the whole thing this verb exists to stop.
;
; The resident asks; we never volunteer. A "you may free me now" call would be
; answered by freeing the image the call has to return into.
; -----------------------------------------------------------------------------
hd_tbusy:
    push bx
    mov bx, [hd_twin]
    call hd_win_live
    jc .busy
    mov bx, [hd_iwin]
    call hd_win_live
    jc .busy
    pop bx
    clc
    ret
.busy:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_win_live - CF = 1 when BX names a window of ours that is on screen
; in:  BX = a window pointer, or 0
; out: CF = 1 visible; CF = 0 = never created, or closed
; clobbers: flags
; -----------------------------------------------------------------------------
hd_win_live:
    or bx, bx
    jz .no
    push cx
    push dx
    call OSAPI_WM_GEOM          ; CF = 1 = not visible
    pop dx
    pop cx
    jc .no
    stc
    ret
.no:
    clc
    ret

%include "hdsec.inc"

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%include "os88ui.inc"

    OS88_DRV_END
