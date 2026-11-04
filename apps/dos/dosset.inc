; =============================================================================
; os8088 - apps/dos/dosset.inc
;
; THE SETUP PAGE (SPEC.md 96.32.2.3): everything that DRAWS, LAYS OUT or
; ANSWERS A PRESS on the DOS window's Setup page, and the .LNK WRITER behind
; its `Save Shortcut` - one source, two shapes:
;
;   DOS_SETPART   kern_big's and kern_emu's DOS.O88: PART 5, far code read by
;                 the host's op_self when Setup opens and freed when it closes.
;                 apps/dos/dos.asm includes this file at its very END, inside
;                 `section .setp vstart=0`, and only in the -DDOS_SETEMIT
;                 assembly the Makefile cuts the part out of
;                 (tools/dossetchk.py); the shipped host assembly never sees it
;   otherwise     inline in the host's .text, where it always was: kern_mega's
;                 DOS.O88 (its host lives above 1MB, OS88_HIGH) and a build
;                 with no part table (build/dos.o88, DOSTRACE)
;
; THE PART RUNS WITH THE HOST'S DS, so every host DATA name below is right as
; written - this file is assembled in the host's own assembly and its labels
; are the host's offsets. What cannot be right as written is a NEAR CALL into
; the host: the part has a CS of its own. Every one is spelled
; `call SPH(routine)`, which inline is the routine itself and in the part is a
; stub that stores the target in [dos_sp_tgt] and far-calls the host's
; bouncer, `call [dos_sp_tgt] / retf` - register- and flag-transparent, so
; every routine keeps its contract. A near branch that leaves the part
; assembles SILENTLY (nasm's bin format resolves it against the other
; section), so tools/dossetchk.py disassembles the part's code and fails the
; build on any. Part DATA is read through CS and nothing else: the one table
; here is the Shell Link header, after sp_code_end.
;
; WHAT IS NOT HERE stays resident because a LAUNCH reads it, the owner's
; line: the .LNK reader, every setting's value and record, and everything
; dos_run asks (dos_mem_fix, dos_mem_whole, dos_mem_take, and the two terms of
; the arena figure that it shares: dos_spmask, dos_pkt_want) -
; and the strings the page draws, which os88ui's records and font_run read
; through DS, and dos_sav_done, which the kernel dispatches by offset.
; =============================================================================

%ifdef DOS_SETPART
 %define SPH(x) sph_ %+ x           ; a host routine, through the bouncer
 %define SP_BEGOTO sph_begoto       ; ...and the core's dos_be_goto, whose
                                    ; name is a slot's %xdefine and cannot be
                                    ; pasted into a stub's
 %define sph_os88ui_chk os88ui_chk  ; ...but the box, the radio and the dial
 %define sph_os88ui_chkhit os88ui_chkhit ; are THIS part's code (os88ui.inc's
 %define sph_os88ui_rad os88ui_rad  ; OS88UI_CTLEXT, expanded below): their
 %define sph_os88ui_radhit os88ui_radhit ; SPH() is a plain near call
 %define sph_os88ui_drop os88ui_drop
 %define sph_os88ui_drpress os88ui_drpress
%else
 %define SPH(x) x                   ; inline: the routine itself
 %define SP_BEGOTO dos_be_goto
%endif

%ifdef DOS_SETPART
; --- the part's head, at fixed offsets (SPH_* in apps/dos/dos.asm) ----------
sp_base:
    jmp short sp_door               ; 0: the ONE far entry, AL = SPD_*
    dw sp_code_end                  ; 2: where the code ends (dossetchk.py)
sp_hostfar:
    dw dos_sp_bounce, 0             ; 4: the host's bouncer: its offset is
                                    ;    this assembly's own label, and its
                                    ;    CS - the host's DS - sp_door writes
%if sp_hostfar - sp_base != SPH_HOST
 %error "the Setup part's head moved: dos_sp_get writes the bouncer at SPH_HOST"
%endif
%endif

%ifdef DOS_SETPART
; -----------------------------------------------------------------------------
; sp_door - the Setup part's five doors, far-called at its offset 0 by
;           dos_spdo (apps/dos/dos.asm)
; in:  AL = SPD_*; BX = the window; for SPD_CLICK also CX/DX = the point.
;      DS = the host's, the gfx lock held (every caller is a UI callback)
; out: CF = 0. AL is spent; every other register as the routine behind the
;      door leaves it - which is every one, but SPD_SAVE's BX
; Inline, the host's call sites ARE these bodies (dos.asm's SPB_* macros).
; -----------------------------------------------------------------------------
sp_door:
    mov [cs:sp_hostfar+2], ds       ; the way back: the host's CS IS its DS
    cmp al, SPD_PLACE
    je .place
    cmp al, SPD_CLICK
    je .click
    cmp al, SPD_ROW
    je .row
    cmp al, SPD_SAVE
    je .save
    SPB_PAINT                       ; --- SPD_PAINT
    jmp short .out
.place:
    SPB_PLACE                       ; --- SPD_PLACE: dos_place's Setup half
    jmp short .out
.click:
    SPB_CLICK .out                  ; --- SPD_CLICK: every control on it
    jmp short .out
.row:
    call dos_mem_row                ; --- SPD_ROW: the arena line alone
    jmp short .out
.save:
    call dos_sav_go                 ; --- SPD_SAVE: the .LNK, and its dialog
.out:
    clc
    retf
%endif                              ; DOS_SETPART

%ifdef DVW
; -----------------------------------------------------------------------------
; sp_wck - a press on `Run in a window` (SPEC.md 96.32.2.4)
; in:  BX = the window, CX/DX = the point; out: CF = 0 it was ours (toggled,
;      and everything that depends on it redrawn), CF = 1 not ours
; GREYED UNDER ARM 1 AND NOT OFFERED there (SPEC.md 47 rule 6): a window
; cannot shut the OS down, and dos_run ignores the box on that arm.
; -----------------------------------------------------------------------------
sp_wck:
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .no
%ifdef DOS_WTAIL
    cmp byte [dos_mhead + OS88UI_CK_ON], 0  ; ...and while `Close when done`
    jne .no                         ; is ticked: the two are exclusive (96.44.20)
%endif
    push bx
    mov bx, dos_mwin
    call SPH(os88ui_chkhit)         ; CF = 0 toggled and redrawn
    pop bx
    jc .r
    push bx                         ; ...and what it decides: arm 0's two
    push di                         ; driver boxes grey with it (no driver
    call dos_mck_place              ; comes out for a window, 96.54) and the
    mov bx, dos_mhdd                ; arena row is the window's figure
    call dos_mck_di
    call SPH(os88ui_chk)
    mov bx, dos_mnet
    call dos_mck_di
    call SPH(os88ui_chk)
    pop di
    pop bx
    call dos_mem_row
%ifdef DOS_WTAIL
    call sp_wdraw1                  ; ...and `Close when done` greys with it
%endif
    clc
.r:
    ret
.no:
    stc
    ret

%ifdef DOS_WTAIL
; -----------------------------------------------------------------------------
; sp_hck - a press on `Close when done` (SPEC.md 96.44.20)
; in:  BX = the window, CX/DX = the point; out: CF = 0 it was ours, CF = 1 not
; Greyed, and so not ours, while `Run in a window` is ticked - a run in a
; window IS a window - and for a Windows program, which opens in WIN16's
; window whatever the box says ([dos_hne], dos_hne_ask)
; -----------------------------------------------------------------------------
sp_hck:
    cmp byte [dos_mwin + OS88UI_CK_ON], 0
    jne .no
    cmp byte [dos_hne], 0
    jne .no
    push bx
    mov bx, dos_mhead
    call SPH(os88ui_chkhit)         ; CF = 0 toggled and redrawn
    pop bx
    jc .r
    call sp_wdraw1                  ; ...and `Run in a window` greys with it
    call dos_mem_row                ; ...and the figure moves: headless is the
    clc                             ; bracket with part 1 left out (96.44.20)
.r:
    ret
