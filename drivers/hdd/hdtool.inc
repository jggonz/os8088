; =============================================================================
; os8088 - HDD.DRV / hdtool.inc
;
; The resident half of the two-image split (SPEC.md 52.11): bringing
; HDDTOOL.DRV in when somebody clicks Format or Install, calling into it,
; answering the calls it makes back, and giving its memory up again.
;
; WHY THE TOOL IS NOT A DRIVER. It has the same header, the same org 0, the
; same three-byte dispatcher and the same one-claim load discipline - but it
; is not in drv_tab, the kernel never sees it, and it has no class of its own
; to be published under. It could not have one: publication is per CLASS
; (SPEC.md 51.2.1) and a second DRVC_DISK driver would disconnect this one.
; So HDD.DRV loads it the way the kernel loads a driver, and everything
; between the two images goes through hddabi.inc.
;
; FINDING THE FILE NEEDS NO NEW API, and that is worth stating because it
; looks like it should. HDDTOOL.DRV is on the SYSTEM volume, which is A: on a
; floppy machine and the boot partition on an installed one (SPEC.md 52.10.3),
; and a driver has no way to ask which. It does not need one: drv_load
; brackets the whole load in drv_vol_bank .. drv_vol_back and calls
; drv_mounted before it reads, and DRVV_READY is sent from INSIDE that bracket
; (SPEC.md 51.2.2) - so at the top of hd_ready the current volume already IS
; the system volume, and one OSAPI_FILE_HERE banks it for good. Every load
; afterwards is a GOTO there and a GOTO back, which is cfg.inc's and
; inst.inc's own idiom (SPEC.md 51.5.2).
;
; THE COST OF THAT, and it is the one user-visible regression in the split:
; Format now needs the system disk in the drive. Install always did - it
; copies from it - so this is new only for Format, and on a single-floppy
; machine (docs/FIELD-MACHINES.md) the user may well have swapped. The
; failure is clean: the GOTO or the read fails, the page says so, and nothing
; is claimed.
;
; WHEN THE MEMORY GOES BACK (SPEC.md 52.11.7): when the CONTROL PANEL closes,
; and at detach whatever happens. DSV_CPCLOSE is the hook (SPEC.md 31.9.1) and
; hd_tool_reap is what the driver publishes in it.
;
; The panel and not the tool's own window, deliberately. Loading is ~18 sectors
; off the system volume - a second and a bit on the field machine - and a user
; who is partitioning a disk clicks Format, closes it, changes a geometry and
; clicks Format again. Tying the free to the window makes them pay that load
; every time; tying it to the panel makes them pay it once per visit, which is
; the same steady state and a quarter of the waiting.
;
; It cannot be the tool telling us it has finished. That call would be
; answered by freeing the image it has to return into, so the tool is ASKED
; (HDT_BUSY) and never volunteers - and it is asked even here, because the
; panel can be closed with the disk tool still on screen.
;
; This needed the one kernel byte-count the split had otherwise avoided:
; OSAPI_WM_DESTROY, slot 0x0398, 8 bytes of jump table. Closing an unowned
; window is a hide (SPEC.md 38.1), so without a destroy every load leaks a
; window record holding a W_SEG that names a freed claim - and MAX_WIN is 12.
; Holding 11KB was the better of those two, which is why this was a documented
; limit rather than a bug until the slot existed.
; =============================================================================

hd_tseg:     dw 0               ; the tool's segment, 0 = not loaded. Also the
                                ; arm for hd_svc's ES and for hd_tool_call
hd_tfar:     dw 0, 0            ; ...as a far pointer to its dispatcher
hd_tent:     dw 0               ; ...and its entry offset, out of the image's
                                ; own header at +6. The dispatcher is `call bp`,
                                ; so BP is an ADDRESS and never the verb
hd_tvol:     db 0               ; the SYSTEM volume, banked at DRVV_READY
hd_tcwd:     dw 0
hd_tvsav:    db 0               ; ...and the caller's, banked across a load
hd_tcsav:    dw 0
hd_tname:    db 'HDDTOOL.DRV', 0

