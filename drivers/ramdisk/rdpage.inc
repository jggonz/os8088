; =============================================================================
; os8088 - RAMDISK.DRV / rdpage.inc
;
; The resident half of the two-image split (SPEC.md 62.9.9): bringing
; RAMPAGE.DRV in when somebody selects `Ram Disk` in the Control Panel,
; calling into it, answering the calls it makes back, and giving its memory up
; again.
;
; It is hdtool.inc with two differences and they are worth naming, because the
; shape looks identical.
;
; **THE TRIGGER IS A PAINT, NOT A CLICK.** The hard disk keeps its page
; resident and loads a second image when the user presses Format; here the
; PAGE ITSELF is the second image, so the load happens at DSV_CPPAINT - the
; first time the page is selected. What stays resident is the NAME, which has
; to: cp_list draws it with font_str and the kernel stages it at attach.
;
; **AND THE FILE DIALOG GOES OUT THROUGH HERE.** OSAPI_DRV_DLG (SPEC.md 51.10)
; resolves a drv_tab row from the CALLING SEGMENT and an overlay has no row,
; so the page asks us, we ask the kernel with rd_dlg_land as the completion
; proc, and we hand the answer back. Two hops each way, and the alternative is
; a fence a second image could walk through.
;
; Finding the file needs no new API, for hdtool.inc's reason: DRVV_READY is
; sent from inside drv_load's volume bracket (SPEC.md 51.2.2), so at the top
; of rd_ready the current volume already IS the system volume and one
; OSAPI_FILE_HERE banks it for good.
; =============================================================================

rd_pfar:     dw PKG_DISP        ; the page's dispatcher, as a far pointer -
rd_pseg:     dw 0               ; whose segment IS the page's, 0 = not loaded,
                                ; so storing the one sets the other
rd_pent:     dw 0               ; ...and its entry offset, out of the image's
                                ; own header at +6. The dispatcher is `call
                                ; bp`, so BP is an ADDRESS and never the verb
rd_pvol:     db 0               ; the SYSTEM volume, banked at DRVV_READY
rd_pcwd:     dw 0
rd_pvsav:    db 0               ; ...and the caller's, banked across a load
rd_pcsav:    dw 0
rd_pname:    db 'RAMPAGE.DRV', 0

; -----------------------------------------------------------------------------
; rd_page_home - bank the system volume, once
; in:  nothing; called from rd_ready and from nowhere else
; out: nothing (all registers preserved)
;
; OSAPI_FILE_HERE answers for the CALLING INSTANCE (SPEC.md 19.2.1) and a
; driver is not one, so what it reports is the machine's own current folder -
; which inside drv_load's bracket is the volume this driver was read from.
; SPEC.md 51.2.3's fix is what makes that sentence true: drv_call clears the
; dispatch stamp, so this is not answered for whichever instance happened to
; open the Control Panel.
; -----------------------------------------------------------------------------
rd_page_home:
    push di
    mov di, rd_pvol
    call rd_here_into           ; the volume and its folder
    pop di
    ret

; -----------------------------------------------------------------------------
; rd_page_need - the page, in memory (module internal)
; in:  nothing
; out: CF = 0 and [rd_pseg] names it; CF = 1 = it could not be brought in
; clobbers: flags
;
; BP COMES BACK UNCHANGED and that is not housekeeping: the caller's verb is
; still to be dispatched, and a BP that comes back changed dispatches the
; WRONG ONE. Nothing here writes it, and rd_page_call puts back the BP it
; spends on the dispatcher.
;
; The loader's order is drv_load's with the instance half removed and the
; volume dance added: bank where the user is, go to the system volume, claim,
; read, validate the image that actually ARRIVED, hand it its init call, come
; back. Anything that fails after the claim frees it.
;
; RAMPAGE_KB is injected by the Makefile from the page's own built size, the
; way boot.asm is told KERNEL_SECTORS - there is no file-size slot in the API
; and a driver cannot ask. It is a CEILING and not an assertion.
; -----------------------------------------------------------------------------
rd_page_need:
    cmp word [rd_pseg], 0
    je .load
    clc
    ret
