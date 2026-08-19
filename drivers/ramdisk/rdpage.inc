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

rd_pseg:     dw 0               ; the page's segment, 0 = not loaded
rd_pfar:     dw 0, 0            ; ...as a far pointer to its dispatcher
rd_pent:     dw 0               ; ...and its entry offset, out of the image's
                                ; own header at +6. The dispatcher is `call
                                ; bp`, so BP is an ADDRESS and never the verb
rd_pvol:     db 0               ; the SYSTEM volume, banked at DRVV_READY
rd_pcwd:     dw 0
rd_pvsav:    db 0               ; ...and the caller's, banked across a load
rd_pcsav:    dw 0
rd_pname:    db 'RAMPAGE.DRV', 0
rd_pdlg:     db 0               ; 1 = a dialog of ours is up, so the image may
                                ; not be freed under its completion

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
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the directory, BL = its drive
    mov [rd_pcwd], dx
    mov [rd_pvol], bl
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_need - the page, in memory (module internal)
; in:  nothing
; out: CF = 0 and [rd_pseg] names it; CF = 1 = it could not be brought in
; clobbers: flags
;
; BP IS SAVED HERE and that is not housekeeping: the caller's verb is still to
; be dispatched, and a BP that comes back changed dispatches the WRONG ONE.
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
    push bp
    push es

    call OSAPI_FILE_HERE        ; where the USER is, to be put back
    mov [rd_pcsav], dx
    mov [rd_pvsav], bl
    mov dx, [rd_pcwd]
    mov bl, [rd_pvol]
    call OSAPI_FILE_GOTO        ; ...and off to the system volume
    jc .fail

    mov ax, RAMPAGE_KB
    call OSAPI_MEM_CLAIM        ; owner = our segment, so drv_release sweeps it
    jc .fail                    ; even if we never get to free it
    mov [rd_pseg], dx

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

    mov ax, [rd_pseg]           ; the dispatcher, as a far pointer
    mov word [rd_pfar], PKG_DISP
    mov [rd_pfar+2], ax
    mov ax, [es:6]              ; ...and the entry the dispatcher will call
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
    mov dx, [rd_pseg]
    call OSAPI_MEM_FREE
    mov word [rd_pseg], 0
.fail:
    call rd_page_back
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
    cmp byte [es:10], RD_ABI_VER    ; the header's spare word, which the
    jne .no                         ; overlay macro puts the ABI version in
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rd_page_call - one far call into the page
; in:  AL = an RDT_*; other registers per the verb
; out: whatever the verb answers, in CF and AX
;
; drv_call's shape (SPEC.md 51.2): BP is the page's ENTRY OFFSET, because the
; dispatcher at PKG_DISP is `call bp / retf` and BP is therefore an address,
; not something handed on to the callee. The verb rides AL.
;
; DS becomes the page's segment so its near code works. ES is left alone: a
; page click arrives with ES = KERNEL_SEG and the page's callbacks expect it.
; -----------------------------------------------------------------------------
rd_page_call:
    push ds
    push bp
    mov bp, [rd_pent]
    mov ds, [rd_pseg]
    call far [cs:rd_pfar]
    pop bp
    pop ds
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
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call rd_page_need
    jc .gone
    mov al, RDT_PAINT
    call rd_page_call
    jmp short .out
.gone:
    push ds
    pop es                      ; font_str reads the string through the X
    mov al, CBLACK              ; stub's ES, which is our own DS
    call OSAPI_SET_COLOR
    mov cx, di
    add cx, 2
    add dx, 4
    mov si, rd_s_nopage
    call OSAPI_FONT_STR
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_page_click - DSV_CPCLICK
; in:  DI = pane left, DX = pane top, CX = rel x, BX = rel y
; out: nothing
;
; The page is loaded by the PAINT that precedes any click on it, so this does
; not load: a click arriving with no image is a click on the apology above,
; and there is nothing there to press.
; -----------------------------------------------------------------------------
rd_page_click:
    cmp word [rd_pseg], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, RDT_CLICK
    call rd_page_call
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rd_page_up   - DSV_CPUP   (SPEC.md 13.8.4)
; rd_page_drag - DSV_CPDRAG (SPEC.md 13.8.1/13.8.4)
; in:  the same four registers DSV_CPCLICK takes
; out: nothing
;
; Two more trampolines of rd_page_click's exact shape, and for its exact
; reason: neither LOADS the image. A release or a drag can only follow a press
; that this page took, and a press can only follow a paint - so an image that
; is not here is one that never armed anything, and there is nothing over
; there for either verb to spend.
; -----------------------------------------------------------------------------
rd_page_up:
    cmp word [rd_pseg], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, RDT_UP
    call rd_page_call
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