; -----------------------------------------------------------------------------
; hd_tool_home - bank the system volume, once (SPEC.md 52.11)
; in:  nothing; called from hd_ready and from nowhere else
; out: nothing (all registers preserved)
;
; hd_ready runs inside drv_load's volume bracket, so what OSAPI_FILE_HERE
; answers here is the volume HDD.DRV was itself read from - which is where
; HDDTOOL.DRV is too, on a floppy machine and an installed one alike.
;
; **AND THAT SENTENCE NEEDED A KERNEL FIX TO BE TRUE** (SPEC.md 51.2.3), which
; is worth leaving here because nothing about this routine looks wrong.
; OSAPI_FILE_HERE answers for the CALLING INSTANCE (SPEC.md 19.2.1), and a
; driver is not one - but inst_caller reports whichever callback is being
; dispatched, and ticking our row on the Drivers page is a CONTROL PANEL
; click. So this banked wherever the PANEL had been launched: a user who
; opened a Disk window on B: and then opened the panel gave us (B:, root) as
; "the system volume", and Format and Install then went to B: for the tool,
; did not find it, and said "Need the system disk" with it sitting in A:.
; drv_call clears the stamp now, so this reads the machine, which is what it
; always meant to read.
; -----------------------------------------------------------------------------
hd_tool_home:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the directory, BL = its drive
    mov [hd_tcwd], dx
    mov [hd_tvol], bl
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tool_need - the tool, in memory (module internal)
; in:  nothing
; out: CF = 0 and [hd_tseg] names it; CF = 1 = it could not be brought in
; clobbers: flags
;
; BP IS SAVED HERE and that is not housekeeping. This calls four kernel slots
; and then HDT_INIT, and it is called from hd_tool_open with the caller's verb
; still to be dispatched - so a BP that comes back changed dispatches the
; WRONG VERB. It did: the restore used to sit around the HDT_INIT call only,
; so it put back whatever the file read had left rather than what the caller
; had, the tool was re-sent HDT_INIT instead of HDT_FORMAT, and that answers
; CF = 0 having done nothing at all. No window, no error, nothing on screen to
; say which of the six steps had failed.
;
; The loader's order is drv_load's with the instance half removed and the
; volume dance added: bank where the user is, go to the system volume, claim,
; read, validate the image that actually ARRIVED, hand it its init call, come
; back. Anything that fails after the claim frees it, so a refused tool costs
; the time and nothing else.
;
; HDTOOL_KB is injected by the Makefile from the tool's own built size, the
; way boot.asm is told KERNEL_SECTORS - there is no file-size slot in the API
; and a driver cannot ask. It is a CEILING and not an assertion: a tool bigger
; than the claim is refused by OSAPI_FILE_READ before any data moves
; (FERR_BIG is decided from the directory entry), and a smaller one just
; leaves the tail of the claim unread and zero.
;
; The file on the disk is a 'CZ' container like every other driver's (SPEC.md
; 20.13.3.1) and OSAPI_FILE_READ is the transparent read (20.14.3): what lands
; in the claim is the IMAGE, expanded in place, and DX:AX is its size. That is
; why the claim is cut from hddtool.bin and not from the file, and why
; hd_tool_check runs against real bytes rather than a stream - the header
; would cross a stream verbatim and pass every test it makes.
; -----------------------------------------------------------------------------
hd_tool_need:
    cmp word [hd_tseg], 0
    je .load
    clc
    ret
.load:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es

    call OSAPI_FILE_HERE        ; where the USER is, to be put back
    mov [hd_tcsav], dx
    mov [hd_tvsav], bl
    mov dx, [hd_tcwd]
    mov bl, [hd_tvol]
    call OSAPI_FILE_GOTO        ; ...and off to the system volume
    jc .fail

    mov ax, HDTOOL_KB
    call OSAPI_MEM_CLAIM_HI     ; owner = our segment, so drv_release sweeps
    jc .fail                    ; it even if we never get to free it - and
    mov [hd_tseg], dx           ; FROM THE TOP (SPEC.md 50.3.2.1): [hd_tfar]
                                ; is PKG_DISP:[hd_tseg], so this image's base
                                ; is its CS and it can never move. 13KB of
                                ; pinned claim in the middle of the arena is
                                ; also the largest single thing this driver
                                ; ever puts there

    mov es, dx
    xor bx, bx
    mov cx, HDTOOL_KB * 1024
    xor dx, dx
    mov si, hd_tname
    call OSAPI_FILE_READ        ; DX:AX = the file's size
    jc .freefail
    or dx, dx
    jnz .freefail               ; a tool over 64KB is not one of ours
    cmp ax, 32
    jb .freefail
    call hd_tool_check
    jc .freefail

    mov ax, [hd_tseg]           ; the dispatcher, as a far pointer
    mov word [hd_tfar], PKG_DISP
    mov [hd_tfar+2], ax
    mov ax, [es:6]              ; ...and the entry the dispatcher will call
    mov [hd_tent], ax

    push cs                     ; HDT_INIT: WHO WE ARE - our own segment, not
    pop dx                      ; [hd_tseg], which is the segment we have just
    mov bx, hd_svc              ; filled and is the tool's. Getting that wrong
    mov al, HDT_INIT            ; is silent: the tool builds a far pointer to
    call hd_tool_call           ; ITSELF, every service call lands on its own
    jc .freefail                ; dispatcher with hd_svc's offset in BP, runs
                                ; whatever is at that offset in ITS image, and
                                ; returns. No fault, no hang - just a device
                                ; table that never syncs and a window that
                                ; never opens

    call hd_tool_back
    clc
    jmp short .out