.load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, rd_pvsav            ; where the USER is, to be put back
    call rd_here_into
    mov dx, [rd_pcwd]
    mov bl, [rd_pvol]
    call OSAPI_FILE_GOTO        ; ...and off to the system volume
    jc .fail

    mov ax, RAMPAGE_KB
    call OSAPI_MEM_CLAIM_HI     ; owner = our segment, so drv_release sweeps it
    jc .fail                    ; even if we never get to free it - and FROM
    mov [rd_pseg], dx           ; THE TOP (SPEC.md 50.3.2.1), because this
                                ; image's base IS its CS: [rd_pfar] is
                                ; PKG_DISP:[rd_pseg] and every verb is a far
                                ; call to it, so no compaction can ever have
                                ; it. Low, it first-fits ABOVE every movable
                                ; claim, so mem_cp_plan's fill point stops
                                ; here and the arena is two runs instead of
                                ; one - measured at 64.5KB of what a claimant
                                ; can have, on and off every time somebody
                                ; selects this page

    mov es, dx
    xor bx, bx
    mov cx, RAMPAGE_KB * 1024
    xor dx, dx
    mov si, rd_pname
    call OSAPI_FILE_READ        ; DX:AX = the file's size
    jc .freefail
    or dx, dx
    jnz .freefail               ; a page over 64KB is not one of ours
    cmp ax, 32
    jb .freefail
    call rd_page_check
    jc .freefail

    mov ax, [es:6]              ; the entry the dispatcher will call (and
                                ; [rd_pfar] already names the dispatcher)
    mov [rd_pent], ax

    push cs                     ; RDT_INIT: WHO WE ARE - our own segment, not
    pop dx                      ; [rd_pseg], which is the segment we have just
    mov bx, rd_svc_page         ; filled and is the page's. Getting that wrong
    mov al, RDT_INIT            ; is silent: the page builds a far pointer to
    call rd_page_call           ; ITSELF, every service call lands on its own
    jc .freefail                ; dispatcher and returns having done nothing

    call rd_page_back
    clc
    jmp short .out
.freefail:
    call rd_page_drop
.fail:
    call rd_page_back
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_back - put the user's volume back (module internal)
; in:  nothing
; out: nothing (all registers AND the flags preserved)
;
; The flags, because every caller's answer is in CF and a failed remount must
; not read as a failed load - drv_vol_back's own rule (SPEC.md 51.5.2).
; -----------------------------------------------------------------------------
rd_page_back:
    pushf
    push bx
    push dx
    mov dx, [rd_pcsav]
    mov bl, [rd_pvsav]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; rd_page_check - is what arrived a page image? (module internal)
; in:  ES = the claim
; out: CF = 0 yes
; clobbers: flags
;
; drv_check's tests, against the bytes that actually came off the disk rather
; than against what the directory said. The last one is this file's own: a
; page built against a different rdabi.inc is refused rather than called,
; because every verb number in it would mean something else.
; -----------------------------------------------------------------------------
rd_page_check:
    cmp word [es:0], 0x384F     ; 'O8'
    jne .no
    cmp byte [es:2], DRV_VER    ; a driver-shaped image, so the application
    jne .no                     ; loader refuses it too (SPEC.md 51.1)
    cmp word [es:4], 0          ; link base: org 0, like every package
    jne .no
    cmp word [es:PKG_DISP], 0xD5FF  ; the dispatcher we are about to far-call:
    jne .no                         ; `call bp`...
    cmp byte [es:PKG_DISP+2], 0xCB  ; ...`retf`
    jne .no
    cmp byte [es:10], RD_ABI_VER    ; the header's spare word, which the
    je .yes                         ; overlay macro puts the ABI version in