rd_page_drag:
    cmp word [rd_pseg], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, RDT_DRAG
    call rd_page_call
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rd_page_key - DSV_CPKEY (SPEC.md 31.9.2)
; in:  BL = ASCII, BH = the scan code; the gfx lock is HELD
; out: CF = 0 the page took it; CF = 1 it did not
;
; Like the click, this does NOT load the image: a page has been painted before
; anything can be typed into it, and a key arriving with no image is a key
; aimed at the one-line apology, which has nothing to type into.
;
; **THE KEY IS IN BX AND NOT IN AX, AND THAT IS THE KERNEL'S CONSTRAINT RATHER
; THAN OURS** - drv_cp_call takes the CLASS in AL (SPEC.md 31.9), so AX is the
; one register a page ABI may not carry an argument in. It lands here in the
; registers RDT_KEY wants it in anyway, AL being the verb on every RDT_* call.
; -----------------------------------------------------------------------------
rd_page_key:
    cmp word [rd_pseg], 0
    je .no
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, RDT_KEY
    call rd_page_call
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret                         ; the page's own CF
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; rd_page_closed - DSV_CPCLOSE: the panel has gone, so the page may (31.9.1)
; in:  nothing; the gfx lock is HELD
; out: nothing (all registers preserved)
;
; NOT A DETACH: we are still loaded, the volume is still mounted, and the
; panel may be back in a second. What goes is what the PAGE was using.
;
; A DIALOG OF OURS CAN OUTLIVE THE PANEL - the panel's close box is clickable
; while a Save dialog is up only in the sense that it is not, the dialog being
; modal, but the panel can be closed from the System menu's Restart path and
; the ordering is not ours to assume. So the image is kept while [rd_pdlg] is
; set, and goes at the next close or at detach.
; -----------------------------------------------------------------------------
rd_page_closed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp word [rd_pseg], 0
    je .out
    cmp byte [rd_pdlg], 0
    jne .out
    mov al, RDT_BUSY
    call rd_page_call
    jc .out                     ; the page says it is not finished
    call rd_page_drop
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    push ax
    push dx
    cmp word [rd_pseg], 0
    je .out
    mov dx, [rd_pseg]
    call OSAPI_MEM_FREE
    mov word [rd_pseg], 0
    mov word [rd_pfar], 0
    mov word [rd_pfar+2], 0
    mov word [rd_pent], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_dlg_land - OSAPI_DRV_DLG's completion, in the RESIDENT (SPEC.md 51.10)
; in:  AL = the mode, ES:DI = the chosen name in KERNEL_SEG, DX:CX = its size
; out: nothing
;
; The kernel far-calls this through our own dispatcher, so it is an ordinary
; near proc. All it does is forward, because everything that decides what to
; DO with a name is layout and is over in the page.
;
; [rd_pdlg] is cleared FIRST: a completion is the end of the dialog whatever
; the page then does with it, and a page that opens the next one re-arms it.
; -----------------------------------------------------------------------------
rd_dlg_land:
    mov byte [rd_pdlg], 0
    cmp word [rd_pseg], 0
    je .out
    push ax
    push bx
    push si
    push bp
    mov al, RDT_DLGDONE         ; ...AL is the verb, so the MODE moves to AH,
    mov ah, [rd_dlgmode]        ; which is what the page reads it from
    call rd_page_call
    pop bp
    pop si
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rd_dlg_ask - RSV_DLG: put a Standard File dialog up for the page
; in:  AL = FDLG_OPEN / FDLG_SAVE, SI = a default name in the PAGE's segment
;      or 0
; out: CF = 0 the dialog is up; CF = 1 refused
;
; THE NAME IS STAGED HERE, because it is in the page's segment and the kernel
; reads a default through the CALLER's - which for OSAPI_DRV_DLG is ours, this
; being the image with the drv_tab row. DS is the page's on entry to rd_svc
; only in the sense that the page set it; by the time we are here DS is ours
; again, so the copy reads through the page's segment explicitly.
; -----------------------------------------------------------------------------
rd_dlg_ask:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov [rd_dlgmode], al
    mov di, rd_dlgname
    mov byte [di], 0
    or si, si
    jz .named
    mov es, [rd_pseg]
    mov cx, 12
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.named:
    push ds
    pop es                      ; ES:SI = the default, in OUR segment
    mov si, rd_dlgname
    cmp byte [si], 0
    jne .go
    xor si, si                  ; nothing to suggest: SPEC.md 38.10's
.go:                            ; remembered name, and NOT an empty string at
                                ; a non-zero address
    mov di, rd_dlg_land
    mov al, [rd_dlgmode]
    call OSAPI_DRV_DLG
    jc .no
    mov byte [rd_pdlg], 1
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
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
    cmp al, RSV_STATE
    je rd_sv_state
    cmp al, RSV_SIZE
    je rd_sv_size
    cmp al, RSV_LOC
    je rd_sv_loc
    cmp al, RSV_MOUNT
    je rd_sv_mount
    cmp al, RSV_UNMOUNT
    je rd_sv_unmount
    cmp al, RSV_PRESERVE
    je rd_sv_preserve
    cmp al, RSV_LOAD
    je rd_sv_load
    cmp al, RSV_CLEAR
    je rd_sv_clear
    cmp al, RSV_DLG
    je rd_sv_dlg
    stc                         ; a verb we do not know: refuse rather than
    jmp rd_sv_out               ; fall into one we do