.freefail:
    mov dx, [hd_tseg]
    call OSAPI_MEM_FREE
    mov word [hd_tseg], 0
.fail:
    call hd_tool_back
    stc
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tool_back - put the user's volume back (module internal)
; in:  nothing
; out: nothing (all registers AND the flags preserved)
;
; The flags, because every caller's answer is in CF and a failed remount must
; not read as a failed load - drv_vol_back's own rule (SPEC.md 51.5.2), and a
; failed restore is swallowed for the same reason it is there.
; -----------------------------------------------------------------------------
hd_tool_back:
    pushf
    push bx
    push dx
    mov dx, [hd_tcsav]
    mov bl, [hd_tvsav]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; hd_tool_check - is what arrived a tool image? (module internal)
; in:  ES = the claim
; out: CF = 0 yes
; clobbers: flags
;
; drv_check's tests, against the bytes that actually came off the disk rather
; than against what the directory said. The last one is this file's own: a
; tool built against a different hddabi.inc is refused rather than called,
; because every verb number in it would mean something else.
; -----------------------------------------------------------------------------
hd_tool_check:
    push ax
    cmp word [es:0], 0x384F     ; 'O8'
    jne .no
    cmp byte [es:2], 4          ; a driver-shaped image, so the application
    jne .no                     ; loader refuses it too (SPEC.md 51.1)
    cmp word [es:4], 0          ; link base: org 0, like every package
    jne .no
    cmp byte [es:PKG_DISP], 0xFF    ; the dispatcher we are about to far-call
    jne .no
    cmp byte [es:PKG_DISP+1], 0xD5
    jne .no
    cmp byte [es:PKG_DISP+2], 0xCB
    jne .no
    cmp byte [es:10], HD_ABI_VER    ; the header's spare word, which the
    jne .no                         ; overlay macro puts the ABI version in
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_tool_call - one far call into the tool
; in:  AL = an HDT_*; other registers per the verb
; out: whatever the verb answers, in CF and AX
; clobbers: per the verb
;
; drv_call's shape (SPEC.md 51.2), and deliberately: BP is the tool's ENTRY
; OFFSET because the dispatcher at PKG_DISP is `call bp / retf` and BP is
; therefore an address, not something handed on to the callee. The verb rides
; AL, exactly as DRVV_* does on the kernel's own call into a driver.
;
; DS becomes the tool's segment so its near code works. ES is left alone: a
; page click arrives with ES = KERNEL_SEG and the tool's callbacks expect it.
; -----------------------------------------------------------------------------
hd_tool_call:
    push ds
    push bp
    mov bp, [hd_tent]
    mov ds, [hd_tseg]
    call far [cs:hd_tfar]
    pop bp
    pop ds
    ret