.no:
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; rd_page_call - one far call into the page
; in:  AL = an RDT_*; other registers per the verb
; out: whatever the verb answers, in CF and AX; EVERY OTHER REGISTER IS
;      PRESERVED, which is what lets each trampoline below be five bytes
;
; drv_call's shape (SPEC.md 51.2): BP is the page's ENTRY OFFSET, because the
; dispatcher at PKG_DISP is `call bp / retf` and BP is therefore an address,
; not something handed on to the callee. The verb rides AL.
;
; DS becomes the page's segment so its near code works. ES is left alone: a
; page click arrives with ES = KERNEL_SEG and the page's callbacks expect it.
; -----------------------------------------------------------------------------
rd_page_call:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    mov bp, [rd_pent]
    mov ds, [rd_pseg]
    call far [cs:rd_pfar]
    pop ds
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_page_paint - DSV_CPPAINT: bring the page in and let it draw
; in:  DI = pane left, DX = pane top; the gfx lock is HELD
; out: nothing
;
; **THE REFUSAL IS DRAWN AND NOT SWALLOWED.** A page whose image will not load
; leaves a pane the kernel has white-filled and nothing in it, which reads as
; a broken Control Panel rather than as a disk that is not in the drive. One
; line is the whole apology, and it names the disk the way SPEC.md 59.6's
; save verdict does: subject, outcome, cause.
; -----------------------------------------------------------------------------
rd_page_paint:
    push ax
    call rd_page_need           ; preserves everything; CF = 1 = not here
    mov al, RDT_PAINT
    jnc rd_page_fwd             ; ...and rd_page_fwd pops the AX banked above
    push cx
    push dx
    push si
    push es
    push ds
    pop es                      ; the run reads the string through the X
    mov cx, di                  ; stub's ES, which is our own DS
    add cx, 2
    add dx, 4
    mov si, rd_s_nopage
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_click - DSV_CPCLICK                       (SPEC.md 13.8.4)
; rd_page_up    - DSV_CPUP                          (SPEC.md 13.8.4)
; rd_page_drag  - DSV_CPDRAG                        (SPEC.md 13.8.1/13.8.4)
; rd_page_key   - DSV_CPKEY                         (SPEC.md 31.9.2)
; in:  click/up/drag: DI = pane left, DX = pane top, CX = rel x, BX = rel y;
;      key: BL = ASCII, BH = the scan code; the gfx lock is HELD
; out: key: CF = 0 the page took it, CF = 1 it did not; the others nothing.
;      Every register is preserved
;
; FOUR TRAMPOLINES OF ONE SHAPE, and none of them LOADS the image. The page
; is loaded by the PAINT that precedes anything else on it, so a click, a
; release, a drag or a key arriving with no image is one aimed at the one-line
; apology above - and a release or a drag can only follow a press this page
; took, so an image that is not here never armed anything over there either.
;
; **THE KEY IS IN BX AND NOT IN AX, AND THAT IS THE KERNEL'S CONSTRAINT RATHER
; THAN OURS** - drv_cp_call takes the CLASS in AL (SPEC.md 31.9), so AX is the
; one register a page ABI may not carry an argument in. It lands here in the
; registers RDT_KEY wants it in anyway, AL being the verb on every RDT_* call.
; -----------------------------------------------------------------------------
rd_page_key:
    push ax
    mov al, RDT_KEY
    jmp short rd_page_fwd
rd_page_up:
    push ax
    mov al, RDT_UP
    jmp short rd_page_fwd
rd_page_drag:
    push ax
    mov al, RDT_DRAG
    jmp short rd_page_fwd
rd_page_click:
    push ax
    mov al, RDT_CLICK
rd_page_fwd:                    ; AL = the verb, the caller's AX on the stack
    cmp word [rd_pseg], 0
    stc                         ; no image: CF = 1, which is rd_page_key's
    je .out                     ; "not taken" and ignored by the other three
    call rd_page_call           ; the page's own CF
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_closed - DSV_CPCLOSE: the panel has gone, so the page may (31.9.1)
; in:  nothing; the gfx lock is HELD
; out: nothing (all registers preserved)
;
; NOT A DETACH: we are still loaded, the volume is still mounted, and the
; panel may be back in a second. What goes is what the PAGE was using.
;
; NO DIALOG OF OURS CAN BE UP HERE, and that is why there is no flag for one.
; There was - [rd_pdlg], set when the dialog went up and cleared only by its
; COMMIT - so a Cancel left it set and this image stayed loaded until the
; driver was unticked. The dialog borrows the PANEL's window (SPEC.md 51.10)
; and is modal over it, so the panel cannot close under it; Restart sends no
; DSV_CPCLOSE at all; and if a commit ever did arrive with the image gone,
; rd_dlg_land goes through rd_page_fwd, which asks [rd_pseg] first and drops
; it. Keeping the image for a completion that cannot come was the whole cost.
; -----------------------------------------------------------------------------
rd_page_closed:
    push ax
    cmp word [rd_pseg], 0
    je .out
    mov al, RDT_BUSY
    call rd_page_call
    jc .out                     ; the page says it is not finished
    call rd_page_drop
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_drop - free the image (module internal)
; in:  nothing
; out: nothing
;
; There is no shutdown verb to send first and there is nothing to send one
; for: this page owns no window and draws only inside the panel's pane, so
; there is nothing of it on screen when the panel has gone. That is the whole
; difference from HDT_SHUT, which exists because the hard disk's tool leaves
; windows whose W_SEG names the image about to be freed.
; -----------------------------------------------------------------------------
rd_page_drop:
    push bx
    push dx
    mov bx, rd_pseg             ; ...and 0 in it is what every trampoline
    call rd_mfree               ; reads as "not loaded"
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_dlg_land - OSAPI_DRV_DLG's completion, in the RESIDENT (SPEC.md 51.10)
; in:  AL = the mode, ES:DI = the chosen name in KERNEL_SEG, DX:CX = its size
; out: nothing
;
; The kernel far-calls this through our own dispatcher, so it is an ordinary
; near proc. All it does is forward, because everything that decides what to
; DO with a name is layout and is over in the page - and rd_page_fwd asks
; whether the page is still here before it does.
; -----------------------------------------------------------------------------
rd_dlg_land:
    push ax
    mov al, RDT_DLGDONE         ; ...AL is the verb, so the MODE moves to AH,
    mov ah, [rd_dlgmode]        ; which is what the page reads it from
    jmp rd_page_fwd             ; ...which asks whether the image is here

; -----------------------------------------------------------------------------
; rd_dlg_ask - RSV_DLG: put a Standard File dialog up for the page
; in:  AL = FDLG_OPEN / FDLG_SAVE, SI = a default name in the PAGE's segment
;      or 0
; out: CF = 0 the dialog is up; CF = 1 refused
; clobbers: AX, CX, SI, DI, ES - it is reached only from rd_svc_page, whose
;      frame puts every one of those back but AX, which is that call's own
;
; THE NAME IS STAGED HERE, because it is in the page's segment and the kernel
; reads a default through the CALLER's - which for OSAPI_DRV_DLG is ours, this
; being the image with the drv_tab row. DS is the page's on entry to rd_svc
; only in the sense that the page set it; by the time we are here DS is ours
; again, so the copy reads through the page's segment explicitly - which is
; the ES rd_svc_page loaded.
; -----------------------------------------------------------------------------
rd_dlg_ask:
    mov [rd_dlgmode], al
    mov di, rd_dlgname
    mov byte [di], 0
    or si, si
    jz .named
    mov cx, 12
    call rd_scopy_in            ; ES:SI -> DS:DI, every register kept
.named:
    push ds
    pop es                      ; ES:SI = the default, in OUR segment
    mov si, di
    cmp byte [si], 0
    jne .go
    xor si, si                  ; nothing to suggest: SPEC.md 38.10's