; --- RSV_STATE: everything the page draws from, in one call ------------------
; Asked on every paint, and it has to be: the panel stays clickable underneath
; a dialog's completion and a mount can happen between two paints.
rd_sv_state:
    mov di, si                  ; ES:DI = the page's block
    mov ax, [rd_kb]
    mov [es:di+RDS_KB], ax
    mov al, [rd_loc]
    mov [es:di+RDS_LOC], al
    mov al, 1
    cmp byte [rd_vol], 0xFF
    jne .mnt
    xor al, al
.mnt:
    mov [es:di+RDS_MNT], al
    mov al, [rd_vol]
    mov [es:di+RDS_VOL], al
    call rd_xms_here            ; ...the SAME test rd_cfg_load refuses a
    mov al, 1                   ; carried-in `Xms` with, so the greying and
    jnc .xms                    ; the fallback cannot disagree. A FACT the code
    xor al, al                  ; can test, which is what SPEC.md 47 rule 3
.xms:                           ; asks of a greying test
    mov [es:di+RDS_XMS], al
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
    xor ax, ax
    cmp byte [rd_have], 0
    je .files
    call rd_nfiles
.files:
    mov [es:di+RDS_NFILE], ax
    xor al, al
    test byte [rd_flags], RDCF_BOOT
    jz .boot
    cmp byte [rd_iname], 0
    je .boot
    mov al, 1
.boot:
    mov [es:di+RDS_BOOT], al
    mov al, [rd_err]
    mov [es:di+RDS_ERR], al
    mov byte [rd_err], RDERR_NONE   ; READ-AND-CLEAR: a verdict that outlived
                                    ; its cause is reported again on the next
                                    ; unrelated paint (SPEC.md 59.5)
    push si
    mov si, rd_iname            ; the remembered name, into the page's block
    add di, RDS_NAME
    mov cx, 13
.name:
    mov al, [si]
    mov [es:di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .name
    mov byte [es:di], 0
.named:
    pop si
    clc
    jmp rd_sv_out

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
    call rd_cfg_mark
    clc
    jmp rd_sv_out

rd_sv_loc:
    cmp byte [rd_vol], 0xFF
    jne rd_sv_busy
    mov al, bl
    cmp al, RDL_XMS
    ja rd_sv_busy
    mov [rd_loc], al
    call rd_cfg_mark
    clc
    jmp rd_sv_out

rd_sv_busy:
    stc
    jmp rd_sv_out

rd_sv_mount:
    call rd_mount
    jnc .ok
    mov [rd_err], al
    stc
    jmp rd_sv_out
.ok:
    clc
    jmp rd_sv_out

rd_sv_unmount:
    call rd_unmount
    clc
    jmp rd_sv_out

; --- RSV_PRESERVE / RSV_LOAD -------------------------------------------------
; SI = a name in the PAGE's segment, or 0 for the remembered one. A name given
; here comes from a dialog that has just committed, so the volume is standing
; in the folder the user chose (SPEC.md 38.10) and rd_here_take is what turns
; that into the (volume, folder) the boot restore will use.
rd_sv_preserve:
    or si, si
    jz .have
    call rd_name_take
    call rd_here_take
    call rd_preserve
    jmp short .done
.have:
    call rd_here_bank
    call rd_here_go
    call rd_preserve
    pushf
    call rd_here_back
    popf
.done:
    jnc .ok
    mov [rd_err], al
    stc
    jmp rd_sv_out
.ok:
    clc
    jmp rd_sv_out

rd_sv_load:
    or si, si
    jz .have
    call rd_name_take
    call rd_here_take
    call rd_load
    jmp short .done
.have:
    call rd_here_bank
    call rd_here_go
    call rd_load
    pushf
    call rd_here_back
    popf
.done:
    jnc .ok
    mov [rd_err], al
    stc
    jmp rd_sv_out
.ok:
    clc
    jmp rd_sv_out

; --- RSV_CLEAR: forget the ASSOCIATION, and never the file -------------------
rd_sv_clear:
    mov byte [rd_iname], 0
    mov byte [rd_flags], 0
    mov byte [rd_idrv], 0
    mov word [rd_icwd], 0
    call rd_cfg_mark
    clc
    jmp rd_sv_out

rd_sv_dlg:
    mov al, bl                  ; the mode: AL was the verb
    call rd_dlg_ask
rd_sv_out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_name_take - ES:SI (a name in the page's segment) -> [rd_iname]
; in:  ES = the page's segment, SI = the name
; out: nothing
; -----------------------------------------------------------------------------
rd_name_take:
    push ax
    push cx
    push si
    push di
    mov di, rd_iname
    mov cx, 12
.next:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret
