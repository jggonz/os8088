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

; CLC_OR_STC, the shared prologue (os88_ent) and the return ladder
; (os88_r_*) are drivers/os88drv.inc's now: this image hand-rolled all three
; as HD_CLC_OR_STC, hd_ent and hd_r_*, and was the reason they went there.

%include "hddabi.inc"
%include "hdcom.inc"
%include "hdsvc.inc"
%include "partw.inc"
%include "fmt.inc"
%include "tool.inc"
%include "inst.inc"
%include "os88rseq.inc"         ; os88_rseq: READ_AT's registers, READ_SEQ's walk
%include "cppage.inc"
%include "iassoc.inc"

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
    cmp al, HDT_SHUT
    je hd_tshut
    cmp al, HDT_BUSY
    je hd_tbusy
    cmp al, HDT_PAINT
    je hd_page_paint            ; the page is ours now (SPEC.md 52.13), and
    cmp al, HDT_CLICK           ; the kernel's own registers came through
    je hd_page_click            ; hd_tool_call untouched - so these are the
    cmp al, HDT_UP              ; kernel's cells reached one image further
    je hd_page_up               ; out, with a verb in front of them
    cmp al, HDT_DRAG
    je hd_page_drag
    cmp al, HDT_KEY
    je hd_page_key
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
; HDT_FORMAT / HDT_INSTALL are NOT DISPATCHED any more, and that is a proof
; rather than a guess: no resident has sent either since the page moved into
; this image (SPEC.md 52.13, HD_ABI_VER 2 -> 3) - the page's own buttons open
; the two windows with hd_tool_open - and the resident refuses any tool whose
; HD_ABI_VER is not its own (hd_tool_check), so no resident that can load this
; image ever sends them. They fall to the refusal below with any other verb.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; THE SHARED PROLOGUE AND THE RETURN LADDER - drivers/os88drv.inc's OS88_ENT
; and OS88_RET_LADDER (kernel.asm's kentc_bp and kret_*, one image further
; out). `call os88_ent` banks AX..BP; a routine ends `jmp os88_r_bp` (or,
; having pushed ES after it, `jmp os88_r_es`). Its rule is the include's: only
; for a routine whose outputs are FLAGS or memory, and cold code only - ~100
; cycles a call, which nothing in this image notices: it runs while a human
; clicks, or between int 13h transfers whose own cost is milliseconds.
;
; HERE AND NOT ELSEWHERE, because of the alignment tuning below: the bytes in
; front of hdsec.inc are what is tuned, and these two are 27 of them.
; -----------------------------------------------------------------------------
    OS88_ENT
    OS88_RET_LADDER es

%include "hdsec.inc"

; =============================================================================
; PAST THE SECTOR BUFFERS ON PURPOSE. hdsec.inc pads to a 512 boundary in front
; of hd_mbr, and every byte of that pad is a byte of every load of this image.
; Code after the buffers costs no alignment, so what sits here is chosen to
; make what is IN FRONT of them end exactly on the boundary - three routines
; this file owns, 91 bytes, which put hd_mbrok at 511 mod 512 and the pad at
; zero. It is a tuning, not a rule: any change above moves it, and the cure is
; to move a routine across the buffers in one direction or the other, never to
; pad. The claim is whole KB (HDTOOL_KB), so the tuning is worth a KB exactly
; when it carries the image under a KB boundary - which today it does.
; =============================================================================
; -----------------------------------------------------------------------------
; hd_tshut - HDT_SHUT: nothing of ours left on screen, nothing of ours held
; in:  nothing
; out: CF = 0
;
; DESTROY AND NOT HIDE, which is what OSAPI_WM_DESTROY (slot 0x02BC) was added
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


; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_CHK              ; the installer's Erase box (SPEC.md 52.10.15)
%define OS88UI_ARM              ; os88ui_arm/fire/armed: the press/release
%define OS88UI_NOGEST           ; ...and all three windows drive their own buttons
                                ; through arm/fire/armed - the record-based
                                ; gesture half is ~226 bytes nothing here
                                ; calls (SPEC.md 20.5.1.3.4)
%define OS88UI_NOBFIND          ; hit-tests its own rects: no os88ui_bfind (SPEC.md 20.5.1.3.4)
%define OS88UI_NOGRADIO       ; check boxes only: no radio ring/dot (SPEC.md 13.15.3)
%include "os88ui.inc"

    OS88_DRV_END