.go:                            ; remembered name, and NOT an empty string at
                                ; a non-zero address
    mov di, rd_dlg_land
    call OSAPI_DRV_DLG          ; AL = the mode, still; its CF is ours
    ret

; =============================================================================
; rd_svc_page - the PAGE calling US (SPEC.md 62.9.9)
;
; in:  AL = an RSV_*, DS = ours (the page set it before the far call, so this
;      is ordinary near code), other registers per the verb
; out: CF, and AL where the verb says so
; clobbers: AX and the flags; everything else is preserved, because the page
;      calls this from the middle of its own routines
;
; ES = the page's segment for the whole of it, which is where a name or a
; status block lives.
; =============================================================================
rd_svc_page:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [rd_pseg]
    cmp al, RSV_MAXV
    ja .bad                     ; a verb we do not know: refuse rather than
    cbw                         ; fall into one we do. AH = 0 for AL <= the
    shl ax, 1                   ; table's last row, and no verb takes AX in -
    xchg ax, di                 ; AL is the verb on every one of them - so DI
    call [di+rd_sv_tab]         ; can index the table
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

rd_sv_tab:                      ; indexed by RSV_*, so the order IS the ABI
    dw rd_sv_state              ; RSV_STATE
    dw rd_sv_size               ; RSV_SIZE
    dw rd_sv_loc                ; RSV_LOC
    dw rd_sv_mount              ; RSV_MOUNT
    dw rd_sv_unmount            ; RSV_UNMOUNT
    dw rd_sv_preserve           ; RSV_PRESERVE
    dw rd_sv_load               ; RSV_LOAD
    dw rd_sv_clear              ; RSV_CLEAR
    dw rd_sv_dlg                ; RSV_DLG
RSV_MAXV    equ ($ - rd_sv_tab) / 2 - 1
%if RSV_MAXV != RSV_DLG
  %error "ramdisk: rd_sv_tab does not end at RSV_DLG - a row missing?"
%endif

; --- RSV_STATE: everything the page draws from, in one call ------------------
; Asked on every paint, and it has to be: the panel stays clickable underneath
; a dialog's completion and a mount can happen between two paints.
rd_sv_state:
    mov di, si                  ; ES:DI = the page's block
    mov ax, [rd_kb]
    mov [es:di+RDS_KB], ax
    mov al, [rd_loc]
    mov [es:di+RDS_LOC], al
    mov al, [rd_vol]
    mov [es:di+RDS_VOL], al
    add al, 1                   ; CF = 1 exactly when it was 0xFF, none...
    sbb al, al                  ; ...so 0xFF for none and 0 for a volume...
    inc al                      ; ...and 0 / 1, which is RDS_MNT
    mov [es:di+RDS_MNT], al
    call rd_xms_here            ; ...the SAME test rd_cfg_load refuses a
    sbb al, al                  ; carried-in `Xms` with, so the greying and
    inc al                      ; the fallback cannot disagree. A FACT the code
                                ; can test, which is what SPEC.md 47 rule 3
    mov [es:di+RDS_XMS], al     ; asks of a greying test
    xor ax, ax
    cmp byte [rd_vol], 0xFF
    je .free
    call rd_free_ext            ; free EXTENTS times the LIVE granule. It used
    mul word [rd_extkb]         ; to read `one extent is 1KB, so the count IS
    or dx, dx                   ; the KB` - true when it was written and false
    jz .free                    ; the moment the extent size started following
    mov ax, 0xFFFF              ; the store's (SPEC.md 62.9.10.5). A 16MB disk