; -----------------------------------------------------------------------------
; hd_tool_open - bring the tool in and open one of its windows
; in:  AL = HDT_FORMAT or HDT_INSTALL
; out: CF = 0 a window is up; CF = 1 nothing opened and [hd_msg] says why
; clobbers: flags
;
; The two Control Panel buttons, and the only place a load is triggered.
;
; IT ANSWERS CF BECAUSE ITS CAPTION WAS UNREACHABLE. Both callers repaint the
; pane BEFORE this and never after - deliberately, because a page draws itself
; unclipped and a repaint after wm_show paints over the window just put up
; (page.inc's .dofmt) - so on the path where no window appears, the verdict
; set here was stored and never drawn. Clicking Install with the wrong disk in
; the drive did nothing visible at all, and the message surfaced later, on
; whatever unrelated thing repainted the page next. A refusal explains itself
; (SPEC.md 47 rule 5), so the caller needs to be able to tell the two apart.
;
; `pop` touches no flag, so the three pops below carry CF out unharmed.
; -----------------------------------------------------------------------------
hd_tool_open:
    push ax
    push bx
    push bp
    mov ah, al                  ; the verb, banked out of a register the load
    call hd_tool_need           ; below is entitled to spend
    jc .nofile
    mov al, ah                  ; the verb back into AL, where hd_tool_call
    call hd_tool_call           ; takes it
    jc .norun
    clc
    jmp short .out
.nofile:
    mov word [hd_msg], hd_s_nosys
    stc
    jmp short .out
.norun:
    mov word [hd_msg], hd_s_notool
    stc
.out:
    pop bp
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tool_reap - give the tool's memory back (DSV_CPCLOSE, SPEC.md 31.9.1)
; in:  nothing; the gfx lock is HELD, which is that cell's contract
; out: nothing (all registers preserved)
; clobbers: flags
;
; The Control Panel has closed, so everything the PAGE was using can go. Not a
; detach: we are still loaded, our volumes are still mounted, and the panel may
; be back in a second.
;
; HDT_BUSY is still asked, because the panel can be closed while the disk tool
; is still on screen - and a tool window whose image had been freed under it is
; a paint into whatever claims that memory next. Left loaded in that case, it
; goes at the next panel close or at detach.
;
; It must never run from inside the tool, and cannot: the caller is the kernel.
; -----------------------------------------------------------------------------
hd_tool_reap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [hd_tseg], 0
    je .out
    mov al, HDT_BUSY
    call hd_tool_call
    jc .out                     ; a window of its is still on screen
    call hd_tool_drop
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tool_drop - shut the tool down and free it (module internal)
; in:  nothing
; out: nothing
; clobbers: flags
;
; HDT_SHUT FIRST AND THE FREE SECOND, always. The tool's windows carry a
; W_SEG naming this claim, so one left on screen is a paint into whatever
; takes the memory next - the same obligation the kernel puts on a driver's
; own detach, one image further down.
; -----------------------------------------------------------------------------
hd_tool_drop:
    push ax
    push dx
    push bp
    cmp word [hd_tseg], 0
    je .out
    mov al, HDT_SHUT            ; AL, like every other verb: hd_tool_call owns
    call hd_tool_call           ; BP, which is the tool's ENTRY OFFSET
    mov dx, [hd_tseg]
    call OSAPI_MEM_FREE
    mov word [hd_tseg], 0
.out:
    pop bp
    pop dx
    pop ax
    ret

; =============================================================================
; hd_svc - the tool calling US (SPEC.md 52.11)
;
; in:  AL = an HSV_*, SI = a request block in the TOOL's segment, DS = ours
;      (the tool set it before the far call, so this is ordinary near code)
; out: CF, and AL where the verb says so
; clobbers: AX and the flags; everything else is preserved, because the tool
;      calls this from the middle of its own routines
;
; ES = the tool's segment for the whole of it, which is where SI points. A
; verb that wants to hand something BACK writes it into the same block.
; =============================================================================
hd_svc:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [hd_tseg]
    cmp al, HSV_RAW
    je hd_svc_raw
    cmp al, HSV_SYNC
    je hd_svc_sync
    cmp al, HSV_MOUNT
    je hd_svc_mount
    cmp al, HSV_UNMOUNT
    je hd_svc_unmount
    cmp al, HSV_CFGMARK
    je hd_svc_cfgmark
    cmp al, HSV_MSG
    je hd_svc_msg
%ifdef INSTBENCH
    cmp al, HSV_CTRS
    je hd_svc_ctrs
%endif
    stc                         ; a verb we do not know: refuse rather than
    jmp hd_svc_out              ; fall into one we do

; --- HSV_RAW: a transfer to a DEVICE, outside any volume ---------------------
; The device comes as an INDEX and is resolved against OUR table, which is
; what stops the tool naming a row that is not there.
hd_svc_raw:
    mov al, [es:si+HRQ_DEV]
    call hd_dev_row_ck          ; DI = the row, CF = 1 = no such device
    jc hd_svc_out
    mov al, [es:si+HRQ_OP]
    mov [hd_rawop], al
    mov ax, [es:si+HRQ_LBA]
    mov dx, [es:si+HRQ_LBA+2]
    mov cx, [es:si+HRQ_CNT]
    mov bx, [es:si+HRQ_OFS]
    push es
    mov es, [es:si+HRQ_SEG]     ; the buffer is in the TOOL's image
    call hd_raw
    pop es
    jmp short hd_svc_out

; --- HSV_SYNC: our device table into the tool's copy of it -------------------
; Not a snapshot the tool keeps for ever: it asks again whenever it is about
; to act, because the Control Panel stays clickable underneath its window and
; the user can retype a geometry there.
hd_svc_sync:
    push si
    push di
    push cx
    mov di, si                  ; the tool hands us the destination
    mov si, hd_devs
    mov cx, HD_MAXDEV * HDD_SIZE
    cld
    rep movsb                   ; DS:SI = ours -> ES:DI = the tool's
    pop cx
    pop di
    pop si
    mov al, [hd_ndev]           ; ...and the two counts in AX, not after the
    mov ah, [hd_sel]            ; copy: see HSV_SYNC in hddabi.inc
    clc
    jmp short hd_svc_out

; --- HSV_MOUNT / HSV_UNMOUNT -------------------------------------------------
; The volume table is OURS and stays ours: the tool names a partition and we
; do the whole job, which is why hd_vols crosses nothing.
hd_svc_mount:
    mov al, [es:si+HRQ_DEV]
    call hd_dev_row_ck          ; DI = the row, CF = 1 = no such device -
    jc hd_svc_out               ; a REAL check now, not the fiction the old
                                ; comment described
    mov ah, [hd_sel]            ; the panel's highlight is the USER's; it is
    push ax                     ; borrowed below as hd_mount_one's routing
    mov [hd_sel], al            ; register and put BACK whatever happens -
                                ; hd_cfg_automount's own save/restore shape.
                                ; Left moved, the next Format or Install
                                ; click bound to a device the user never
                                ; picked
    call hd_part_load           ; OUR copy of the table, as the format left it
    mov al, [es:si+HRQ_SLOT]
    mov [hd_pslot], al
    push es                     ; THE REQUEST BLOCK, banked BEFORE hd_part_ent
    push si                     ; takes SI - which is the whole of SPEC.md
    call hd_part_ent            ; 52.11.5. It used to be banked AFTER, so what
    call hd_mount_one           ; came back was the ENTRY pointer and the
    pop si                      ; answer below was read from, and written to,
    pop es                      ; the tool's image at a garbage offset
                                ; (`pop` leaves CF alone)
    pop ax
    mov [hd_sel], ah            ; (mov leaves CF: hd_mount_one's verdict)
    jc hd_svc_out
    mov al, [es:si+HRQ_DEV]     ; which kernel volume did it become? The row
    mov ah, [es:si+HRQ_SLOT]    ; hd_mount_one filled says, and finding it here
    call hd_vol_of              ; is what spares the tool a sight of hd_vols
    mov [es:si+HRQ_RES], al
    jmp short hd_svc_out

hd_svc_unmount:
    mov al, [es:si+HRQ_DEV]
    mov ah, [es:si+HRQ_SLOT]
    call hd_unmount_part
    mov [es:si+HRQ_RES], al     ; did anything go...
    mov [es:si+HRQ_VOL], ah     ; ...and WHICH volume, for SPEC.md 52.10.8
    clc
    jmp short hd_svc_out

hd_svc_cfgmark:
    call hd_cfg_mark
    clc
    jmp short hd_svc_out

hd_svc_msg:
    mov word [hd_msg], hd_s_pick    ; HDM_PICK is the only one so far, and a
    clc                             ; CODE is still the right shape: a string
                                    ; in the tool's segment would render this
                                    ; image's own bytes (SPEC.md 31.9)
hd_svc_out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

%ifdef INSTBENCH
; --- HSV_CTRS: the DEVICE counters (SPEC.md 52.10.9) -------------------------
; They are incremented HERE, in the transport, and read by the installer,
; which is in the other image - so they cross the seam like everything else
; does. Free-running: the caller banks and subtracts.
hd_svc_ctrs:
    mov ax, [hd_bn_cal]
    mov [es:si+HRQ_C0], ax
    mov ax, [hd_bn_sec]
    mov [es:si+HRQ_C1], ax
    mov ax, [hd_bn_rst]
    mov [es:si+HRQ_C2], ax
    clc
    jmp short hd_svc_out
%endif