.no:
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; sp_wdraw - `Run in a window`, placed and drawn as it stands
; sp_wdraw1 - ...already placed (dos_paint_furn's own dos_furn_rects did it)
; in:  BX = the window; every register preserved
; -----------------------------------------------------------------------------
sp_wdraw:
    call dos_furn_rects
sp_wdraw1:
    push bx
    push di
    xor di, di
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    jne .live
    mov di, OS88UI_DIS              ; arm 1's, in the disabled pen (47 rule 2)
.live:
%ifdef DOS_WTAIL
    cmp byte [dos_mhead + OS88UI_CK_ON], 0  ; ...and while `Close when done`
    je .wl                          ; is ticked (SPEC.md 96.44.20)
    mov di, OS88UI_DIS
.wl:
%endif
    mov bx, dos_mwin
    call SPH(os88ui_chk)
%ifdef DOS_WTAIL
    xor di, di                      ; --- `Close when done`, beside it -------
    cmp byte [dos_mwin + OS88UI_CK_ON], 0
    jne .hd
    cmp byte [dos_hne], 0
    je .hl
.hd:
    mov di, OS88UI_DIS
.hl:
    mov bx, dos_mhead
    call SPH(os88ui_chk)
%endif
    pop di
    pop bx
    ret
%endif                              ; DVW

; -----------------------------------------------------------------------------
; dos_sav_go - the Save Shortcut button was pressed
; in:  BX = the window
;
; The kernel's Standard File dialog in SAVE mode (SPEC.md 38), with the
; completion proc below. The DEFAULT NAME is the program's own with .LNK on
; it, because that is what the user would type.
; -----------------------------------------------------------------------------
dos_sav_go:
    push ax
    push cx
    push si
    push di
    call dos_lnk_build              ; **BUILT BEFORE THE DIALOG OPENS**, and
    jc .out                         ; that is not an ordering preference: the
    mov [dos_lend], cx              ; dialog NAVIGATES, so by the time its
                                    ; completion runs the instance stands
                                    ; wherever the user went - and the link's
                                    ; WORKING_DIR would be that folder rather
                                    ; than the program's. It read `\` before
                                    ; this moved
    call dos_sav_dfl                ; dos_wname = `NAME.LNK`
    mov bx, [dos_win]               ; **BX IS THE WINDOW WE WANT TO HEAR BACK
                                    ; ABOUT**, and leaving it out is a dialog
                                    ; given a garbage pointer - which opens,
                                    ; closes on Enter, and writes nothing
    mov si, dos_wname
    mov di, dos_sav_done
    mov al, FDLG_SAVE
    call OSAPI_FILE_DLG             ; CF=1 = one is already up, or no room -
.out:                               ; and a refusal needs no report: the user
    pop di                          ; pressed a button and nothing happened,
    pop si                          ; which is what a busy dialog looks like
    pop cx
    pop ax
    ret

; --- dos_sav_dfl - `NAME.LNK` from dos_name, into dos_sbuf ------------------
dos_sav_dfl:
    push ax
    push cx
    push si
    push di
    mov si, dos_name
    mov di, dos_wname
    mov cx, 8
.c:
    mov al, [si]
    or al, al
    jz .ext
    cmp al, '.'
    je .ext
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .c
.ext:
    mov byte [di+0], '.'
    mov byte [di+1], 'L'
    mov byte [di+2], 'N'
    mov byte [di+3], 'K'
    mov byte [di+4], 0
    pop di
    pop si
    pop cx
    pop ax
    ret


; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_paint_furn - the setup area's title and its bottom row (SPEC.md 96.32.2)
; in:  BX = the window, gfx lock held (every painter has it)
; out: nothing; every register preserved
;
; ONE ROUTINE FOR EVERY SETUP PAGE, which is the point: `<`, `>`, Save Shortcut
; and Return are in the same place whatever is above them, so the pointer does
; not have to hunt and a third page costs a string rather than a layout. The
; TITLE is what says which page is up - a page that looked the same as its
; neighbour but behaved differently would be the worst of the three shapes.
; -----------------------------------------------------------------------------
dos_paint_furn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, ax
    add bx, DOS_SETPAD
    add dx, DOS_TITY
    mov si, dos_l_tset              ; THE page's name, there being one
    mov ax, (CWHITE << 8) | CBLACK
    mov cx, bx
    call OSAPI_FONT_RUN
    pop bx

    push bx                         ; ...and THE COMMAND, beside it (SPEC.md
    call dos_cmd_place              ; 96.32.2.2): the same box the main page's
    mov si, dos_pln                 ; bar carries, on the row that names the
    call SPH(os88line_draw)              ; page, because what every option below
    pop bx                          ; applies to is the command

    call dos_furn_rects             ; the two rects, both from one arithmetic
    push bx
    mov bx, dos_btrec
    mov al, DOS_BT_SAV
    call SPH(os88ui_btn)
    mov al, DOS_BT_RET
    call SPH(os88ui_btn)
    pop bx
%ifdef DVW
    call sp_wdraw1                  ; ...and `Run in a window` at the row's
%endif                              ; LEFT, placed by the same call (96.32.2.4)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_furn_rects - the bottom row's two buttons (SPEC.md 96.32.2)
; in:  BX = the window
; out: dos_srect and dos_trect filled; every register preserved
;
; dos_bar_rects' shape one page down, and for its reason: the two words are
; RIGHT-anchored, and both share a row
; whose y comes from the content BOX rather than a constant - so the row sits
; on the bottom of a CGA window and of a VGA one without a per-adapter number.
; -----------------------------------------------------------------------------
dos_furn_rects:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dos_btrec               ; the Setup page's pair, aimed here for
    mov word [si+OS88UI_BT_RECTS], dos_trect
    mov word [si+OS88UI_BT_LABELS], dos_bt_setl
    mov word [si+OS88UI_BT_N], 2    ; dos_bar_rects' reason
    call OSAPI_WM_GEOM              ; CX = content width, DX = its height
    jc .out
    mov di, cx
    sub dx, DOS_BTNH + DOS_FURNB    ; DX = the row's top, from the BOTTOM
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add dx, cx                      ; ...and now it is a screen row
    mov bx, dx
    add bx, DOS_BTNH - 1            ; BX = its last line, rects being inclusive

%ifdef DVW
    mov si, dos_mwin                ; --- 'Run in a window', LEFT-anchored
    mov word [si+OS88UI_CK_LABEL], dos_l_mwin   ; (SPEC.md 96.32.2.4), on
    mov cx, ax                      ; the row the buttons share, which has
    add cx, DOS_SETPAD              ; been empty at its left since the `<`
    mov [si+OS88UI_CK_RECT+0], cx   ; and `>` went (96.32.2.1)
    add cx, OS88UI_CKBOX + OS88UI_CKGAP + 8 * DOS_MWINW - 1
    mov [si+OS88UI_CK_RECT+4], cx
    mov [si+OS88UI_CK_RECT+2], dx
    mov [si+OS88UI_CK_RECT+6], bx
%ifdef DOS_WTAIL
    mov si, dos_mhead               ; --- ...and `Close when done` after it
    mov word [si+OS88UI_CK_LABEL], dos_l_mhead  ; (SPEC.md 96.44.20), the
    add cx, 1 + DOS_SETPAD          ; same width, a pad along
    mov [si+OS88UI_CK_RECT+0], cx
    add cx, OS88UI_CKBOX + OS88UI_CKGAP + 8 * DOS_MWINW - 1
    mov [si+OS88UI_CK_RECT+4], cx
    mov [si+OS88UI_CK_RECT+2], dx
    mov [si+OS88UI_CK_RECT+6], bx
%endif
%endif
    mov si, dos_trect               ; --- 'Return', right-anchored ---
    mov cx, ax
    add cx, di
    sub cx, DOS_SETPAD + DOS_RETW
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    push cx
    add cx, DOS_RETW - 1
    mov [si+4], cx
    pop cx
    mov si, dos_srect               ; --- ...and 'Save Shortcut' beside it ---
    sub cx, DOS_BARGAP + DOS_SAVW
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    add cx, DOS_SAVW - 1
    mov [si+4], cx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_paint_set - the Setup page's body (SPEC.md 96.32.2)
; in:  BX = the window, gfx lock held
; out: CF=0; every register preserved
;
; TWO HALVES. The left is what a RUN needs - the arguments and one environment
; row - and the right is §96.25's memory block, which used to be a page and is
; three lines and a control. The env box here IS Environment's first row: one
; buffer, two rects, so there is no mirroring to keep in step and no way for
; the user to type into the one nothing reads.
; -----------------------------------------------------------------------------
dos_paint_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, DOS_SETPAD
    add dx, DOS_BODYY
    mov ax, (CWHITE << 8) | CBLACK
    push dx
    mov si, dos_l_args
    call OSAPI_FONT_RUN             ; 'Arguments:'
    pop dx
    push dx
    add dx, DOS_SLBL2
    mov ax, (CWHITE << 8) | CBLACK
    mov si, dos_l_envb
    call OSAPI_FONT_RUN             ; 'Environment'
    pop dx
    pop bx

    push bx                         ; ...and the two boxes under them
    call dos_fld_place
    mov si, dos_ln
    call SPH(os88line_draw)
    pop bx
    push bx                         ; ...and EVERY environment row under it
    xor cx, cx                      ; (SPEC.md 96.32.2.1), where there used to
.erow:                              ; be one and a page button to reach the
    push cx                         ; other three
    call dos_erow
    call SPH(os88line_draw)
    pop cx
    inc cx
    cmp cx, DOS_ENVN
    jb .erow
    pop bx

    call dos_paint_mem              ; ...and the right half, which is §96.25's
                                    ; block drawn at an origin of its own
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_mck_place - the three check boxes and the drop-down's figures, put where
;                 the window is now (SPEC.md 96.36.7)
; in:  BX = the window; every register preserved
;
; ONE PLACER FOR ALL THREE, because they share an origin and a width and are
; recomputed on every paint and every click for dos_mfld_place's reason. It
; also reads the two CLASS FIGURES - what the hard disks and the network are
; holding - and banks them, because the label prints one and dos_mem_arena
; adds the same one, and two reads could answer differently across a driver
; being unloaded between them (SPEC.md 47 rule 5).
; -----------------------------------------------------------------------------
dos_mck_place:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                         ; dos_mem_num2 patches THROUGH DI, and the
                                    ; click path banks nothing in it any more -
                                    ; but the contract says every register, and
                                    ; a placer that quietly kept one would be
                                    ; the next silent defect of exactly this
                                    ; shape (SPEC.md 96.36.4)
    call dos_mem_org
    mov al, DRVC_DISK               ; WHAT EACH CLASS IS HOLDING, once
    call dos_classk                 ; (SPEC.md 51.12)
    mov [dos_mhkb], ax
    mov al, DRVC_NET
    call dos_classk
    mov [dos_mnkb], ax
    mov al, DRVC_SOUND              ; ...AND THE ONE WITH NO BOX (SPEC.md
    call dos_classk                 ; 96.36.7.2): dos_drv_take unmounts the
    mov [dos_msnk], ax              ; sound driver on EVERY arm and the user is
                                    ; never asked, because a DOS program cannot
                                    ; reach it - so it is a term in the figure
                                    ; and not a control

    ; --- ...AND THE CAPTIONS, WHICH ARE THE OTHER QUESTION (SPEC.md 51.12.1) -
    ; `(Up to NNK)` is about the CLASS and not about this machine's state, so
    ; it is a BUILD-TIME constant and the answer is the same on every machine.
    ; Fed the figures above it read `(Up to  0K)` on a machine with nothing of
    ; that class mounted - a true number that reads exactly like a page whose
    ; arithmetic has broken, and the state DOS is MOST often opened in,
    ; since a user shutting the OS down for a DOS program tends not to have a
    ; hard disk or a card in the first place.
    mov ax, DRVM_CEIL_DISK          ; ...and the ceiling is a CONSTANT and not
    mov di, dos_mhddk               ; a call (SPEC.md 51.12.1): the slot used
    call SPH(dos_mem_num2)               ; to walk a table of build-time figures to
    mov ax, DRVM_CEIL_NET_PCI       ; add them up, and the SDK carries the sum
                                    ; (every kernel DOS runs on has
                                    ; PCIETHER.DRV's row, the class's larger
                                    ; one: SPEC.md 125.5. kern_small, whose
                                    ; ceiling is DRVM_CEIL_NET, has no DOS)
    mov di, dos_mnetk               ; that walk was producing
    call SPH(dos_mem_num2)
    mov si, dos_mhdd                ; ...then the three rects, one pitch apart
    mov ax, dos_l_mhdd
    mov dx, DOS_MHDDY
    call dos_mck1
    mov si, dos_mnet
    mov ax, dos_l_mnet
    mov dx, DOS_MNETY
    call dos_mck1
    mov si, dos_mmou
    mov ax, dos_l_mmou
    mov dx, DOS_MMOUY
    call dos_mck1
%ifdef DOS_MEGA
    mov si, dos_mhma
    mov ax, dos_l_mhma
    mov dx, DOS_MHMAY
    call dos_mck1
    mov si, dos_msav
    mov ax, dos_l_msav
    mov dx, DOS_MSAVY
    call dos_mck1
%endif
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dos_classk - one class's KB, with the refusal folded into the figure
; in:  AL = a DRVC_*
; out: AX = the KB, 0 where the slot refused; clobbers flags
;
; Three call sites wanted the same two instructions after the slot. There
; used to be a third instruction and two more sites: the slot's answer once
; carried `DRVM_PLUS` in bit 15 and once had a CEILING form, and both went
; when it stopped quoting drv_memk (SPEC.md 51.12.2, 51.12.1) - what it
; weighs now is the heap, in plain kilobytes.
dos_classk:
    call OSAPI_DRV_CLASSK
    jnc .ok
    xor ax, ax                      ; CF = nothing of that class - the figure
.ok:                                ; is 0 and not whatever AX was left as
    ret

; dos_mck1 - one box's rect and label (internal)
; in:  SI = the record, AX = its label, DX = the row offset from [dos_my]
; out: nothing; AX, DX and SI preserved
dos_mck1:
    push ax
    push cx
    push dx
    mov [si+OS88UI_CK_LABEL], ax
    mov cx, [dos_mx]
    add cx, DOS_MSUBX
    mov [si+OS88UI_CK_RECT+0], cx
    add cx, OS88UI_CKBOX + OS88UI_CKGAP + 8 * DOS_MCKW
    dec cx                          ; ...x2 INCLUSIVE, os88ui_rad's rule
    mov [si+OS88UI_CK_RECT+4], cx
    add dx, [dos_my]
    mov [si+OS88UI_CK_RECT+2], dx
    add dx, OS88UI_CKBOX
    dec dx
    mov [si+OS88UI_CK_RECT+6], dx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mck_di - DI = OS88UI_DIS when the box at BX names a class nothing has
;              mounted (SPEC.md 47 rule 5, 96.36.7)
; in:  BX = dos_mhdd or dos_mnet
; out: DI; every other register preserved
;
; **GREY A FACT.** A box that would unmount a driver the machine has not got
; is offering to give back memory nobody is holding, and OSAPI_DRV_CLASSK's CF
; is that fact - banked in the figure by dos_mck_place, so this is a compare
; and not a second call. The box is forced ON with it: an unticked box means
; "take it out" and there is nothing to take out, so an off box would be
; describing an action rather than a state.
; -----------------------------------------------------------------------------
dos_mck_di:
    push ax
    mov di, OS88UI_DIS              ; **AND A SUBSECTION BELONGS TO ITS ARM**
    cmp bx, dos_mmou                ; (SPEC.md 96.36.9): a box whose arm is not
    jne .arm0                       ; the pick cannot be used, so it is greyed
%ifdef DOS_MEGA
    xor di, di                      ; ...BUT kern_mega's page draws no radio
    jmp short .out                  ; (KERN-MEGA-DOS-PLAN 12.1 item 1): every
                                    ; launch is the whole machine and
                                    ; KDLF_NOMOUSE is honoured on it, while
                                    ; [dos_keepc] stays arm 0 for the drivers'
                                    ; rows - so the arm test greyed this box for
                                    ; good (KERN-MEGA-PLAN 14.43's finding)
%endif
    cmp byte [dos_keepc], DOS_MEM_WHOLE ; - and a press on it falls through to
    jne .out                        ; the radio, which picks that arm
    xor di, di                      ; (96.36.4). Two clicks, and the first one
    jmp short .out                  ; says what the second will mean
.arm0:
    cmp byte [dos_keepc], DOS_MEM_IN
    jne .out
%ifdef DVW
    cmp byte [dos_mwin + OS88UI_CK_ON], 0   ; ...and NOT FOR A WINDOW
    jne .out                        ; (SPEC.md 96.32.2.4): a run in the window
%endif                              ; unmounts no driver and publishes no
    xor di, di                      ; **AND LIVE WHATEVER IS MOUNTED** (SPEC.md
.out:                               ; 96.36.7.1). It used to grey a box whose
    pop ax                          ; class had nothing loaded, and force it
    ret                             ; ON, on the ground that an unticked box
                                    ; would describe an action rather than a
                                    ; state. That reads the box as a REPORT
                                    ; about this machine, and it is a REQUEST
                                    ; about the program: `include these if
                                    ; available`. A .LNK outlives the setup it
                                    ; was saved under, so a machine with no
                                    ; card is exactly when the user needs to
                                    ; be able to say what they want on the
                                    ; machine that has one.
                                    ;
                                    ; The arithmetic needs nothing: the figure
                                    ; a clear box adds is the class's own, and
                                    ; a class with nothing loaded reports ZERO
                                    ; - so an empty class moves the estimate by
                                    ; 0 whichever way its box is set, which is
                                    ; the only honest answer about the machine
                                    ; in front of you. `Up to` in the label is
                                    ; what says the tick is about another one

; -----------------------------------------------------------------------------
; dos_mck_hit - test the box at BX against the banked press, if it is live
; in:  BX = one of the three records; [dos_mpx]/[dos_mpy] = the point
; out: CF = 0 it was HIT AND TOGGLED (the box redrew itself), CF = 1 it was
;      not ours - greyed, or the point is elsewhere
;
; It reloads the point rather than taking it in CX/DX because that is the one
; thing every caller here gets wrong: `dos_mck_di` answers in DI and
; `os88ui_drpress` zeroes it, so a point banked in a register does not survive
; the control before this one (SPEC.md 96.36.4).
; -----------------------------------------------------------------------------
dos_mck_hit:
    push cx
    push dx
    push di
    call dos_mck_di
    or di, di
    pop di
    jnz .no                     ; greyed: nothing to give, and the press
    mov cx, [dos_mpx]           ; belongs to the arm under it (SPEC.md 96.36.9)
    mov dx, [dos_mpy]
    call SPH(os88ui_chkhit)
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; dos_drop_place - the disk cache's box, and WHICH LIST IS IN IT (96.36.6)
; in:  BX = the window; every register preserved
;
; **THE LIST IS THE ARM'S**, and the pick is remembered per arm: arm 1 has the
; whole rung ladder because `kd_giveback` sheds one at a time, and arm 0 has
; Auto and Off because the kernel's claim is taken at a mount and is either
; standing or shed. Swapping the list without swapping the pick would leave
; SEL indexing past the short one, which draws a caption out of whatever
; follows the table.
; -----------------------------------------------------------------------------
dos_drop_place:
    push ax
    push cx
    push dx
    push si
    call dos_mem_org
    mov si, dos_mdr
    mov word [si+OS88UI_DR_WIN], bx
    mov word [si+OS88UI_DR_ITEMS], dos_ca_items     ; ONE LIST ON BOTH ARMS
    mov word [si+OS88UI_DR_N], DOS_CA_N             ; (96.36.6): since SPEC.md
                                    ; 18.95.8 arm 0 can hold a WIDTH too, so
                                    ; there is nothing to swap, nothing to park
                                    ; and no second length to clamp against
    mov ax, [dos_mx]
    add ax, DOS_MCACX
    mov [si+OS88UI_DR_RECT+0], ax
    add ax, DOS_MCACW
    dec ax
    mov [si+OS88UI_DR_RECT+4], ax
    mov dx, [dos_my]
    add dx, DOS_MCACY
    mov [si+OS88UI_DR_RECT+2], dx
    add dx, DOS_MCACH
    dec dx
    mov [si+OS88UI_DR_RECT+6], dx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mfld_place - put the memory limit field where the window is now
; in:  BX = the window; every register preserved
;
; dos_fld_place's shape and for its reason: os88line's rect is in SCREEN
; coordinates, so a banked one puts the caret one drag behind.
; -----------------------------------------------------------------------------
dos_mfld_place:
    push ax
    push cx
    push dx
    push si
    call dos_mem_org                ; the BLOCK's origin (SPEC.md 96.32.2),
    mov ax, [dos_mx]                ; never the content edge
    mov dx, [dos_my]
    mov si, dos_mln
    mov cx, ax
    add cx, DOS_MSUBX + DOS_MFLDX   ; ...indented into arm 0's subsection
    mov [si+LN_X1], cx
    add cx, DOS_MFLDW
    mov [si+LN_X2], cx
    mov cx, dx
    add cx, DOS_MFLDY
    mov [si+LN_Y1], cx
    add cx, DOS_FLDH
    mov [si+LN_Y2], cx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mrad_place - put the radio group's record where the window is now
; in:  BX = the window; every register preserved
;
; The RECT IS THE WHOLE CLICKABLE AREA - every ring, gap and label of every
; arm - which is os88ui_rad's own contract, so a press on the words counts and
; the row is worked out by dividing the press's y by the pitch. Recomputed
; from the content origin on every paint and every click for dos_fld_place's
; reason: the rect is in SCREEN coordinates and a window moves.
;
; THE OTHER FOUR FIELDS ARE SET HERE TOO, not once at entry, for the same
; reason the rect is: this runs before every paint and before every click, so
; a record filled in one place cannot disagree with itself. The exception is
; OS88UI_RD_SEL, which IS [dos_keepc] (SPEC.md 96.36) and is the user's.
;
; DOS_MEM_WHOLE's greying is DI's bit 2 and comes from dos_mem_whole - one
; predicate, three consumers (SPEC.md 47 rule 4), of which this is the one
; that makes the ring and the label dither.
; -----------------------------------------------------------------------------
dos_mrad_place:
    push ax
    push cx
    push dx
    push si
    push di
    call dos_mem_org                ; the BLOCK's origin, as above
    mov ax, [dos_mx]
    mov dx, [dos_my]
    xor di, di
    call SPH(dos_mem_whole)              ; CF = 1: the third arm cannot be picked.
    jnc .live                       ; **IT ANSWERS IN SI**, so it is asked
    mov di, 1 << DOS_MEM_WHOLE      ; BEFORE the record is pointed at and not
.live:                              ; after - the reason it returns and the
    mov si, dos_mrad                ; record live in the same register, and
                                    ; the wrong order writes the rect into the
                                    ; LABEL and draws the group at 0,0
    mov word [si+OS88UI_RD_ITEMS], dos_mem_items
    mov word [si+OS88UI_RD_N], DOS_MEM_N
    mov word [si+OS88UI_RD_PITCH], DOS_MRADP
    mov [si+OS88UI_RD_DIS], di
    mov cx, ax
    mov [si+0], cx
    add cx, OS88UI_RDBOX + OS88UI_RDGAP + 8 * DOS_MEMI_N
    mov [si+4], cx                  ; ...x2 INCLUSIVE, so the longest label's
    dec word [si+4]                 ; last cell is inside and the next is not
    mov cx, dx
    add cx, DOS_MRADY
    mov [si+2], cx
    mov cx, dx                      ; **AND y2 IS NOT y1 + N*PITCH** (SPEC.md
    add cx, DOS_MRADB               ; 96.36.4): two rows of the pitch would
    dec cx                          ; reach past the block, and os88ui_radhit
    mov [si+6], cx                  ; tests the rect FIRST and divides
                                    ; afterwards. It used to stop one line
                                    ; above the disk cache's box, which was
                                    ; below it; the dial is at the TOP now
                                    ; (96.36.6.3) so there is nothing left to
                                    ; stop short of and this is simply the
                                    ; block's own last line. It still has to
                                    ; clear arm 1's band MIDDLE - y1 + pitch +
                                    ; pitch/2 - which is what every kd* row
                                    ; clicks that arm at
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret


; -----------------------------------------------------------------------------
; dos_mem_arena - THE ONE FIGURE, as the controls under it stand (96.36.3)
; out: AX = the KB this page's settings would hand the program; CF = 1 when
;      it is an AUTOMATIC window's DVW_DEFKB, which a bigger program raises
;      (SPEC.md 96.54.2.1) - dos_mem_row's `+`
;
; It replaces the two numbers the page used to be a choice BETWEEN, because
; the page is no longer a choice between two numbers: it is an arm, three
; boxes, a dial and a cap, and what the user wants to know is what all of them
; together come to. So it is read on every paint and after every control that
; can move it - one row, live.
;
; **ARM 0's TERMS ARE EXACT AND ARM 1's ARE ESTIMATES**, which is what the '~'
; in front of the row is about. Inside the OS the kernel answers the same
; question `dos_run` asks, so what the page shows is what the program gets;
; the driver boxes add figures that are build-time constants (SPEC.md 51.12)
; and a driver's real claim can be a rung lower. Shutting the OS down cannot
; be asked of anything - the machine it describes does not exist yet - so it
; is conventional memory less what `kern_dos` is known to keep.
;
; **AND THE SAME QUESTION IS THE WHAT-IF, NOT PLAIN AVAIL** (SPEC.md 96.25.1.1).
; dos_run posts OSAPI_MEM_COMPACT and claims on the wake, so what it
; hands the program is a heap that has been packed with OUR OWN REGION IN THE
; PASS - which is exactly the question the what-if answers and exactly
; the one plain avail does not, a package being pinned by the act of asking
; (SPEC.md 66.4.3). It made no difference for a release because this region
; could not move at all: it is PART 0 of a re-homed DOS.O88 and the carve was
; refused its declaration, so the excuse bought nothing and the two slots
; agreed by accident. SPEC.md 66.6.1.2 unpinned it and the accident ended -
; measured on a 360KB desktop, the page said 442K/474K and the launch handed
; out 445K/477K, under-promising by the 3KB the region's own move recovers.
;
; The what-if is documented as a measurement rather than a promise, and for
; THIS package it is the promise: the post is what makes it true, and dos_run
; sends one whenever a pass would add anything (SPEC.md 66.4.3.2).
; -----------------------------------------------------------------------------
dos_mem_arena:
    push bx
    push cx
    push dx
    push di
    push es
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .whole

    ; --- ARM 0: the kernel's own answer, plus what the boxes would hand back --
    mov ax, DOS_PG_FLOOR            ; AH = MEMC_WHATIF, AL = the level, which is
    call OSAPI_MEM_COMPACT          ; the floor dos_run will set - ON EVERY RUNG
                                    ; of the dial now (SPEC.md 18.95.8), so this
                                    ; no longer picks between two levels. The
                                    ; what-if reads its level from AL rather
                                    ; than from the task's floor the way
                                    ; OSAPI_MEM_AVAIL does
%ifdef DOS_SETPART
    add ax, [dos_spwall]            ; ...PLUS what the page's own code walls
%endif                              ; off while it is held (SPEC.md 96.32.2.3.1)
    call dos_cache_give             ; ...PLUS what the dial hands back out of
    add ax, cx                      ; the cache, which is the one term the
%ifdef DVW
    cmp byte [dos_mwin + OS88UI_CK_ON], 0   ; **A WINDOW'S FIGURE** (SPEC.md
    je .full                        ; 96.32.2.4): no driver comes out for it
    mov cx, [dos_memkb]             ; and no packet driver is published, so
    jcxz .auto                      ; none of the terms below applies - only
    jmp short .capcx                ; the cap, which is the user's...
.auto:                              ; ...or AUTOMATIC (96.54.2.1): DVW_DEFKB,
    cmp ax, DVW_DEFKB               ; and MORE for a program that needs it -
    jbe .outc                       ; which is CF = 1, the row's `+`. Not when
    mov ax, DVW_DEFKB               ; the machine is under the default: then
    stc                             ; the machine is the figure, and all of it
    jmp short .out
.full:
%endif
%ifdef DOS_WTAIL
    cmp byte [dos_mhead + OS88UI_CK_ON], 0  ; **AND HEADLESS IS THE BRACKET
    je .nohead                      ; WITHOUT THE WINDOW** (SPEC.md 96.44.20):
    add ax, DOS_WTAILKB             ; that region never holds part 1, so the
.nohead:                            ; program gets its KB as well (floor)
%endif
                                    ; what-if cannot see: it was asked at a
                                    ; floor that counts the window as KEPT, and
                                    ; the dial is about to narrow it. Auto
                                    ; answers 0 here, which is right - it
                                    ; commands nothing
    ; ...AND AN UNTICKED BOX IS A DRIVER THAT WILL NOT BE THERE (96.36.7) -
    ; **ASKED OF THE ROUTINE THE LAUNCH ITSELF ASKS** (SPEC.md 96.36.7.3).
    ; This used to read the two check boxes directly, which is the same
    ; question one step short of the answer: `dos_drv_take` passed a mask of
    ; ZERO for a cycle, so every term added here was a promise nothing kept.
    ; One reader means the figure and the sweep cannot disagree again, and it
    ; carries the guard with it - a class the launch will keep adds nothing,
    ; whatever its box says. A class nothing has mounted reports 0 KB either
    ; way (96.36.7.1).
    call SPH(dos_spmask)                 ; BL = what the launch will really let go
    test bl, 1 << DRVC_DISK
    jz .nohdd
    add ax, [dos_mhkb]
.nohdd:
    test bl, 1 << DRVC_NET
    jz .snd
    add ax, [dos_mnkb]
.snd:
    add ax, [dos_msnk]              ; ...AND THE SOUND DRIVER, UNCONDITIONALLY
                                    ; (SPEC.md 96.36.7.2). It has no box
                                    ; because there is nothing to ask: a DOS
                                    ; program cannot reach it either way, so
                                    ; dos_drv_take unmounts it on every arm and
                                    ; every path. The what-if above is asked
                                    ; while it is still MOUNTED, so without
                                    ; this line the page under-reported by the
                                    ; whole of it on any machine with a card,
                                    ; silently.
                                    ;
                                    ; IT IS A FLOOR. Measured card against
                                    ; bare, the launch delivers the SAME 443KB
                                    ; either way - so what comes back is 48KB
                                    ; where drv_memk says 34, the other 14
                                    ; being the MERGE the unmount unlocks
                                    ; (docs/plans/HEAP-UNPIN-PLAN.md 2.0). No
                                    ; constant can carry that, so the residual
                                    ; stays under-reported - which is the safe
                                    ; direction, and 48 under became 14
    ; ...AND THE PACKET DRIVER'S BUFFERS COME OUT OF IT (SPEC.md 96.23.7.1).
    ; dos_pkt_bufs claims them BEFORE the what-if above was ever asked, out of
    ; the same heap, so on a machine with a wire the row promised three
    ; kilobytes the program could never be handed. It is the same reader the
    ; claim uses, which is what stops the two disagreeing again - and it
    ; answers 0 on a machine with no wire and on one whose card the Network
    ; box has just told the launch to let go.
    push ax
    call SPH(dos_pkt_want)               ; CX = the KB, or CF = 1 for none
    pop ax
    jc .cap
    cmp ax, cx                      ; ...and never BELOW zero: `.cap` clamps
    jbe .none                       ; against the user's ceiling and has no
    sub ax, cx                      ; floor, so an underflow here would print a
    jmp short .cap                  ; 65,000K row on a machine too tight to run
.none:                              ; DOS at all
    xor ax, ax
    jmp short .cap
                                    ; ...and the LIMIT is arm 0's too, which
                                    ; is why the other arm jumps past it:
                                    ; `dos_lbfill` fills KDL_CAP from the BDA
                                    ; and never from [dos_memkb], so a cap
                                    ; shown on arm 1 would be a promise the
                                    ; launch does not keep (SPEC.md 96.36.7)

    ; --- ARM 1: conventional memory, less what kern_dos is known to keep ------
    ; Every term is a constant this build knows, which is the only instrument
    ; there is: the machine being described has no kernel in it, so nothing
    ; can be asked. SK_KERN + SK_HEAP is everything from KERNEL_SEG up, and
    ; DOS_LOWKB is what sits below it - the IVT, the BDA and the boot area.
.whole:
    push ds
    pop es
    mov di, dos_skbuf
    call OSAPI_SYS_KB
    mov ax, [dos_skbuf + SK_KERN]
    add ax, [dos_skbuf + SK_HEAP]
    add ax, DOS_LOWKB
%ifndef DOS_MEGA
    sub ax, DOS_KDKB                ; ...less kern_dos's own resident span
%else
    sub ax, DOS_KDKB_HMA            ; ...less kern_dos's own resident span,
    cmp byte [dos_mhma + OS88UI_CK_ON], 0   ; which on kern_mega is the HMA
    jne .kdhma                      ; arm's or the LOW one's (96.40.9.2)
    sub ax, DOS_KDKB_LOW - DOS_KDKB_HMA
.kdhma:
%endif
    sub ax, DSH_CPKB                ; ...and the buffer COPY and TYPE share
    call dos_cache_kb               ; ...and the rung the dial is asking for
    sub ax, cx
    jmp short .outc
.cap:
    mov cx, [dos_memkb]             ; ...and the user's own ceiling, which is
    jcxz .outc                      ; the one term that is neither estimate nor
.capcx:
    cmp ax, cx                      ; measurement: it is a decision
    jbe .outc
    mov ax, cx
.outc:
    clc                             ; CF = 0: the figure is all there is
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_line - one opaque run at BX, DX
; in:  SI = NUL string, BX = x, DX = y
; out: nothing; preserves all registers
;
; font_run and not a fill-then-letter pair: one pass draws the ground and the
; glyphs, so the line is never momentarily blank (SPEC.md 6.1).
; -----------------------------------------------------------------------------
dos_line:
    push ax
    push cx
    push dx
    mov cx, bx
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_cache_give - the KB the dial is going to hand back out of the cache
; out: CX = the KB, 0 = none; every other register preserved
;
; The one term arm 0's estimate cannot get from the what-if. `OSAPI_MEM_COMPACT`
; is asked at `DOS_PG_FLOOR`, which counts the read-ahead window as KEPT - and
; that is right, because the dial usually keeps some of it. What the dial gives
; up is the difference between the two widths, and it is computed as two KB
; figures rather than one width: `ceil(a * 4.5) - ceil(b * 4.5)` is not
; `ceil((a - b) * 4.5)` when one of them is odd (7 and 2 agree at 23, 2 and 1
; disagree at 4 against 5), and a figure that is 1 KB over is a promise the
; claim does not keep.
; -----------------------------------------------------------------------------
dos_cache_give:
    push ax
    push bx
    push dx
    xor cx, cx
    cmp byte [dos_cache], DOS_CA_AUTO
    je .out                         ; Auto commands nothing, so nothing moves
    call dos_cache_ask              ; AX = what is held now...
    call dos_rah_kb
    mov dx, cx                      ; DX = what it costs
    mov bl, [dos_cache]
    xor bh, bh
    mov al, [bx + dos_ca_runs]      ; ...and what the dial leaves standing
    xor ah, ah
    call dos_rah_kb
    sub dx, cx
    mov cx, dx
    jnc .out
    xor cx, cx                      ; a dial ASKING FOR MORE than is held frees
.out:                               ; nothing - the kernel's own two bounds are
    pop dx                          ; what held it down and this cannot lift
    pop bx                          ; them (SPEC.md 18.95.8)
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_cache_kb - what the dial is asking the cache to cost, in KB
; out: CX = the KB; AX and every other register preserved
;
; `(runs * 9 + 1) >> 1` is the claim `dsk_rah_want` and `kd_rahpara` make
; (`runs * 288` paragraphs) in whole KB, an odd width's half KB rounded up:
; a figure on the glass that disagrees with the claim is worse than no figure.
; -----------------------------------------------------------------------------
dos_cache_kb:
    push ax
    push bx
    mov bl, [dos_cache]
    xor bh, bh
    mov al, [bx + dos_ca_runs]      ; the ladder, one byte a row
    xor ah, ah
    call dos_rah_kb
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_rah_kb - ...and the same sum for a width that is not the dial's
; in:  AX = chunks
; out: CX = the KB they cost; every other register preserved
; -----------------------------------------------------------------------------
dos_rah_kb:
    push bx
    mov bx, ax
    mov cx, ax
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = runs * 8 (cpu 8086: no shl by an
    add cx, bx                      ; immediate other than 1)...
    inc cx
    shr cx, 1                       ; ...and KB = ceil(runs * 4.5)
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_cache_ask - what the KERNEL is holding, in chunks (SPEC.md 18.95.8)
; out: CF = 0 with AX = the width held now, 0 = none; CF = 1 = no such slot
;      (a kernel older than 18.95.8), AX = 0. Every other register preserved
;
; `DSK_RAH_AUTO` is the sentinel that means *no ceiling of mine*, which is the
; truth outside a launch - DOS has commanded nothing - so ASKING IS NOT A
; SIDE EFFECT and the slot needs no read-only spelling. Where a previous launch
; left the cache shed it takes it BACK, which is the resting state every figure
; on this page is supposed to describe.
; -----------------------------------------------------------------------------
dos_cache_ask:
    mov al, DSK_RAH_AUTO
    call OSAPI_DSK_CACHE
    jnc .out
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; dos_mem_row - redraw the arena line alone, after a control has moved it
; in:  BX = the window; the gfx lock is held. Every register preserved
;
; ONE ROW AND NOT THE BLOCK (PERFORMANCE.md's first rule): a check box redraws
; its own mark and a drop-down its own caption, so the only other pixels that
; changed are these eleven cells - and the line is an opaque font_run, so it
; needs no ground laid under it (SPEC.md 13.14.6).
; -----------------------------------------------------------------------------
dos_mem_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dos_mem_arena              ; CF = 1: an AUTOMATIC window's figure,
    sbb dl, dl                      ; which grows to its program - `128KB+`
    and dl, '+' - ' '               ; (SPEC.md 96.54.2.1) - and a BLANK
    add dl, ' '                     ; otherwise, not a NUL: the run is opaque
    mov [dos_marn + 7], dl          ; and one cell shorter would leave the
                                    ; last `+` on the glass
    mov di, dos_marn
    call SPH(dos_mem_num)
    mov bx, [dos_mx]
    mov dx, [dos_my]
    add dx, DOS_MARNY
    mov si, dos_l_marn
    call dos_line
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; -----------------------------------------------------------------------------
; dos_paint_mem - the memory page (SPEC.md 96.25)
; in:  BX = the window; the gfx lock is held, as every W_PAINT's is
;
; NO GROUND FILL (SPEC.md 13.14.6), dos_paint_env's reason exactly: every line
; is an opaque font_run, an os88line or a control that draws its own ground.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_mem_org - the memory block's top-left, into [dos_mx]/[dos_my]
; in:  BX = the window; every register preserved
;
; **IT IS THE SETUP PAGE'S RIGHT HALF NOW** (SPEC.md 96.32.2), and this is the
; whole of what moving it cost: the block already painted from a banked origin
; because the heading, the figures, the field and the check box all had to
; agree on one, so giving that origin a different answer moves the lot. The
; two placers ask for it rather than computing from the content edge, which is
; what they did when the block had a page to itself.
; -----------------------------------------------------------------------------
dos_mem_org:
    push ax
    push cx
    push dx
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    shr cx, 1                       ; ...and the block lives in the right half
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add ax, cx
    add ax, DOS_SETPAD
    mov [dos_mx], ax
    add dx, DOS_BODYY
    mov [dos_my], dx
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
dos_paint_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dos_mem_org                ; [dos_mx]/[dos_my] = the block's top-left

    ; **THE FIGURES BEFORE THE FIGURE THEY ARE TERMS OF** (SPEC.md 96.36.3.1).
    ; dos_mck_place reads the three class words the arena is a sum of, and it
    ; DRAWS NOTHING - it is rects and literals - so it sat two blocks below,
    ; where the boxes it places are. That made the FIRST paint of a fresh
    ; window compute the arena from the bss zeros the loader left, so the
    ; sound term was missing from it and from nothing else; the next recompute
    ; - a click, the dial, a keystroke - had them, and the figure moved with
    ; the user having changed nothing. Reported from the field as `433K, then
    ; 467K after going back in`.
    push bx
    call dos_mck_place              ; all three boxes, and their figures
    pop bx

    call dos_mem_row                ; **THE ARENA, AND IT IS ONE LINE NOW**
                                    ; (SPEC.md 96.36.3): the estimate this
                                    ; page's own controls add up to - the same
                                    ; routine every control that moves it calls

    push bx                         ; ...and the choice itself, FIRST of the
%ifdef DOS_MEGA
    mov bx, [dos_mx]                ; **ON kern_mega THERE IS NO CHOICE**
    mov dx, [dos_my]                ; (KERN-MEGA-DOS-PLAN 12.1 item 1): one
    add dx, DOS_MRADY               ; line saying what will happen, where the
    mov si, dos_l_mpark             ; radio stood, and no control to click
    call dos_line
%else
    call dos_mrad_place             ; controls because its subsections are the
    mov bx, dos_mrad                ; ground the rest of them stand on
    xor di, di
    call SPH(os88ui_rad)
%endif
    pop bx

    ; --- ARM 0's subsection: the two drivers, and the limit ------------------
    push bx                         ; ...placed at the top, with the figures
    mov bx, dos_mhdd
    call dos_mck_di                 ; DI = OS88UI_DIS when this one is dead
    call SPH(os88ui_chk)
    mov bx, dos_mnet
    call dos_mck_di
    call SPH(os88ui_chk)
    pop bx

    ; --- the limit: its box, its label and its unit, all three together -----
    ; **AND ALL THREE GREY WITH THE ARM** (SPEC.md 96.36.10.2). It is arm 0's
    ; control exactly as the two driver boxes above it are, and the click path
    ; has always known that - `.notck` refuses the press on any other arm and
    ; lets it fall through to the radio - but the DRAWING did not, so the field
    ; sat there black and typeable on an arm that ignores it. 47 rule 2 is why
    ; the three are one bracket: half a control greyed is worse than none.
    push bx
    mov byte [dos_mln+LN_DIS], 0
    cmp byte [dos_keepc], DOS_MEM_IN
    je .lim
    mov byte [dos_mln+LN_DIS], 1    ; the frame takes the pen below instead of
    stc                             ; forcing black, and no caret is drawn
    call OSAPI_GFX_PEN
.lim:
    call dos_mfld_place             ; THE BOX FIRST, because it is the only one
    mov si, dos_mln                 ; of the three that reads [gfx_color] - the
    call SPH(os88line_draw)              ; other two are opaque font_run pairs with
    mov bx, [dos_mx]                ; their colours in AL/AH, and take their
    add bx, DOS_MSUBX               ; grey from [gfx_dis] alone (font_ink)
    mov dx, [dos_my]
    add dx, DOS_MFLDY + 3
    mov si, dos_l_meml
    call dos_line
    mov bx, [dos_mx]                ; ...and the UNIT, hard against the box
    add bx, DOS_MSUBX + DOS_MFLDX + DOS_MFLDW + DOS_MFLDKX
    mov dx, [dos_my]
    add dx, DOS_MFLDY + 3
    mov si, dos_l_memk
    call dos_line
    clc                             ; ...and the pen back at once, whichever
    call OSAPI_GFX_PEN              ; arm it was: the lock is held for the
    pop bx                          ; whole page and whatever paints next
                                    ; would inherit it (the `.why` caption
                                    ; below states the same rule)

    ; --- ARM 1's: the mouse box, OR why the arm cannot be picked -------------
    ; ONE ROW, TWO THINGS, and they never want it at once (SPEC.md 96.36.8):
    ; an arm that is greyed is not offering its option either.
    push bx
    call SPH(dos_mem_whole)              ; (SPEC.md 47 rule 3): consumer two of the
    jc .why                         ; one predicate
    mov bx, dos_mmou
    call dos_mck_di                 ; greyed while arm 0 is the pick (96.36.9)
    call SPH(os88ui_chk)
%ifdef DOS_MEGA
    mov bx, dos_mhma                ; ...and kern_mega's HMA box, which has no
    xor di, di                      ; arm to belong to and nothing to grey on:
    call SPH(os88ui_chk)                 ; this DOS.O88 runs only in protected mode,
                                    ; where both kern_dos images can run
    call SPH(dos_msav_di)                ; ...and the save box, greyed where no
    mov bx, dos_msav                ; fixed disk could hold the image
    call SPH(os88ui_chk)                 ; (SPEC.md 47: a fact, hb_pick's)
%endif
    jmp short .nowhy
.why:
    mov bx, [dos_mx]                ; a caption explaining a control that is
    add bx, DOS_MSUBX               ; not disabled is noise, so this is drawn
    mov dx, [dos_my]                ; only when the arm really is
    add dx, DOS_MMOUY + 3
    stc                             ; **IN THE DISABLED PEN** (SPEC.md 47 rule
    call OSAPI_GFX_PEN              ; 2, 96.36.1): it belongs to the row above
    call dos_line                   ; it. Drawn solid it is the HEAVIEST text
    clc                             ; on the block and sits at the labels' own
    call OSAPI_GFX_PEN              ; indent, so it reads as a THIRD arm that
.nowhy:                             ; is live - which is rule 2's confusion one
    pop bx                          ; level out. And the pen goes back at once:
                                    ; gfx_unlock clears the flag but the lock
                                    ; is held for the whole page, so whatever
                                    ; paints after this would inherit it

    ; --- ...and the disk cache, which belongs to neither ---------------------
    push bx
    mov bx, [dos_mx]
    mov dx, [dos_my]
    add dx, DOS_MCACY + 3
    mov si, dos_l_memc
    call dos_line
    pop bx
    push bx
    call dos_drop_place             ; the list is the ARM's (SPEC.md 96.36.6)
    mov bx, dos_mdr
    xor di, di
    call SPH(os88ui_drop)
    pop bx

    pop di                          ; **NO PAGE BUTTON HERE.** The setup area's
                                    ; furniture - the title, the arrows, Save
                                    ; Shortcut and Return - is drawn once by
                                    ; dos_paint_furn for whichever page is up,
                                    ; so a page draws only its own body
                                    ; (SPEC.md 96.32.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret


; -----------------------------------------------------------------------------
; dos_click_mem - a press on the memory page
; in:  BX = the window, CX = x, DX = y
; -----------------------------------------------------------------------------
dos_click_mem:
    push ax
    push si
    push di
    mov [dos_mpx], cx               ; **THE POINT GOES IN THE IMAGE AND NOT IN
    mov [dos_mpy], dx               ; A REGISTER** (SPEC.md 96.36.4): five
                                    ; controls are tested with it now, and two
                                    ; of the library calls between them do not
                                    ; preserve DI

    ; --- THE DROP-DOWN FIRST, and then the boxes (SPEC.md 96.36.4) ----------
    ; A pitch here is a SUBSECTION, so the radio's own rect covers everything
    ; between the two headings - which is where the boxes are. The order is
    ; the fix and not a hack: os88ui's controls all answer "was this mine",
    ; so the INNERMOST one asks first and whatever no control claims falls
    ; through to the arm it is standing in.
    push bx
    call dos_drop_place
    mov bx, dos_mdr
    call SPH(os88ui_drpress)             ; AH = 1 SPENT here, AL = the new pick or
    pop bx                          ; 0FFh, CF = 1 the save-under was REFUSED
    jc .repaint                     ; **A PICK IS AL, AND ONLY AL** (SPEC.md
                                    ; 13.14.1). CF=1 is one case and it is not
                                    ; this one - the bank was refused, so the
                                    ; write-back never happened and the whole
                                    ; half is owed; `wd_drrep` states the same
                                    ; rule in as many words. And AH=1 means
                                    ; SPENT, which an OPENING press is too. So
                                    ; the pick is AL != 0FFh and nothing else:
    cmp al, 0FFh                    ; testing CF or AH reads a pick as an open
    je .nopick                      ; and leaves the figure above STANDING at
    call dos_mem_row                ; the last value - which is what shipped,
                                    ; and which the field reported as `changing
                                    ; the disk cache does not change the
                                    ; estimate` (96.36.6.2). The BOX repaints
                                    ; either way - os88ui_drbox runs on the
                                    ; CF=0 path - so the caption and the
                                    ; caption's consequence disagreed, which is
                                    ; the shape that reads as a dead control
.nopick:
    or ah, ah
    jz .boxes
    call SPH(dos_defocus)
    jmp .out
.boxes:
    push bx                         ; ...then the three check boxes, of which
    call dos_mck_place              ; only the arm's own are live
    mov bx, dos_mhdd
    call dos_mck_hit                ; a greyed box is not offered, which is
    jnc .ckhit                      ; SPEC.md 47 rule 6 - and here it means the
    mov bx, dos_mnet                ; press belongs to the ARM under it
    call dos_mck_hit
    jnc .ckhit
    call SPH(dos_mem_whole)              ; arm 1's box is not offered while the arm
    jc .notck                       ; itself is greyed - the row is carrying
    mov bx, dos_mmou                ; the REASON there (96.36.8)
    call dos_mck_hit
%ifdef DOS_MEGA
    jnc .ckhit
    push cx                         ; ...and the HMA box (96.40.9.2), live
    push dx                         ; whenever the page is
    mov bx, dos_mhma
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call SPH(os88ui_chkhit)
    jnc .hmahit
    push di
    call SPH(dos_msav_di)                ; ...and the save box, not offered greyed
    or di, di                       ; (47 rule 6)
    pop di
    stc
    jnz .hmahit
    mov bx, dos_msav
    call SPH(os88ui_chkhit)
.hmahit:
    pop dx
    pop cx
%endif
    jc .notck
.ckhit:
    pop bx                          ; the box redrew itself, but the ARENA row
    call dos_mem_row                ; above it has just moved (96.36.3)
    jmp short .out
.notck:
    pop bx
    cmp byte [dos_keepc], DOS_MEM_IN
    jne .notfld                     ; ...THEN THE LIMIT, which is arm 0's own
                                    ; (SPEC.md 96.36.9) and is INSIDE the
                                    ; radio's rect like the boxes above it -
                                    ; so it asks before the arm does, or a
                                    ; press on it picks the arm it is already
                                    ; in and never reaches the caret
    call dos_mfld_place
    mov si, dos_mln
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call SPH(os88line_hit)
    jc .notfld
    call SPH(dos_defocus_but)            ; ...and a field keeping the caret while
    cmp byte [si+LN_FOCUS], 0       ; another control is worked is a caret the
    jne .move                       ; user cannot account for
    mov byte [si+LN_FOCUS], 1
    call SPH(os88line_draw)
    jmp short .out
.move:
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call SPH(dos_caret_to)               ; ...and the BAR follows it (96.32.4)
    jmp short .out

.notfld:
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
%ifdef DOS_MEGA
    jmp short .away                 ; no radio on kern_mega (12.1 item 1), so
                                    ; nothing here took it: REFUSE it, or the
                                    ; arguments box and the environment rows
                                    ; beside this block never see a press
                                    ; (KERN-MEGA-PLAN 14.77)
%endif
    call dos_mrad_place             ; ...AND THE ARM LAST, which is what
    push bx                         ; anything inside a subsection that no
    mov bx, dos_mrad                ; control of its own claimed belongs to.
    call SPH(os88ui_radhit)              ; os88ui_radhit takes the point in CX/DX,
    pop bx                          ; moves the record's own SEL word and
    jc .away                        ; redraws THE TWO DOTS that changed - not
    jz .repaint                     ; the group and not the page (13.17.4).
    call SPH(dos_defocus)                ; **TEST ZF BEFORE ANYTHING ELSE IS
    jmp short .out                  ; CALLED**: dos_defocus clobbers the flags,
                                    ; and reading its answer instead of
                                    ; radhit's is a pick that moves and a block
                                    ; that does not repaint. A greyed arm is
                                    ; swallowed here (CF=0, ZF=0), which is
                                    ; SPEC.md 47 rule 6: the control already
                                    ; said why. **A REAL CHANGE REPAINTS THE
                                    ; BLOCK**, because the two subsections
                                    ; under it and the cache's own LIST are
                                    ; the arm's (96.36.6)
.repaint:
    call SPH(dos_defocus)
    call dos_paint_mem              ; the whole right half, under the lock the
%ifdef DVW                          ; handler already holds - and `Run in a
    call sp_wdraw                   ; window`, which greys with arm 1 and sits
%endif                              ; in the furniture row (96.32.2.4)
    jmp short .out
.away:
    ; The POINT is still in CX/DX for dos_fld_hit, which tests the two boxes
    ; beside this block with it - and os88ui_radhit DOES carry os88ui_bhit's
    ; "every register preserved", which the check box it replaced did not, so
    ; the point survives a miss without being banked first.
    pop di                          ; **IT REFUSES WHAT IT DID NOT USE** now
    pop si                          ; (SPEC.md 96.32.2): this block shares the
    pop ax                          ; Setup page with two more boxes, so a
    stc                             ; click outside its own two controls is
    ret                             ; theirs to test - where it used to be the
                                    ; whole page and could defocus on sight
.out:
    pop di
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_fld_place - put the arguments field where the window is now
; in:  BX = the window; out: nothing, every register preserved
;
; The rect is recomputed from the CONTENT origin on every paint rather than
; banked, because a window moves and os88line's rect is in SCREEN coordinates
; (its LN_X1 comment says so). Banking it would put the caret one drag behind.
; -----------------------------------------------------------------------------
dos_fld_place:
    mov si, dos_ln
    push dx
    mov dx, DOS_SFLD1
    call dos_setbox
    pop dx
    ret

; -----------------------------------------------------------------------------
; dos_cmd_place - the COMMAND box, on the Setup page's TITLE row (SPEC.md
;                 96.32.2.2)
; in:  BX = the window
; out: dos_pln's rect; SI is clobbered, every other register preserved
;
; **IT IS THE SAME BLOCK the main page's top bar places** (96.32.1) and not a
; second one: one `os88line` record, one buffer, so the two rects are two VIEWS
; of the command and there is nothing to mirror. `dos_senv_place` makes the
; same trade one control along, and for the same reason - whichever page is up
; placed it last, which is exactly what a hit test wants.
;
; It sits beside the page's own name rather than in either column, because the
; command is what all the setup below APPLIES to: it is the header, not a
; field. So it spans everything the name does not.
; -----------------------------------------------------------------------------
dos_cmd_place:
    push ax
    push cx
    push dx
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    mov si, dos_pln
    add cx, ax                      ; CX = the content's RIGHT edge, before AX
    add ax, DOS_CMDX                ; is spent on the left one
    mov [si+LN_X1], ax
    sub cx, DOS_SETPAD + 1
    mov [si+LN_X2], cx
    add dx, DOS_CMDY
    mov [si+LN_Y1], dx
    add dx, DOS_FLDH
    mov [si+LN_Y2], dx
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_senv_place - ...and the environment box beside it, on the SAME page
; in:  BX = the window; every register preserved
;
; IT IS Environment's FIRST ROW (SPEC.md 96.32.2) - dos_eln, not a block of
; its own - so the two rects address one buffer and there is nothing to
; mirror. dos_erow places the same block for the other page, and whichever
; page is up placed it last, which is exactly what a hit test wants.
; -----------------------------------------------------------------------------
dos_senv_place:
    push cx
    push si
    xor cx, cx
.row:
    call dos_erow
    inc cx
    cmp cx, DOS_ENVN
    jb .row
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dos_setbox - put the block at SI in the Setup page's LEFT column, DX rows
;              down from the body top
; in:  BX = the window, SI = an os88line block, DX = the row offset
; out: the block's rect; SI and every other register preserved
;
; THE COLUMN IS HALF THE CONTENT and the content is 640 or 720, so a box here
; is 304 or 344 px - 38 or 43 cells - against the 256 it had when the whole
; window was 288 wide. Computed rather than a constant for that reason: the
; number that was right for one window is wrong for all three.
; -----------------------------------------------------------------------------
dos_setbox:
    push ax
    push cx
    push dx
    push di
    mov di, dx                      ; DI = the row offset, across the calls
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    shr cx, 1                       ; ...the left half of it
    sub cx, DOS_SETPAD * 2
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    mov ax, ax
    add ax, DOS_SETPAD
    mov [si+LN_X1], ax
    add ax, cx
    mov [si+LN_X2], ax
    add dx, DOS_BODYY
    add dx, di
    mov [si+LN_Y1], dx
    add dx, DOS_FLDH
    mov [si+LN_Y2], dx
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret


; -----------------------------------------------------------------------------
; dos_fld_hit - the Setup page's two left-column boxes (SPEC.md 96.32.2)
; in:  BX = the window, CX/DX = the point, the rects already placed
; out: nothing; every register preserved
;
; The memory block's own two controls have had their turn before this is
; reached, so a click that is not in either of these is a click on the page
; background - and the caret goes out, because a caret on a page the user has
; clicked away from is one they cannot account for.
; -----------------------------------------------------------------------------
dos_fld_hit:
    push cx
    push si
    push di
    push bp
    mov si, dos_pln                 ; THE COMMAND BOX FIRST (SPEC.md 96.32.2.2):
    call SPH(os88line_hit)               ; it is on this page too now, and it is the
    jnc .take                       ; one every option below is about
    mov si, dos_ln
    call SPH(os88line_hit)               ; preserves CX and DX, so every test below
    jnc .take                       ; gets the same point
    ; **AND EVERY ENVIRONMENT ROW** (SPEC.md 96.32.2.1) - `dos_click_env`'s
    ; loop, moved here now that there is no second page for it to live on.
    ; `dos_erow` spends CX on the row index, so the point is banked across it
    ; exactly as that routine's own caller used to bank it.
    mov di, cx
    mov bp, dx
    xor cx, cx
.erow:
    push cx
    call dos_erow
    mov cx, di
    mov dx, bp
    call SPH(os88line_hit)
    pop cx
    jnc .take
    inc cx
    cmp cx, DOS_ENVN
    jb .erow
    call SPH(dos_defocus)
    jmp short .out
.take:
    cmp byte [si+LN_FOCUS], 0
    jne .move                       ; already ours: just move the caret
    call SPH(dos_defocus)
    mov byte [si+LN_FOCUS], 1
    call SPH(os88line_draw)
    jmp short .out
.move:
    call SPH(dos_caret_to)               ; ...and the BAR follows it (96.32.4)
.out:
    pop bp
    pop di
    pop si
    pop cx
    ret


; -----------------------------------------------------------------------------
; dos_erow - the Nth environment row's block and its rect
; in:  CX = the row (0..DOS_ENVN-1), BX = the window
; out: SI = its os88line block, rect filled in; CX preserved
; -----------------------------------------------------------------------------
dos_erow:
    push ax
    push cx
    push dx
    mov ax, DOS_LNSZ
    mul cx                          ; DX:AX, and DX is banked - the high half
    mov si, dos_eln                 ; a row index lands there is nobody's
    add si, ax
    mov ax, DOS_EROWH               ; **THE LEFT COLUMN, NOT THE WHOLE WIDTH**
    mul cx                          ; (SPEC.md 96.32.2.1). The rows had the
    add ax, DOS_SFLD2               ; window to themselves on a page of their
    mov dx, ax                      ; own; they sit under the first one in
    call dos_setbox                 ; Setup's left column now - which is the
    pop dx                          ; SAME width row 0 has always had there,
    pop cx                          ; `os88line` scrolling a 48-char buffer
    pop ax                          ; through a ~37-cell box either way, so
    ret                             ; nothing narrowed


; -----------------------------------------------------------------------------
; dos_lnk_build - this instance's state as a Shell Link, into dos_lbuf
; out: CX = its length; CF=1 = it does not fit LNK_MAX
;
; THE HEADER IS 76 BYTES AND ALMOST ALL OF IT IS LEGALLY ZERO - three
; FILETIMEs, the file size, the icon index, the hotkey and three reserved
; fields. What is not zero is the size dword, the fixed CLSID, LinkFlags and
; ShowCommand, so the template below IS the header and nothing is patched.
;
; Each StringData entry is a 2-byte CHARACTER COUNT then the characters, NOT
; NUL-terminated. Count is characters rather than bytes, which are the same
; thing here only because IsUnicode is clear.
; -----------------------------------------------------------------------------
dos_lnk_build:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, dos_lbuf
    mov si, dos_lnk_hdr
    mov cx, LNK_HDR
    cld
.hdr:                               ; **THE TEMPLATE IS IN CS, NOT DS**
    mov al, [cs:si]                 ; (SPEC.md 96.32.2.3): it travels with
    stosb                           ; the writer, which is the Setup part -
    inc si                          ; and inline, CS IS DS, so one spelling
    loop .hdr                       ; serves both shapes
%ifdef DOS_WTAIL
    cmp byte [dos_mhead + OS88UI_CK_ON], 0  ; **ShowCommand = 7 IS CLOSE WHEN
    je .shown                       ; DONE** (SPEC.md 96.44.20): what the
    mov byte [dos_lbuf + LNK_SHOW], LNK_SW_HEAD ; loader reads, in the
.shown:                             ; header's own field
%endif

    call dos_lnk_wdir               ; WORKING_DIR: the folder we came from
    jc .no
    call dos_lnk_rel                ; RELATIVE_PATH: `.\` and the 8.3 name
    jc .no
    mov si, dos_args                ; COMMAND_LINE_ARGUMENTS
    call dos_lnk_str
    jc .no
    call dos_lnk_env                ; ...and ours, in an ExtraData block
    jc .no
    call dos_lnk_mem                ; ...and the memory settings, in a SECOND
    jc .no                          ; one (SPEC.md 96.25.2)
    xor ax, ax                      ; the terminal block: any value below 4
    stosw
    stosw
    mov cx, di
    sub cx, dos_lbuf
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; --- dos_lnk_str - SI = a NUL string -> a counted StringData at ES:DI --------
; out: DI advanced; CF=1 = it would pass LNK_MAX and nothing was written
dos_lnk_str:
    push ax
    push cx
    push si
    mov ax, si
    xor cx, cx
.len:
    cmp byte [si], 0
    je .got
    inc si
    inc cx
    jmp short .len
.got:
    mov si, ax                      ; ...back to the start
    mov ax, di
    sub ax, dos_lbuf
    add ax, cx
    inc ax
    inc ax
    cmp ax, LNK_MAX
    ja .no
    mov ax, cx
    stosw                           ; the character count...
    jcxz .done
    rep movsb                       ; ...and the characters, no NUL
.done:
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop cx
    pop ax
    stc
    ret

; --- dos_lnk_wdir - the launch folder, as a StringData ----------------------
; **DI IS AN OUTPUT AND MUST NOT BE RESTORED.** dos_lnk_str advances it past
; the entry it wrote, and an earlier version of this banked DI across the
; whole routine - so the working directory was written and then OVERWRITTEN by
; the next string, and every field in the file came out one place early.
dos_lnk_wdir:
    push ax
    push bx
    push cx
    push dx
    push si
    ; **STAND WHERE THE PROGRAM IS, FIRST** - dos_path_make's own opening, and
    ; for the same reason: `OSAPI_FILE_PATH` answers for where the MACHINE is
    ; standing (SPEC.md 96.48), which a program that has walked away with
    ; AH=3Bh or AH=0Eh has moved. `dos_sav_go` already orders the build before
    ; the dialog, so the DIALOG's navigation cannot reach this; the program is
    ; the other mover, and until 96.21.2.1 it could only ever write a wrong
    ; FOLDER - under a drive letter it is a wrong folder stated confidently.
    mov dx, [dos_dir]
    mov bl, [dos_vol]
    call SP_BEGOTO                  ; (a CORE entry: dos_be_goto is a
                                    ;  %xdefine'd slot, doscall.inc)
    jc .bare
    push di                         ; ...only across the CALL that needs it as
    mov di, dos_pbuf + 2            ; a destination of its own - and TWO ALONG,
    mov cx, DOS_PBUF - 2            ; because the drive goes in front of it
                                    ; (SPEC.md 96.21.2.1)
    call OSAPI_FILE_PATH            ; ES is the caller's DS: an X cell sets it
    pop di
    jnc .drv
.bare:
    mov byte [dos_pbuf+2], '\'      ; a refusal is not fatal - `\` is a folder
    mov byte [dos_pbuf+3], 0        ; and the link still resolves from it
.drv:
    ; **AND THE DRIVE, WHICH OSAPI_FILE_PATH DOES NOT ANSWER** (SPEC.md
    ; 19.2.4: OSAPI_FILE_HERE answers that question, so the path slot does
    ; not). Without it a shortcut says `\PRINCE` and resolves against
    ; whichever volume it was READ from, which is right exactly as often as
    ; the link and its program are on one disk.
    mov al, [dos_vol]               ; the box's own volume, which is the one
    add al, 'A'                     ; dos_run resolves the program on
    mov [dos_pbuf+0], al
    mov byte [dos_pbuf+1], ':'
    mov si, dos_pbuf
    call dos_lnk_str                ; ...and DI comes out ADVANCED
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dos_lnk_rel - `.\NAME.EXT`, which is BOTH spellings --------------------
; Windows wants a link-relative path and so do we, and `.\` satisfies each.
dos_lnk_rel:
    push ax
    push cx
    push si
    push bx
    mov bx, dos_sbuf
    mov byte [bx], '.'
    mov byte [bx+1], '\'
    inc bx
    inc bx
    mov si, dos_name
.c:
    mov al, [si]
    mov [bx], al
    inc si
    inc bx
    or al, al
    jnz .c
    mov si, dos_sbuf
    call dos_lnk_str
    pop bx
    pop si
    pop cx
    pop ax
    ret

; --- dos_lnk_env - the four rows, in an ExtraData block ---------------------
; {size, signature, data}: size counts ITSELF and the signature, so an
; unknown-signature reader steps over the whole thing with one add - which is
; what makes a private block legal rather than a squat.
dos_lnk_env:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, di                      ; bank where the size goes
    add di, 8                       ; ...and step over it and the signature
    mov bx, dos_ebuf
    mov cx, DOS_ENVN
.row:
    push cx
    mov si, bx
    cmp byte [si], 0
    je .next
    mov ax, di
    sub ax, dos_lbuf
    add ax, DOS_ENVBUF + 8
    cmp ax, LNK_MAX
    ja .no
.cp:
    mov al, [si]
    stosb
    inc si
    or al, al
    jnz .cp                         ; ...NUL and all: the block is a SET, and
                                    ; a set's members are NUL-terminated
.next:
    pop cx
    add bx, DOS_ENVBUF
    loop .row
    xor al, al
    stosb                           ; ...and the bare NUL that ends the set
    mov ax, di                      ; now the size, over the hole banked above
    sub ax, dx
    push di
    mov di, dx
    stosw
    xor ax, ax
    stosw                           ; ...a dword, and 512 never needs the top
    mov ax, LNK_EXTSIG & 0xFFFF
    stosw
    mov ax, LNK_EXTSIG >> 16
    stosw
    pop di                          ; ...back to the END of the block
    clc
    jmp short .out
.no:
    pop cx
    stc
.out:
    pop si                          ; **DI IS NOT RESTORED**, for dos_lnk_wdir's
    pop dx                          ; reason: it is an OUTPUT. Banking it here
    pop cx                          ; left the block written and then
    pop bx                          ; OVERWRITTEN by the terminal marker, which
    pop ax                          ; presents as a link with no environment
    ret

; --- dos_lnk_mem - the memory settings, in an ExtraData block of their own --
; A SECOND BLOCK and not two more fields on the first (SPEC.md 96.25.2): that
; one ends in a bare NUL after a variable number of rows, so anything appended
; to it sits at an offset that depends on what the user typed. Here the two
; fields are at a fixed offset inside a fixed-size block, and a reader that
; does not know the signature steps over it with one add - which is what
; ExtraData is specified for.
dos_lnk_mem:
    push ax
    mov ax, di
    sub ax, dos_lbuf
    add ax, LNK_EXT2SZ
    cmp ax, LNK_MAX
    ja .no
    mov ax, LNK_EXT2SZ              ; BlockSize, counting itself
    stosw
    xor ax, ax
    stosw
    mov ax, LNK_EXTSIG2 & 0xFFFF
    stosw
    mov ax, LNK_EXTSIG2 >> 16
    stosw
    mov ax, [dos_memkb]             ; the cap, 0 = as much as the machine gives
    stosw
    mov al, [dos_keepc]             ; ...the arm...
    stosb
    mov al, [dos_cache]             ; ...and the cache dial, WHICH TOOK THE
    stosb                           ; PAD BYTE (SPEC.md 96.36.6): the signature
                                    ; did not move, and a link written before
                                    ; the dial existed reads a zero there -
                                    ; which is Auto, the default a link
                                    ; without one would have been saved with
    ; **AND THE THREE BOXES** (SPEC.md 96.25.2.1). They are requests about
    ; the program and not reports about this machine (96.36.7.1) - which is
    ; the whole reason a shortcut has to carry them - and for as long as the
    ; boxes have existed this block did not, so `Disable the mouse` came back
    ; unticked from every shortcut that was saved with it ticked.
    xor al, al
    cmp byte [dos_mhdd + OS88UI_CK_ON], 0
    jne .bnet
    or al, LNK_BX_NOHDD
.bnet:
    cmp byte [dos_mnet + OS88UI_CK_ON], 0
    jne .bmou
    or al, LNK_BX_NONET
.bmou:
    cmp byte [dos_mmou + OS88UI_CK_ON], 0
    je .bwin
    or al, LNK_BX_NOMOU
.bwin:
%ifdef DVW
    cmp byte [dos_mwin + OS88UI_CK_ON], 0   ; Run in a window (96.32.2.4)
    je .bhma
    or al, LNK_BX_WIN
%endif
.bhma:
%ifdef DOS_MEGA
    cmp byte [dos_mhma + OS88UI_CK_ON], 0
    jne .bsav
    or al, LNK_BX_NOHMA
.bsav:
    cmp byte [dos_msav + OS88UI_CK_ON], 0
    je .bput
    or al, LNK_BX_SAVE
%endif
.bput:
    xor ah, ah                      ; ...and the pad, zero
    stosw
    clc
    jmp short .out
.no:
    stc
.out:
    pop ax
    ret                             ; DI IS NOT RESTORED - it is an OUTPUT, for
                                    ; dos_lnk_env's reason


%ifdef DOS_SETPART
; --- os88ui's CHECK BOX, RADIO AND DROP-DOWN, and the glyph they draw --------
; Their only caller in DOS is this page, so where it is a part their code is
; the part's (OS88UI_CTLEXT in apps/dos/dos.asm took it out of the window's
; os88ui.inc) and every call above is near. They read their records through
; DS, which is the host's here as it is there. Their one way back into the
; host is os88ui_bhit, the button's hit test, spelled a stub for the length
; of the expansion
sp_t_bhit equ os88ui_bhit           ; (the real label, before the %define)
%define os88ui_bhit sph_ui_bhit
    OS88UI_GLYPH_CODE
    OS88UI_CHK_CODE
    OS88UI_RAD_CODE
    OS88UI_DROP_CODE
%undef os88ui_bhit

; --- THE STUBS: one per host routine the code above calls --------------------
; `mov word [dos_sp_tgt], target` (6 bytes) and a jump to the one far call
; (2): the target is a HOST offset, which is what this assembly's labels are.
%macro SPSTUB 1
sph_%1:
    mov word [dos_sp_tgt], %1
    jmp short sp_far
%endmacro
sph_begoto:
    mov word [dos_sp_tgt], dos_be_goto
    jmp short sp_far
    SPSTUB dos_caret_to
    SPSTUB dos_defocus
    SPSTUB dos_defocus_but
    SPSTUB dos_mem_num
    SPSTUB dos_mem_num2
sph_dos_mem_whole:                  ; ...and THE ONE FAR CALL in the middle of
    mov word [dos_sp_tgt], dos_mem_whole    ; them, so every stub's jump to
sp_far:                             ; it is short
    call far [cs:sp_hostfar]        ; -> dos_sp_bounce: call [dos_sp_tgt] / retf
    ret
    SPSTUB os88line_draw
    SPSTUB os88line_hit
    SPSTUB os88ui_btn
    SPSTUB dos_pkt_want             ; ...and dos_mem_arena's two, which is
    SPSTUB dos_spmask               ; the page's own figure and so the part's
sph_ui_bhit:                        ; ...and os88ui's own call out of the
    mov word [dos_sp_tgt], sp_t_bhit    ; controls above
    jmp short sp_far
sp_code_end:                        ; ...and nothing past here is code
%endif

; --- the Shell Link header, 76 bytes, fixed (SPEC.md 96.21) ------------------
dos_lnk_hdr:
    dd 0x0000004C                   ; HeaderSize, and the format's own magic
    db 0x01,0x14,0x02,0x00          ; LinkCLSID {00021401-0000-0000-
    db 0x00,0x00, 0x00,0x00         ;            C000-000000000046}, in the
    db 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46   ; mixed-endian GUID order
    dd LNK_F_WDIR | LNK_F_RELP | LNK_F_ARGS      ; LinkFlags
    dd 0                            ; FileAttributes
    dd 0, 0                         ; CreationTime  - legally zero...
    dd 0, 0                         ; AccessTime
    dd 0, 0                         ; WriteTime
    dd 0                            ; FileSize      - ...and so is this
    dd 0                            ; IconIndex
    dd 1                            ; ShowCommand = SW_SHOWNORMAL
    dw 0                            ; HotKey
    dw 0                            ; Reserved1
    dd 0                            ; Reserved2
    dd 0                            ; Reserved3
DOS_LNK_HDRLEN equ $ - dos_lnk_hdr
%if DOS_LNK_HDRLEN != LNK_HDR
 %error "the Shell Link header is 76 bytes and this template is not"
%endif
