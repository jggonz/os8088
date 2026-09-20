; =============================================================================
; os8088 - HDDTOOL.DRV / hdsvc.inc
;
; The tool's side of the crossing (SPEC.md 52.11): one far call into HDD.DRV,
; and the handful of local routines that wear the same names the resident's
; did so that partw.inc, fmt.inc, tool.inc and inst.inc need not know the
; split happened.
;
; **hd_raw IS THE WHOLE TRICK.** In the resident it is the transport; here it
; is a thunk that packages the request and far-calls. Same registers, same
; answer in CF - so hd_part_load, hd_part_write, the formatter's hd_fmt_wr and
; every sector the installer moves are ONE copy of one source that happens to
; be assembled into two images.
;
; The device crosses as an INDEX and never as a row pointer: our hd_devs is a
; copy (HSV_SYNC) and the resident's is the real thing, so a pointer into ours
; would mean nothing over there. hd_dev_ix turns one back into the other.
; =============================================================================

hd_rseg:     dw 0               ; HDD.DRV's segment, from HDT_INIT
hd_rdisp:    dw 0, 0            ; ...as a far pointer to its dispatcher
hd_rsvc:     dw 0               ; ...and hd_svc's offset inside it, which is
                                ; what BP carries through that dispatcher
hd_req:      times HRQ_SIZE db 0    ; the request block most verbs read

; -----------------------------------------------------------------------------
; hd_svc_call - one call into the resident
; in:  AL = an HSV_*, SI = the block that verb reads
; out: CF, AX per the verb, and the block updated where the verb says so
; clobbers: AX, flags
;
; DS becomes the RESIDENT's for the duration, because over there this is
; ordinary near code reading its own data - the mirror of what hd_tool_call
; does coming the other way. BP is loaded BEFORE the swap: after it,
; [hd_rsvc] would be read out of the resident's image. CS is untouched, so
; the far pointer stays reachable through a `cs:` override.
; -----------------------------------------------------------------------------
hd_svc_call:
    push ds
    push bp
    mov bp, [hd_rsvc]           ; hd_svc's OFFSET: the resident's dispatcher is
    mov ds, [hd_rseg]           ; `call bp` too, so this is an address as well
    call far [cs:hd_rdisp]      ; `call bp / retf`, with DS already right
    pop bp
    pop ds
    ret

; -----------------------------------------------------------------------------
; hd_dev_ix - a row in OUR device table, as the index the resident knows
; in:  DI = a row in hd_devs
; out: AL = the index
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hd_dev_ix:
    push dx
    mov ax, di
    sub ax, hd_devs             ; 0 .. HD_MAXDEV*HDD_SIZE-1, so well under 256
    mov dl, HDD_SIZE
    div dl                      ; AL = the index, AH = 0 on a real row
    pop dx
    ret

; -----------------------------------------------------------------------------
; hd_raw - one raw transfer to a DEVICE, outside any volume
; in:  [hd_rawop] = 0 read / 1 write, DX:AX = the LBA, ES:BX = the buffer,
;      CX = sectors, DI = the device row
; out: CF = 0 done; CF = 1 failed
; clobbers: flags
;
; hdcom.inc's callers cannot tell this from the resident's, which is the
; point. The buffer is always in OUR image - hd_mbr and hd_sec0 both are - so
; ES crosses as a segment the resident loads to reach it.
; -----------------------------------------------------------------------------
hd_raw:
    push ax
    push si
    mov [hd_req+HRQ_LBA], ax
    mov [hd_req+HRQ_LBA+2], dx
    mov [hd_req+HRQ_OFS], bx
    mov [hd_req+HRQ_CNT], cx
    mov ax, es
    mov [hd_req+HRQ_SEG], ax
    mov al, [hd_rawop]
    mov [hd_req+HRQ_OP], al
    call hd_dev_ix
    mov [hd_req+HRQ_DEV], al
    mov si, hd_req
    mov al, HSV_RAW
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_sync - our copy of the device table, refreshed
; in:  nothing
; out: hd_devs, [hd_ndev] and [hd_sel] are the resident's
; clobbers: flags
;
; Asked for whenever we are about to ACT rather than kept from when we opened:
; the Control Panel stays clickable underneath our window and the user can
; retype a geometry there, so a snapshot taken at open time is a way to format
; with the wrong cylinder count.
;
; The COUNT and the SELECTION come back in AX rather than after the table,
; deliberately: writing them past the copy would make hdcom.inc's declaration
; order an unwritten part of the ABI, and the first person to tidy that block
; would break a format on a second drive with nothing to read that says why.
; -----------------------------------------------------------------------------
hd_sync:
    push ax
    push si
    mov si, hd_devs             ; SYNC's "block" is the table itself
    mov al, HSV_SYNC
    call hd_svc_call
    mov [hd_ndev], al
    mov [hd_sel], ah
    mov si, hd_req              ; ...and the three bytes that do not fit AX,
    mov al, HSV_STATE           ; in a verb of their own (hddabi.inc)
    call hd_svc_call
    mov al, [hd_req+HRQ_RES]
    mov [hd_msgc], al           ; the caption the resident last set...
    mov al, [hd_req+HRQ_VOL]
    mov [hd_mntmask], al        ; ...and which devices have a volume up
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount_one - mount the partition [hd_pslot] names on device AL
; in:  AL = the device, [hd_pslot] = the slot; the gfx lock is HELD
; out: CF = 0 and [hd_mvol] = the kernel's volume index
; clobbers: flags
;
; The resident's name, so inst.inc's call site did not have to move - but it
; ANSWERS now, where the resident's published its result by filling a row in
; hd_vols and left the caller to go and find it. That walk was the installer's
; only reason to see the volume table at all, so moving the answer into the
; verb is what keeps hd_vols entirely on the other side of the boundary.
;
; The device is an ARGUMENT and not [hd_sel], on purpose: [hd_sel] is the
; panel's live highlight and any later hd_sync - the Format window's open
; does one - moves it. The installer banked its device into [hd_idev] at open
; for exactly that reason, and reading the highlight here would have mounted,
; and then copied the system tree onto, whatever the user last clicked.
; -----------------------------------------------------------------------------
hd_mount_one:
    push ax
    push si
    mov [hd_req+HRQ_DEV], al
    mov al, [hd_pslot]
    mov [hd_req+HRQ_SLOT], al
    mov byte [hd_req+HRQ_RES], 0xFF
    mov si, hd_req
    mov al, HSV_MOUNT
    call hd_svc_call
    mov al, [hd_req+HRQ_RES]
    mov [hd_mvol], al
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_unmount_slot - drop every volume mounted on one partition
; in:  AL = the slot, [hd_tdev] = the device; the gfx lock is HELD
; out: [hd_tdrop] = 1 if anything was unmounted, 0 if not
; clobbers: flags
;
; The walk itself is the resident's now (hd_unmount_part): hd_vols is its
; table, the kernel volume indices were registered from there and are given
; back from there, so naming the partition is the whole of our half.
; -----------------------------------------------------------------------------
hd_tw_unmount_slot:
    push ax
    push si
    mov [hd_req+HRQ_SLOT], al
    mov [hd_req+HRQ_DEV], ah    ; THE DEVICE IS AN ARGUMENT, and it used to be
                                ; [hd_tdev] - which is the TOOL WINDOW's
                                ; selection, while the second caller is the
                                ; INSTALLER with [hd_idev] and a window of its
                                ; own. An install on a machine whose tool
                                ; window had never been opened unmounted a
                                ; partition of somebody else's device or of
                                ; nobody's (SPEC.md 52.10.8)
    mov byte [hd_req+HRQ_RES], 0
    mov byte [hd_req+HRQ_VOL], 0xFF
    mov si, hd_req
    mov al, HSV_UNMOUNT
    call hd_svc_call
    mov al, [hd_req+HRQ_RES]
    mov [hd_tdrop], al
    mov al, [hd_req+HRQ_VOL]
    mov [hd_tdropv], al
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_cfg_mark - stage the settings blob (SPEC.md 52.6). NO DISK I/O
; in:  nothing
; out: nothing
; clobbers: flags
; -----------------------------------------------------------------------------
hd_cfg_mark:
    push ax
    push si
    mov si, hd_req
    mov al, HSV_CFGMARK
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_page_pick - put the PAGE's caption back to its neutral line
; in:  nothing
; out: nothing
; clobbers: flags
;
; What `mov word [hd_msg], hd_s_pick` used to be, and it has to be a verb
; rather than a pointer: the page is drawn with DS = the RESIDENT and
; font_str reads through DS, so a caption pointing at a string in THIS image
; would render the resident's own bytes at that offset - SPEC.md 31.9's rule
; for DSV_CPNAME, one image further out.
; -----------------------------------------------------------------------------
hd_page_pick:
    push ax
    push si
    mov si, hd_req
    mov al, HSV_MSG
    mov ah, HDM_PICK
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_say - set the page's caption, both sides (SPEC.md 52.13)
; in:  AL = an HDM_*
; out: nothing (all registers preserved)
;
; BOTH, and that is the point: this image renders [hd_msgc] so it must hold
; it, and the resident owns the copy hd_sync will bring back the next time the
; page acts - so setting only one of them is a caption that survives until the
; first mount and then silently reverts.
; -----------------------------------------------------------------------------
hd_say:
    push ax
    push si
    mov [hd_msgc], al
    mov ah, al
    mov si, hd_req
    mov al, HSV_MSG
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_sel_set - move the panel's highlight, both sides
; in:  AL = the device index
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_sel_set:
    push ax
    push si
    mov [hd_sel], al
    mov [hd_req+HRQ_DEV], al
    mov si, hd_req
    mov al, HSV_SEL
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_geom_push - the typed geometry, into the resident's row and the drive
; in:  DI = a row in OUR copy of hd_devs
; out: nothing (all registers preserved)
;
; The resident's name and the resident's contract, so hd_page_adjust's call
; site did not move. Over there it is SET PARAMETERS on an IDE drive; here it
; is that plus the store, because our hd_devs is a COPY and an edit to it
; reaches the transport through nothing else.
; -----------------------------------------------------------------------------
hd_geom_push:
    push ax
    push si
    call hd_dev_ix
    mov [hd_req+HRQ_DEV], al
    mov ax, [di+HDD_CYL]
    mov [hd_req+HRQ_CYL], ax
    mov ax, [di+HDD_HEADS]
    mov [hd_req+HRQ_HEADS], ax
    mov ax, [di+HDD_SPT]
    mov [hd_req+HRQ_SPT], ax
    mov al, [di+HDD_FLAGS]
    mov [hd_req+HRQ_FLAGS], al
    mov si, hd_req
    mov al, HSV_GEOM
    call hd_svc_call
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount / hd_unmount - the page's two buttons, on the selected device
; in:  [hd_sel]; the gfx lock is HELD
; out: nothing (all registers preserved)
;
; The resident's names again. hd_vols is its table and the kernel registered
; those volume indices from there, so the whole job is its - the page names a
; DEVICE and reads the answer back on the next hd_sync.
; -----------------------------------------------------------------------------
hd_mount:
    mov al, HSV_MOUNTDEV
    jmp short hd_mnt_do
hd_unmount:
    mov al, HSV_UNMOUNTDEV
hd_mnt_do:
    push ax
    push si
    mov ah, al                  ; THE VERB, banked in AH and not on the stack:
    mov al, [hd_sel]            ; a second push of AX inside one proc is what
    mov [hd_req+HRQ_DEV], al    ; t_asmrules reads as a crossed pop, and it is
    mov al, ah                  ; right to - the depth is fine either way, so
    mov si, hd_req              ; getting it wrong returns with two registers
    call hd_svc_call            ; swapped and nothing faults
    call hd_sync                ; the mask, the count and the caption are all
    pop si                      ; the resident's answer (SPEC.md 52.13)
    pop ax
    ret

hd_pslot:    db 0               ; the partition hd_mount_one is to mount
hd_mvol:     db 0               ; ...and the kernel volume index it became