.free:                          ; reported 2,048K free of 16,384K, which reads
                                ; as a disk that is 7/8 full and is a disk that
                                ; is empty. DX cannot be set at RD_MAXKB, but
                                ; saturating costs four bytes and is what a
                                ; larger ceiling would need
    mov [es:di+RDS_FREEK], ax
    call rd_nfiles              ; 0 with no store: it asks [rd_have] itself,
    mov [es:di+RDS_NFILE], ax   ; the directory going when the store does
    xor al, al
    test byte [rd_flags], RDCF_BOOT
    jz .boot
    cmp byte [rd_iname], 0
    je .boot
    inc ax
.boot:
    mov [es:di+RDS_BOOT], al
    xor al, al                  ; READ-AND-CLEAR: a verdict that outlived its
    xchg al, [rd_err]           ; cause is reported again on the next
    mov [es:di+RDS_ERR], al     ; unrelated paint (SPEC.md 59.5). RDERR_NONE
                                ; is 0, which is what the xchg leaves behind
    mov si, rd_iname            ; the remembered name, into the page's block
    add di, RDS_NAME
    mov cx, 13
    jmp rd_scopy                ; ...whose ret is this verb's, CF = 0

; --- RSV_SIZE / RSV_LOC: staged settings, never a disk write (SPEC.md 31.8) --
; Both are refused while the volume is up, and that is the SAME predicate the
; page greys the control with (SPEC.md 47 rule 2): a store cannot be resized
; or moved underneath a live volume, and the honest control is one that says
; so rather than one that silently unmounts.
rd_sv_size:
    cmp byte [rd_vol], 0xFF
    jne rd_sv_busy
    mov ax, bx                  ; BX is the size: AL was the verb
    call rd_kb_clamp
    mov [rd_kb], ax
    jmp short rd_sv_mark

rd_sv_loc:
    cmp byte [rd_vol], 0xFF
    jne rd_sv_busy
    cmp bl, RDL_XMS
    ja rd_sv_busy
    mov [rd_loc], bl
    jmp short rd_sv_mark

rd_sv_busy:
    stc
    ret

; --- RSV_CLEAR: forget the ASSOCIATION, and never the file -------------------
rd_sv_clear:
    xor ax, ax
    mov [rd_iname], al
    mov [rd_flags], al
    mov [rd_idrv], al
    mov [rd_icwd], ax
rd_sv_mark:
    call rd_cfg_mark
    clc
    ret

rd_sv_unmount:
    call rd_unmount
    clc
    ret

rd_sv_mount:
    call rd_mount
    jmp short rd_sv_verdict

; --- RSV_PRESERVE / RSV_LOAD -------------------------------------------------
; SI = a name in the PAGE's segment, or 0 for the remembered one. A name given
; here comes from a dialog that has just committed, so the volume is standing
; in the folder the user chose (SPEC.md 38.10) and rd_here_take is what turns
; that into the (volume, folder) the boot restore will use. ONE body for the
; two, the verb's own routine in AX: everything around it is the same.
rd_sv_preserve:
    mov ax, rd_preserve
    jmp short rd_sv_image
rd_sv_load:
    mov ax, rd_load
rd_sv_image:
    or si, si
    jz .have
    call rd_name_take
    call rd_here_take
    call ax
    jmp short rd_sv_verdict
.have:
    call rd_here_bank
    call rd_here_go
    call ax
    call rd_here_back           ; ...which preserves the flags and AL
rd_sv_verdict:                  ; CF = 1 and AL = RDERR_*: bank it for the
    jnc .ok                     ; page's next paint, CF untouched
    mov [rd_err], al
.ok:
    ret

rd_sv_dlg:
    mov al, bl                  ; the mode: AL was the verb
    jmp rd_dlg_ask

; -----------------------------------------------------------------------------
; rd_name_take - ES:SI (a name in the page's segment) -> [rd_iname]
; in:  ES = the page's segment, SI = the name
; out: nothing
; clobbers: CX, DI - rd_svc_page's frame is the only caller and restores both
; -----------------------------------------------------------------------------
rd_name_take:
    mov di, rd_iname
    mov cx, 12
    jmp rd_scopy_in
