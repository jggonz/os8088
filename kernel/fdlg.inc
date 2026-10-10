; =============================================================================
; os8088 - fdlg.inc
;
; The Standard File dialog (SPEC.md 38): a DISK WINDOW (SPEC.md 22) put into a
; chooser role for one application, in an Open form and a Save form, that
; hands a chosen 8.3 name back to whoever asked. Label prefix fdlg_.
;
; It is the missing half of SPEC.md 18.4. The file slots gave packages five
; whole-file operations and no way to NAME a file, which is why Note Pad
; wrote a hard-coded NOTES.TXT (SPEC.md 27.1) - not because one note is
; enough, but because there was nothing to ask a filename with.
;
; **This file is GLUE, not a browser (SPEC.md 38.1).** It used to be one: its
; own window, listing, row painter, selection bar, scroll bar wiring, name
; editor, New Folder button, drive cycling and path stack - 3,622 resident
; bytes on kern_big, every one of them a second copy of something files.inc
; already had resident. The chooser is now a KIND_FILES instance launched
; through app_launch, and what lives here is only what is genuinely the
; dialog's:
;
;   - WHO ASKED: the requester's record, window, I_SPTR and completion proc,
;     and the staleness triple that guards the callback (SPEC.md 38.6).
;   - MODALITY: fdlg_grab / fdlg_top / fdlg_gate, unchanged (SPEC.md 38.2).
;   - THE TWO FORMS' ANSWERS: files.inc raises an FDH_* event at its sites
;     through `fdlg_hook`, which is a resident compare of the acting block
;     against [fdlg_blk] - so a Disk window that is not the chooser never
;     crosses into this file (SPEC.md 38.9).
;   - THE BUTTON COLUMN: Open|Save, Cancel, Drive (SPEC.md 38.3).
;   - WHERE TO OPEN and what to remember (SPEC.md 38.10).
;   - THE ANSWER IS POSTED: every hook that ends the dialog runs inside the
;     chooser's OWN callback, whose caller goes on drawing that window, so the
;     hook only records [fdlg_act] and fdlg_reap does the close and the
;     callback on the next UI pass (SPEC.md 38.6).
;
; **This module is `.cold` (SPEC.md 2.6)** on kern_big and FDLG.DRV on
; kern_small (SPEC.md 38.0). Its code has a CS of its own; calls that leave
; the cold set go out through `KERNEL_SEG:cw_*`. All of its data is above the
; single section toggle, and a `db`/`dw` added below it is read at the wrong
; segment with no diagnostic.
;
; **MEASURING THIS FILE: `kernsize --modules` OVERSTATES IT BY ~1,600 BYTES.**
; That tool brackets the `%include` in kernel.asm, and `%include "os88ui.inc"`
; is NESTED INSIDE this one, at the foot below the section toggle - so
; os88ui.inc's kernel copy is billed here. NONE of it is this module's: it is
; the shared control, resident on BOTH kernels, and files.inc, ctrl.inc and
; apps.inc reach it as well. Quote the dialog's own symbols, and say which
; figure you mean.
; =============================================================================

FD_NAMEMAX  equ 12              ; an 8.3 name, dot included
%if FD_NAMEMAX + 1 > INST_FNSZ
  %error "INST_FNSZ too small for FD_NAMEMAX + NUL"
%endif

; --- the events files.inc raises (SPEC.md 38.9) -------------------------------
; `fdlg_hook` takes one of these in AL and answers CF=1 when the acting Disk
; window IS the chooser and the event was taken. Each is its OWN TABLE OFFSET -
; twice the row - so the dispatcher indexes without a shift; the order is the
; table's below. The tail differs per build: kern_big has no FDH_PAINT (its
; column is drawn by fm_draw_core's own button loop through fm_btn1) and no
; FDH_LABEL (FDH_RECT answers the label with the rect), and kern_small's
; press and release are its alone.
FDH_ARM     equ 0               ; fm_layout: re-arm the Save form's name box
FDH_BANK    equ 2               ; fm_edit_end: bank a mode-8 buffer
FDH_PICK    equ 4               ; fm_open_sel, a FILE: FS_SEL
FDH_SEL     equ 6               ; fm_onclick: FS_SEL changed
FDH_COMMIT  equ 8               ; fm_edit_commit, mode 8 (Enter)
FDH_KEY     equ 10              ; fm_onkey_x: any key, in DX (AL/AH's shape)
FDH_BTN     equ 12              ; a column button fired; AH = its FM_B* id
%ifdef KERN_BIG
FDH_RECT    equ 14              ; fm_brect: AH = a column id; rect AND label
%else
FDH_PAINT   equ 14              ; fm_draw_core's end: draw the column
FDH_CLICK   equ 16              ; a press, [fm_pressx/y]
FDH_UP      equ 18              ; the release (fdlg_onup)
%endif

FDA_COMMIT  equ 1               ; [fdlg_act]: the posted answer (SPEC.md 38.6)
FDA_CANCEL  equ 2

fdlg_s_topen:  db 'Open', 0     ; files.inc's fm_s_open is this string
fdlg_s_tsave:  db 'Save As', 0  ; ...and the Save form's caption
fdlg_s_save:   db 'Save', 0
fdlg_s_cancel: db 'Cancel', 0
%ifndef NOFDMEDIA
fdlg_s_media:  db 'MEDIA', 0    ; SPEC.md 38.10's default folder
%endif

; -----------------------------------------------------------------------------
; fdlg_win - the chooser's window, 0 = none, 0FFFFh = being launched, AND the
; modal gate (SPEC.md 38.2). fdlg_blk - its fm_pool block, 0 = none.
;
; Both live HERE, in .text with an initialiser, and not in .bss: `-f bin`
; gives .bss no image bytes and nothing zeroes it at boot, and fdlg_grab
; reads the first on the very first mouse press the machine ever sees, while
; fm_layout reads the second (through fdlg_hook) on the very first Disk
; window paint. As garbage either one is a chooser that does not exist.
; -----------------------------------------------------------------------------
fdlg_win:   dw 0
fdlg_blk:   dw 0

%ifdef FDLG_MOD
FD_NENT     equ 5               ; entries FDLG.DRV publishes (SPEC.md 2.8)
FDE_OPEN    equ 0
FDE_HOOK    equ 1
FDE_REAP    equ 2
FDE_GRAB    equ 3
FDE_TOP     equ 4
%endif

; =============================================================================
; TWO SHAPES, ONE SOURCE - SPEC.md 22.3.0.1's three rules
; =============================================================================
; kern_big keeps every body below resident in `.cold`. kern_small emits them
; into `.modd`, which tools/os88mod.py cuts out as FDLG.DRV and mod_need reads
; back when an application asks for a dialog.
;
;   FDBODY  the label a body carries: `fdlg_open_x` resident, `fdlg_open_x_m`
;           in the image with a resident stub of the public name in front.
;   FDX     a call OUT of the image, near on big and `call fdx_go` with the
;           target in the word after it on small - filecp.inc's fcpx_go and
;           extmod.inc's exk_go, once more: fdx_go builds the far frame into
;           COLD_SEG:<name> (an assembly-time constant) with the body's near
;           `ret` landing on cold_kretf. Five bytes a site, the same as the
;           `call COLD_SEG:xd_<name>` it replaced, and no resident thunk.
;   FDK     a call INTO `.text` through one of the `.cold` trampolines of
;           SPEC.md 2.6.2: `call ct_<target>` on big, and the plain
;           `call KERNEL_SEG:<target>` on small.
;   FDENT   bank AX..DI, restored by `jmp fdk_di` (fdlg_open, which kern_big
;           also near-calls) or `jmp fdk_dif` (fdlg_reap, far on both).
;           kern_big's are kentc_di, kretc_di and kretfc_di, all `.cold`
;           beside this file; the image may not near-call any of them (a
;           `.cold` routine is another segment to it), so it pushes its own
;           and carries its own epilogue - ONE, ending in `retf`, because
;           both of its users are far entries there.
;   FDRET   the hook's return: `ret` on kern_big, where the resident
;           fdlg_hook near-calls it, and `retf` in the image, where it is
;           its own far entry.
; =============================================================================
%ifdef FDLG_MOD
  %macro FDBODY 1
%1 %+ _m:
  %endmacro
  %macro FDX 1
    call fdx_go
    dw %1
  %endmacro
  %macro FDK 1
    call KERNEL_SEG:%1
  %endmacro
  %macro FDRET 0
    retf
  %endmacro
%else
  %macro FDBODY 1
%1:
  %endmacro
  %macro FDX 1
    call %1
  %endmacro
  %macro FDK 1
    call ct_ %+ %1
  %endmacro
  %macro FDRET 0
    ret
  %endmacro
%endif
; ...and FDENT, always written `FDENT kentc_di`, whose epilogue is
; `jmp fdk_di`. os88ovlchk.py reads source and cannot evaluate the %ifdef, so
; it files every body here as `.modd` on both builds and would read a literal
; `call kentc_di` as the image near-calling `.cold`; through `%1`, as FDX is,
; it is not a word that scan can see, and the image's arm, which it can, is
; true of `.modd`. tools/stkbalance.py reads the same argument the other way:
; `call %1` makes FDENT a branching macro, and kentc_di's declared +6 is what
; it banks - against fdk_di's six pops.
%ifndef FDLG_MOD
  %macro FDENT 1
    call %1
  %endmacro
%define fdk_di kretc_di        ; kern_big shares the kernel's ladder
%define fdk_dif kretfc_di
%else
%define fdk_dif fdk_di         ; ...and the image's one epilogue is far
  %macro FDENT 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
  %endmacro
%endif

%ifndef FDLG_MOD
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged
%else
section .modd                   ; THE ON-DEMAND IMAGE (SPEC.md 2.8). The header
modd_hdr:                       ; is at offset 0 because vstart is 0 and
    dw 0x384F                   ; mod_check reads it through ES
    db MOD_VER
    db MOD_FDLG
    dw BUILD_NUM                ; the commit...
    dw MOD_STAMP                ; ...and this build's LAYOUT
    dw modd_end                 ; MOD_H_IMG - vstart 0, so the end label IS it
    dw FD_NENT
    dw fdlg_open_x_m            ; EVERY ENTRY IS ITS OWN BODY: each ends in a
    dw fdlg_hook_x_m            ; `retf` here (fdk_di, FDRET), so the table
    dw fdlg_reap_x_m            ; needs no `call body / retf` wrapper between
    dw fdlg_grab_x_m            ; it and the code
    dw fdlg_top_x_m

; --- the image's epilogue, because kretfc_di is not in reach ---------------
; fdlg_ref is fdlg_open's refusal: CF=1 and out. It sits HERE, in front of
; the body, so that the three refusals at its head are short jumps.
fdlg_ref:   stc
fdk_di:     pop di
            pop si
            pop dx
            pop cx
            pop bx
            pop ax
            retf

; --- fdx_go - the far call FDX names, built by hand (filecp.inc's fcpx_go,
; extmod.inc's exk_go): the call left the address of `dw target` on the
; stack, and this turns that word into a far return to just past it with
; cold_kretf's near return on top, then RETFs into COLD_SEG:target - whose
; near `ret` lands on cold_kretf and its `retf` back here. Every register and
; the flags are preserved both ways (no ADD and no INC: LEA). It replaced nine
; resident `xd_` thunks and FILECP.DRV's xf_fm_stage_name, 40 bytes of
; kern_small's `.cold`, at ~300 cycles a hop on a dialog's open, a click or a
; commit; the frame at the body's entry is the six bytes the thunk's was, and
; the peak inside this routine is 14 above the call.
fdx_go:                         ; ovlchk: builds its own far frame
    push ax                     ; STKBALANCE-OK: a far frame built by hand, its
    push ax                     ; four slots spent by the RETF below and by the
    push ax                     ; target's own return through cold_kretf
    push ax
    push bp
    mov bp, sp                  ; [bp+2] offset, [bp+4] segment, [bp+6] the
    push bx                     ; near return, [bp+8] the far return's offset
    mov bx, [bp+10]             ; and [bp+10] the call's own return, which
    mov [bp+10], cs             ; becomes the far return's CS
    mov word [bp+6], cold_kretf
    mov word [bp+4], COLD_SEG
    lea bx, [bx+2]              ; past the `dw`
    mov [bp+8], bx
    mov bx, [cs:bx-2]           ; ...which names the target
    mov [bp+2], bx
    pop bx
    pop bp
    retf
%endif

%ifndef FDLG_MOD
fdlg_ref:   stc                 ; kern_big's: the same refusal, in front of
            jmp fdk_di          ; the body for the same short jumps
%endif

; -----------------------------------------------------------------------------
; fdlg_open - slot 0x0124's body (SPEC.md 38.6, 38.7)
; in:  AL = 0 Open / 1 Save, BX = requester window, DI = completion proc,
;      SI = a default name (NUL, <= 12) or 0; gfx lock HELD by the caller
; out: CF=0 the chooser is ON SCREEN; CF=1 refused. All registers preserved.
;
; It launches a Disk window through app_launch, which takes the gfx lock
; itself - so [inst_lkhd] says the caller already holds it and app_launch
; shows the window under that hold instead (SPEC.md 38.6). [fdlg_win] =
; 0FFFFh is the mark fm_kinit adopts the window by; a launch that never
; reached fm_kinit leaves the mark, which is the refusal.
; -----------------------------------------------------------------------------
FDBODY fdlg_open_x
    FDENT kentc_di

    cmp word [fdlg_win], 0
    jne fdlg_ref                ; no dialog ever opens on top of another
    mov [fdlg_rqcb], di
    mov [fdlg_rqwin], bx
    mov [fdlg_mode], al
    mov cx, fdlg_s_topen        ; the caption fm_kinit puts on it - written
    or al, al                   ; before the fences below, like the three
    jz .ttl                     ; above: nothing reads any of them until
    mov cx, fdlg_s_tsave        ; [fdlg_win] says a chooser is being built
.ttl:
    mov [fdlg_ttl], cx

    FDK cw_inst_win_owner       ; DI = the owning record, or 0; BX preserved
    or di, di
    jz fdlg_ref
    cmp byte [di+I_STATE], 1
    jne fdlg_ref
    mov [fdlg_rqrec], di        ; ...so from here on it is a LIVE record in
    mov ax, [di+I_SPTR]         ; inst_tab, and fdlg_hidx can never miss
    mov [fdlg_rqsp], ax

    xor ax, ax
    mov [fdlg_act], al          ; no answer yet
%ifdef KERN_SMALL
    mov [fdlg_down], al         ; a press the last chooser never saw released
%endif                          ; (an Escape mid-press) is not this one's
    or si, si                   ; the caller's default wins...
    jnz .setname
    call fdlg_hidx              ; ...else the name this app last committed -
.setname:                       ; which may be empty, and copying an empty
    mov di, fdlg_name           ; name IS emptying the box: fdlg_cpy always
    call fdlg_cpy               ; stores the NUL
    mov bx, fm_pool             ; A NEW FOLDER OR RENAME PROMPT IN ANY DISK
    mov cx, FM_NSLOT            ; WINDOW ENDS HERE: modes 1 and 2 type into
.endp:                          ; fm_ebuf, which this chooser's box is about
    cmp byte [bx+FS_EDIT], 2    ; to own, and fm_edit_end spares every block
    ja .keepp                   ; but the chooser's while it is up (38.5) - so
    mov byte [bx+FS_EDIT], 0    ; the prompt would stay armed over the
.keepp:                         ; chooser's text and its Enter rename the file
    add bx, FS_SIZE             ; to it. Modes 3-7 keep their state elsewhere
    loop .endp                  ; (a Clone's claim, a paste's buffer) and stand.
                                ; Mode 0 is stored as the 0 it already was

    call fdlg_seed              ; AL = volume, DX = folder, SI = its name|0
    mov [fdlg_sdrv], al         ; where it OPENED, so a cancel that never
    mov [fdlg_scwd], dx         ; moved records nothing (SPEC.md 38.10)
    FDX fm_seed                 ; parked for fm_kinit, as a drive icon does

    or word [fdlg_win], -1      ; fm_kinit adopts the window being built:
                                ; 0FFFFh, out of the 0 the fence above saw
    inc byte [inst_lkhd]        ; ...and app_launch shows it under OUR hold
    mov al, KIND_FILES          ; (0 -> 1 -> 0: no other path sets it)
    call KERNEL_SEG:cw_app_launch
    dec byte [inst_lkhd]        ; (CF is the launch's: `dec` keeps it)
    jc .fail
    mov bx, [fdlg_win]          ; still the mark: launched, but not by
    inc bx                      ; fm_kinit - impossible, and refused rather
    jz .fail                    ; than trusted. `inc` keeps the launch's CF=0
%ifdef KERN_SMALL
    mov word [bx+W_ONMOUSEUP-1], fdlg_onup ; the column's release (SPEC.md
                                ; 38.3): this build's Disk window has none of
                                ; its own, so the chooser's is ours - and
                                ; wm_onmouseup IS this store, which kern_small
                                ; publishes no shim for. BX is the window + 1
%endif
    jmp fdk_di                  ; CF=0, still app_launch's
.fail:
    and word [fdlg_win], 0
    jmp fdlg_ref

; -----------------------------------------------------------------------------
; fdlg_seed - where the chooser opens (SPEC.md 38.10)
; out: AL = volume, DX = first cluster (0 = root), SI = the folder's name for
;      the title's path, or 0
; clobbers: AX, BX, CX, DX, SI (fdlg_open banked them all)
;
; The user's last folder if they ever chose one; else MEDIA at the root of the
; instance's own drive (assembled out by NOFDMEDIA=1, which then takes the
; first arm with the launch folder in it). There is no third arm: the
; requester is a live record by fdlg_open's own fence, so it always has a row.
; The MEDIA arm mounts the root to look, which moves the globals - nothing
; reads them on this path, and fm_kinit's own listing is the second mount.
; -----------------------------------------------------------------------------
fdlg_seed:
    call fdlg_hidx              ; BX = the requester's row
    mov al, [inst_fdrv+bx]
%ifndef NOFDMEDIA
    cmp byte [inst_fpick+bx], 0
    je .media
%endif
    shl bx, 1
    mov dx, [inst_fcwd+bx]
    xor si, si
    ret
%ifndef NOFDMEDIA
.media:
    push ax
    mov dl, al
    xor ax, ax
    FDX dsk_chdir_x             ; the root - and only for the volume and
    mov si, fdlg_s_media        ; folder it publishes: a mount with no
    jc .root                    ; listing store lists nothing, so MEDIA
    cmp byte [dsk_vkind], DVK_FILE ; is looked up BY NAME in the directory
    je .root                    ; (dskw_stat), not in a listing (SPEC.md
    call COLD_SEG:dwf_dskw_stat ; 38.10). A redirected volume's stat writes
    jc .root                    ; nothing, so it is the root there. SI is
    test al, 0x10               ; kept; AL = the attribute, BX = the cluster
    jnz .got                    ; a FILE called MEDIA is not the folder
.root:
    xor bx, bx                  ; the root, and no name for the title
    xor si, si
.got:
    mov dx, bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; fdlg_hidx - the requester's row in instance.inc's side table
; out: BX = the row and SI -> its inst_fname; every other register kept
;
; It cannot miss: [fdlg_rqrec] is a live record in inst_tab from the moment
; fdlg_open accepts, and inst_fhome_idx of any record in the table is a row.
; -----------------------------------------------------------------------------
fdlg_hidx:
    push di
    mov di, [fdlg_rqrec]
    call KERNEL_SEG:cw_inst_fhome_idx
    pop di
    push ax
    mov al, INST_FNSZ
    mul bl                      ; AX = row * INST_FNSZ (DX untouched)
    add ax, inst_fname
    xchg ax, si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_home_dir - record where the chooser stood as the app's folder
; in:  [fdlg_sdrv] / [fdlg_scwd], which fdlg_reap has turned from where the
;      chooser OPENED into where it STOOD; out: SI -> the row's inst_fname;
; clobbers AX and BX - fdlg_reap banks the BX and SI it still needs
;
; It is the instance's ACTUAL current directory (SPEC.md 19.2.1), and
; [inst_fpick] says the USER chose it, so the MEDIA default stops applying.
; On a COMMIT fdlg_reap copies the name in after it as well (SPEC.md 38.10):
; a Save As moves the app.
; -----------------------------------------------------------------------------
fdlg_home_dir:
    call fdlg_hidx
    mov al, [fdlg_sdrv]
    mov [inst_fdrv+bx], al
    mov byte [inst_fpick+bx], 1
    shl bx, 1
    mov ax, [fdlg_scwd]
    mov [inst_fcwd+bx], ax
    ret

; -----------------------------------------------------------------------------
; fdlg_gate - is the chooser still a USED and VISIBLE window? (SPEC.md 38.2)
; out: CF=0 with BX = the chooser; CF=1 when there is none (ZF=1), or the one
;      there was has gone (ZF=0: close box, minimize, anything). AL clobbered
;      - all three callers bank AX - and nothing else is touched.
;
; It ANSWERS and does nothing else: fdlg_reap - the same UI pass, because the
; wm_hide or wm_destroy that took the window posted [ui_post] through
; wm_paint_dmg - is what cancels a chooser that has gone, and the two filters
; hold everything still until it has (ZF is how they tell the cases apart).
; -----------------------------------------------------------------------------
%if W_FLAGS != 0
  %error "fdlg_gate reads bit 0 (used) and bit 1 (visible) at [bx]"
%endif
fdlg_gate:
    mov bx, [fdlg_win]
    or bx, bx
    stc
    jz .ret                     ; no chooser at all: CF=1 and the `or`'s ZF=1
    mov al, [bx+W_FLAGS]
    and al, 3
    cmp al, 3                   ; CF=1 exactly when either bit is clear, and
                                ; ZF=0 with it: gone
.ret:
    ret

; -----------------------------------------------------------------------------
; fdlg_reap - once per UI pass while a chooser is up (SPEC.md 38.1.1, 38.6)
; in:  nothing; the gfx lock is FREE (this is ui_task's deferred section).
;      Both callers test [fdlg_win] first (SPEC.md 38.1.1) and the body keeps
;      its own test as well, for that section's reason
; out: nothing (all registers preserved)
;
; THE ONE PLACE A CHOOSER IS ENDED: a posted answer is acted on here, and a
; chooser that has gone is cancelled here - close, then for a commit the
; callback, all under one hold. A dead chooser's posted answer is spent with
; it and never committed. The close and the commit are this routine's own
; two halves rather than procs of their own: it is their only caller, and
; it banks every register once for both.
; -----------------------------------------------------------------------------
FDBODY fdlg_reap_x
    FDENT kentc_di
    cmp word [fdlg_win], 0
    je .done
    call fdlg_gate              ; CF=1: it has gone, which is a cancel
    mov al, 0
    xchg al, [fdlg_act]         ; the posted answer, taken (CF untouched)
    jnc .alive
    mov al, FDA_CANCEL
.alive:
    or al, al
    jz .done                    ; (BX = [fdlg_win] from the gate either way)
    push ax
    FDK cw_gfx_lock

    ; --- THE CLOSE (SPEC.md 38.7 step 6) -------------------------------------
    ; Records where it stood FIRST, out of a block that is still intact
    ; whichever route got here - a close box has already torn the instance
    ; down, and the pool block is only reused by the next launch. A failed
    ; mount is not a folder to bring an app back to (FS_MOK), and arriving at
    ; the seed without moving is not a choice (SPEC.md 38.10). Where it STOOD
    ; goes over where it OPENED: nothing reads the seed after this, and the
    ; commit below and fdlg_home_dir read the same two fields.
    xor ax, ax                  ; BX is still the chooser, fdlg_gate's load
    mov [fdlg_win], ax
    mov si, ax
    xchg si, [fdlg_blk]

    cmp byte [si+FS_MOK], 0     ; a failed mount stands at the seed
    je .kept
    mov al, [si+FS_DRV]
    mov dx, [si+FS_CWD]
    cmp al, [fdlg_sdrv]
    jne .moved
    cmp dx, [fdlg_scwd]
    je .kept
.moved:
    mov [fdlg_sdrv], al
    mov [fdlg_scwd], dx
    push bx                     ; (fdlg_home_dir spends BX and SI, and both
    push si                     ; are the chooser's, wanted just below)
    call fdlg_home_dir          ; ...moving is a statement about the location
    pop si
    pop bx
.kept:
    FDK cw_inst_win_owner       ; DI = the window's record, or 0
    or di, di
    jz .gone                    ; already torn down: the close box did it
    cmp byte [di+I_STATE], 1
    jne .gone
    cmp [di+I_SPTR], si         ; ...and it is still the CHOOSER's: a window
    jne .gone                   ; slot is reused, a pool block names one role
    call KERNEL_SEG:cw_app_close_win ; frees the cache claim with the record
.gone:
    mov bx, [fdlg_rqwin]
    call KERNEL_SEG:cw_menu_activate ; the bar goes back to the requester
    call KERNEL_SEG:cw_menu_draw_bar
    pop ax
    cmp al, FDA_COMMIT
    je .commit
.unlk:
    FDK cw_gfx_unlock
.done:
    jmp fdk_dif

    ; --- THE COMMIT (SPEC.md 38.6, 38.7 step 7) ------------------------------
.commit:
    mov dl, [fdlg_sdrv]         ; the callback's contract: the file's volume
    mov ax, [fdlg_scwd]         ; is mounted and its folder is current - put
    cmp dl, [disk_drive]        ; them back if anything moved them, which
    jne .cd                     ; costs nothing when nothing did
    cmp ax, [dsk_cwd]
    je .here
.cd:
    FDX dsk_chdir_x
.here:
    mov di, [fdlg_rqrec]        ; THE STALENESS TRIPLE (SPEC.md 38.6) - of a
    cmp byte [di+I_STATE], 1    ; record fdlg_open fenced live, so never 0
    jne .unlk
    mov ax, [fdlg_rqwin]
    cmp ax, [di+I_WIN]
    jne .unlk
    mov ax, [fdlg_rqsp]
    cmp ax, [di+I_SPTR]
    jne .unlk
    push di                     ; snd_disp_set's input, below
    call fdlg_home_dir          ; SI -> the row's name: the folder and...
    mov di, si
    mov si, fdlg_name           ; ...the name (SPEC.md 38.10)
%if INST_FNSZ - 1 != FD_NAMEMAX
  %error "fdlg_cpy's bound is INST_FNSZ - 1's"
%endif
    call fdlg_cpy

    ; DX:CX = what OSAPI_FILE_READ will deliver for [fdlg_name] (SPEC.md
    ; 38.6.1): 0 when there is no such file or it is a folder, the UNPACKED
    ; size when it is compressed. dskw_stat answers the size on BOTH of its
    ; arms - a redirected volume's by asking the driver - and only the
    ; compression hint is a FAT volume's alone: its DVK_FILE arm writes no
    ; dskw_raw, and nothing on a redirected volume is compressed.
    ;
    ; **AND IT IS dskw_czknow's JUDGEMENT, SNIFF INCLUDED** (SPEC.md
    ; 20.14.6): a file whose hint a foreign tool dropped is READ as its
    ; unpacked bytes, so reporting what it occupies told the application a
    ; size the read then exceeded. One sector, once, on a commit - the one
    ; the application's read is about to want (SPEC.md 18.95) - which is
    ; 20.14.6.2.1's line for where a sniff belongs. ES is banked round it
    ; (a sniff spends it, and this routine gives every register back).
    mov si, fdlg_name
    call COLD_SEG:dwf_dskw_stat ; CF=0: AL = attribute, DX:CX = the size
    jc .zero                    ; no such name in this folder
    test al, 0x10
    jnz .zero                   ; a folder has no size to report
    cmp byte [dsk_vkind], DVK_FILE
    je .sized
    push es
%ifdef KERN_BIG
    FDX dskw_czsize             ; drvvol.inc's, shared with drv_find
    pop es
%else
    push cx                     ; (FDLG.DRV: in line, the same three steps,
    push dx                     ; so the resident kernel carries no shared
    FDX dskw_czknow             ; routine on this build - drvvol.inc says why)
    pop dx
    pop cx
    pop es
    ja .sized
    mov cx, [dskw_raw+DSK_R_CZL]
    mov dl, [dskw_raw+DSK_R_CZH]
    mov dh, 0
%endif
    jmp short .sized
.zero:
    xor cx, cx
    xor dx, dx
.sized:
    pop di                      ; the requester's record
    mov ax, [fdlg_rqcb]
    or ax, ax
    jz .unlk
    push word [snd_inst]        ; the callback is the PACKAGE's code, so its
    call KERNEL_SEG:cw_snd_disp_set ; sound stamp is the requester's (34.3)
    push es                     ; the callback owes us nothing (SPEC.md
    push bp                     ; 38.6), and ES is the one register the
    mov bp, ax                  ; kernel cannot shrug off (SPEC.md 1)
    mov ax, [fdlg_mode]         ; AL = mode and AH = [fdlg_act], which the
                                ; xchg at the top left 0 and nothing can post
                                ; to a chooser that is closed. SI = its
    mov si, [fdlg_rqwin]        ; window, DI = the name (ours - for a package
    mov di, fdlg_name           ; DS is its own segment and this buffer is in
                                ; the kernel's, 20.1), DX:CX = the size
    call KERNEL_SEG:cw_wm_pkgcall
    pop bp
    pop es
    pop word [snd_inst]
    jmp .unlk

; -----------------------------------------------------------------------------
; fdlg_grab - a press outside the chooser is swallowed (SPEC.md 38.2)
; in:  CX = x, DX = y; the gfx lock is free
; out: CF=1 swallow it (with a beep); CF=0 let it through
;
; "Outside" is wm_hit's answer: the press passes only when the window it
; would reach IS the chooser, so the menu bar, the dock, the desktop and every
; other window are inert - including one that has come up OVER the chooser.
;
; A chooser that has ANSWERED, or whose window has GONE, is not reaped until
; the tail of this pass, and the rest of the drain is still to come: every
; press in it is swallowed, silently and arming nothing, so no release fires
; either. A press let through there would change a posted answer's folder or
; name, or - a drive icon's double-click - launch a Disk window into the dead
; chooser's freed block and slot, which the reap would then adopt as the
; chooser (SPEC.md 38.2).
; -----------------------------------------------------------------------------
FDBODY fdlg_grab_x
    push ax
    push bx
    call fdlg_gate
    jc .notup
    cmp byte [fdlg_act], 0
    jne .eat                    ; answered: nothing more reaches it
    push bx
    call KERNEL_SEG:cw_wm_hit   ; BX = the window this press would reach
    pop ax
    cmp ax, bx
    je .out                     ; CF=0 from the compare
    FDK cw_snd_beep
.eat:
    stc
    jmp short .out
.notup:
    jnz .eat                    ; ZF=0: gone, and not yet reaped
    clc                         ; ZF=1: there is none
.out:
    pop bx
    pop ax
    retf

; fdlg_top - in: BX = wm_top's answer; out: BX = the chooser instead, if up,
; and 0 - the key goes nowhere - if it has gone and is not yet reaped
FDBODY fdlg_top_x
    push ax
    push bx
    call fdlg_gate
    pop ax                      ; AX = wm_top's answer
    jnc .out
    jz .stand                   ; ZF=1: there is none
    xor ax, ax                  ; ZF=0: gone (SPEC.md 38.2)
.stand:
    xchg ax, bx                 ; ...which stands
.out:
    pop ax
    retf

; =============================================================================
; THE HOOK (SPEC.md 38.9): files.inc's sites, one dispatch
; =============================================================================
; in:  AL = FDH_* (already a table offset), and AH = the button for FDH_BTN
;      and FDH_RECT; BX = the chooser's pool block, which the resident
;      fdlg_hook's compare has just loaded - so EVERY HANDLER ENTERS WITH
;      BX = [fdlg_blk] and none loads it. The one exception is FDH_UP, which
;      kern_small's fdlg_onup raises straight from W_ONMOUSEUP. An index is
;      never passed: every route into fm_open_sel opens FS_SEL, so the
;      handlers read that
; out: CF=1 the chooser took it; BP preserved (it is the dispatch register,
;      because FDH_RECT answers in SI and DI), BX restored by fdlg_hook, AX
;      and the rest per handler
; -----------------------------------------------------------------------------
FDBODY fdlg_hook_x
    push bp
    push ax
    mov ah, 0
    xchg ax, bp                 ; BP = the table offset...
    pop ax                      ; ...and AH, the handler's argument, back
    call [cs:fdlg_htab+bp]
    pop bp
    FDRET

fdlg_htab:
    dw fdlg_h_arm, fdlg_h_bank, fdlg_h_pick, fdlg_h_sel
    dw fdlg_h_commit, fdlg_h_key, fdlg_h_btn
%ifdef KERN_BIG
    dw fdlg_h_rect
%else
    dw fdlg_h_paint, fdlg_h_click, fdlg_h_up
%endif

; FDH_ARM - fm_layout, before it caches FS_EDIT: a Save chooser that is not
; in some other prompt is typing its name (SPEC.md 38.5). CF=0.
fdlg_h_arm:
    cmp byte [fdlg_mode], 0
    je fdlg_h_no
    cmp byte [bx+FS_EDIT], 0
    jne fdlg_h_no
    ; fall through

; fdlg_toebuf - fdlg_name into the line editor, as mode 8. In: BX = the
; chooser's block. AX clobbered; CF=0. CX is KEPT: the arrows' FDH_SEL
; divides by it after
fdlg_toebuf:
    push si
    push di
    push cx
    mov byte [bx+FS_EDIT], FM_ESAVE
    mov si, fdlg_name
    mov di, fm_ebuf
    call fdlg_cpy               ; CX = FD_NAMEMAX less the length...
    mov al, FD_NAMEMAX
    sub al, cl
    mov [fm_elen], al           ; ...which is the length
    pop cx
    pop di
    pop si
fdlg_h_no:
    clc
    ret

; FDH_BANK - fm_edit_end, BEFORE it clears the shared buffer: a mode-8 name
; goes back to fdlg_name so the next FDH_ARM restores it. CF=0, and AX, CX,
; SI and DI clobbered: every caller is done with them.
fdlg_h_bank:
    cmp byte [bx+FS_EDIT], FM_ESAVE
    jne fdlg_h_no
fdlg_bank:
    mov si, fm_ebuf
    mov di, fdlg_name           ; ...and into fdlg_cpy, whose CF=0 is fm_ncpy's
                                ; last `or al, al`

; fdlg_cpy - a NUL name into a 13-byte field, SI -> DI (fm_ncpy at
; FD_NAMEMAX). AX clobbered; CX = FD_NAMEMAX less what was copied
fdlg_cpy:
    mov cx, FD_NAMEMAX
    FDX fm_ncpy
    ret

; fdlg_tsel - stage the chooser's selection's name as the answer
; in:  BX = the chooser's block
; out: CF=1 nothing selected, or a folder (nothing taken); else fdlg_name.
;      AX clobbered
fdlg_tsel:
    mov ax, [bx+FS_SEL]
    cmp ax, [bx+FS_N]
    cmc
    jc fdlg_ret                 ; 0xFFFF, none, fails this too
    push si
    push di
    push cx
    FDX fm_stage_name           ; fm_name, fm_ftype; every register kept
    cmp word [fm_ftype], 2
    cmc                         ; CF=1 exactly for a folder or '..'
    jc .out
    mov si, fm_name
    mov di, fdlg_name
    call fdlg_cpy               ; CF=0 (fm_ncpy's last `or al, al`)
.out:
    pop cx
    pop di
    pop si
fdlg_ret:
    ret

; FDH_PICK - a FILE opened (double-click, Enter, File > Open, the Open
; button) - FS_SEL, which is what every route into fm_open_sel opens. Open
; form: it is the answer. Save form: it is the name in the box, redrawn here
; because fm_open_sel then reports "quiet". CF=1.
fdlg_h_pick:
    call fdlg_tsel
    cmp byte [fdlg_mode], 0
    je fdlg_post_commit
    call fdlg_toebuf
    mov si, [fdlg_win]          ; fm_open_sel's quiet exit reads no SI
    FDX fm_status_only
    stc
    ret

; FDH_SEL - fm_onclick's new FS_SEL. Save form: a file fills the box (the
; status line is the caller's to redraw - the click ended mode 8, so it is
; owed). Open form: the default button's greying is the selection, and it is
; redrawn only when that greying FLIPS (SPEC.md 38.8) - [fdlg_gdis] is the
; flags it was last DRAWN with, recorded by the draw itself (fdlg_drawbtn,
; kern_big's fm_btn1) and not by fdlg_blabel, which a hit test calls too.
; CF=0; CX, DX, SI and DI kept.
fdlg_h_sel:
    cmp byte [fdlg_mode], 0
    je .open
    call fdlg_tsel
    jc fdlg_h_no
    jmp fdlg_toebuf
.open:
    push si
    push di
    mov si, [fdlg_gdis]         ; what the glass shows...
    mov al, FM_BGO
    call fdlg_blabel            ; ...against what it should
    cmp si, di
    pop di
    pop si                      ; (pop writes no flag)
    je fdlg_h_no                ; one file to another: not a pixel changes
%ifdef KERN_BIG
    FDX fm_btn1                 ; the Disk window's own: pressed look and all
%else
    call fdlg_drawbtn
%endif
    clc
    ret

; FDH_KEY - DX = the key, DL the ASCII and DH the scan (fm_onkey_x's AL/AH).
; Escape cancels, unless some OTHER prompt is open in the chooser (a Rename, a
; New Folder), whose own Escape it is. Anything else is the Disk window's -
; the arrows are whatever SPEC.md 22.26 makes them on the build, and reach
; this file only as FDH_SEL. CF=1 taken.
%if FM_ESAVE != 8
  %error "fdlg_h_key tests FS_EDIT for 0 or FM_ESAVE with one mask"
%endif
fdlg_h_key:
    test byte [bx+FS_EDIT], ~FM_ESAVE & 0xFF ; 0 (none) and 8 (the box)
    jnz fdlg_h_no               ; are ours; modes 1-7 are another prompt's,
    cmp dl, 27                  ; which owns the keyboard
    jne fdlg_h_no               ; ...and Escape falls into the cancel below

fdlg_post_cancel:
    mov al, FDA_CANCEL
    jmp short fdlg_post
fdlg_post_commit:
    mov al, FDA_COMMIT
; fdlg_post - record the answer and wake ui_task; fdlg_reap acts on it
fdlg_post:
    mov [fdlg_act], al
    inc byte [ui_post]
    stc
    ret

; FDH_COMMIT - Enter in the Save form's box. CF=1 either way.
fdlg_h_commit:
    call fdlg_bank
fdlg_save:
    cmp byte [bx+FS_MOK], 0     ; a FAILED mount is nowhere to save: the
    je fdlg_beep                ; commit would fall back to the folder the
                                ; chooser opened on (fdlg_reap) and write the
                                ; file there
    cmp byte [fdlg_name], 0
    jne fdlg_post_commit
fdlg_beep:
    FDK cw_snd_beep             ; an empty box answers nothing
    stc
    ret

; FDH_BTN - AH = FM_BGO / FM_BCANCEL / FM_BDRIVE (kern_small's release enters
; at fdlg_btn with it in AL, and BX = the block as here). CF=1. SI is
; clobbered: kern_big's fm_onup leaves through its own epilogue and
; kern_small's FDH_UP banks it.
fdlg_h_btn:
    mov al, ah
fdlg_btn:
    cmp byte [fdlg_act], 0      ; ANSWERED already: a release whose press
    jne .done                   ; came before the answer acts on nothing
    cmp al, FM_BCANCEL
    je fdlg_post_cancel
    cmp al, FM_BDRIVE
    je .drive
    cmp byte [fdlg_mode], 0
    jne .save
    mov ax, [bx+FS_SEL]         ; Open: the selection acts - a file is the
    cmp ax, [bx+FS_N]           ; answer (back through FDH_PICK), a folder is
    jae fdlg_beep               ; entered, nothing selected refuses
    FDX fm_open_sel
    jnc .done
.paint:
    mov si, [fdlg_win]
    FDX fm_repaint
.done:
    stc
    ret
.save:
    call fdlg_h_bank            ; a box mid-edit is banked first, if it is
    jmp short fdlg_save         ; the box's edit that is up

; --- Drive: DL = the next live volume after the chooser's own, wrapping
; (SPEC.md 38.11). A machine with one live volume answers that volume. BX
; and CX are spent: every caller of the hook is done with both, and BX is
; fdlg_hook's (or fdlg_h_up's) to restore.
%if DVOL_MAX & (DVOL_MAX - 1)
  %error "the Drive button wraps with a mask: DVOL_MAX must be a power of two"
%endif
.drive:
    mov dl, [bx+FS_DRV]
    mov cx, DVOL_MAX
.next:
    inc dx
    and dl, DVOL_MAX - 1
    mov al, DV_SIZE
    mul dl
    xchg ax, bx                 ; BX = the row's offset in dsk_vtab
    cmp byte [dsk_vtab+bx+DV_KIND], DVK_FREE
    loope .next
    FDX fm_mount
    jmp short .paint

; =============================================================================
; THE BUTTON COLUMN (SPEC.md 38.3)
; =============================================================================
; fdlg_brect - os88ui_krect = button AL's rect (FM_BGO..FM_BDRIVE), off the
; layout cache. All registers preserved. kern_big's fm_brect hands its ids
; above FM_BVIEW here, so both builds place the column identically.
fdlg_brect:
    push ax
    mov ah, 20
    mul ah                      ; AX = 20 * id
    add ax, [fm_cy]
    sub ax, 20*FM_BGO - 2       ; y = content top + 2 + 20 * row
    mov [os88ui_krect+2], ax
    add ax, FM_BTN_H-1
    mov [os88ui_krect+6], ax
    mov ax, [fm_rgt]
    add ax, 5
    mov [os88ui_krect+0], ax
    add ax, FM_BTN_W-1
    mov [os88ui_krect+4], ax
    pop ax
    ret

%ifdef KERN_BIG
; FDH_RECT - kern_big's fm_brect asking about id AH: the rect, and with it
; the label and flags fm_btn1 draws it with (SI, DI). CF=1, which fm_brect
; turns into its own CF=0 - a non-chooser's CF=0 from fdlg_hook is its
; "no such button".
fdlg_h_rect:
    mov al, ah
    call fdlg_brect
    call fdlg_blabel
    stc
    ret
%endif

; fdlg_blabel - SI = button AL's label, DI = its OS88UI_* flags: the first is
; the default, and greyed in the Open form with nothing selected (SPEC.md 47).
; In: BX = the chooser's block (fdlg_hook's for FDH_RECT, drawbtn's own load)
fdlg_blabel:
    mov di, OS88UI_FILL
    mov si, fdlg_s_cancel
    cmp al, FM_BCANCEL
    je .out
    mov si, fdlg_s_drive
    ja .out
    inc di                      ; OS88UI_FILL | OS88UI_DEF
    inc di
    mov si, fdlg_s_save
    cmp byte [fdlg_mode], 0
    jne .out
    mov si, fdlg_s_topen
    cmp word [bx+FS_SEL], 0xFFFF
    jne .out
    inc di                      ; | OS88UI_DIS
.out:
    ret

%ifdef KERN_SMALL
; fdlg_drawbtn - draw button AL (all registers preserved)
fdlg_drawbtn:
    push bx
    push si
    push di
    mov bx, [fdlg_blk]          ; blabel's: two of the four callers have it
    call fdlg_brect             ; in BX already and two do not
    call fdlg_blabel
    cmp al, FM_BGO              ; the default's flags as DRAWN, for FDH_SEL
    jne .ndef                   ; (fm_btn1 records them on kern_big)
    mov [fdlg_gdis], di
.ndef:
    cmp al, [fdlg_down]         ; the one being held (SPEC.md 13.8.3)
    jne .up
    or di, OS88UI_DOWN
.up:
    mov bx, os88ui_krect
    call COLD_SEG:os88ui_btn_f  ; os88ui_kbtn's own far entry (SPEC.md 2.6)
    pop di
    pop si
    pop bx
    ret

; FDH_PAINT - fm_draw_core's end: the whole column. CF=0.
fdlg_h_paint:
    mov al, FM_BGO
.l:
    call fdlg_drawbtn
    inc ax
    cmp al, FM_BDRIVE
    jbe .l
    clc
    ret

; FDH_CLICK - kern_small's press: ARM and draw pressed, nothing more - the
; release fires (SPEC.md 13.7), exactly as kern_big's ids 3-5 do through
; fm_onup_x. CF=1 a button took it. BX is fdlg_hook's to restore.
fdlg_h_click:
    push cx
    push dx
    mov cx, [fm_pressx]
    mov dx, [fm_pressy]
    mov bx, os88ui_krect
    mov al, FM_BGO
.l:
    call fdlg_brect
    FDX os88ui_bhit
    jnc .hit
    inc ax
    cmp al, FM_BDRIVE
    jbe .l
    jmp short .none             ; CF=0, the `cmp` past FM_BDRIVE's
.hit:
    mov [fdlg_down], al         ; the ARM and the pressed look are one byte:
                                ; kern_small carries no os88ui_armw
    call fdlg_drawbtn
    stc
.none:
    pop dx
    pop cx
    ret

; FDH_UP - the release, from fdlg_onup with CX/DX = the point. What was armed
; is SPENT first (13.7's second rule: a release anywhere disarms), the button
; comes back up, and it fires only if the pointer is still on it. A
; W_ONMOUSEUP gives every register back: AX is fdlg_onup's, BP fdlg_hook_x's,
; and the rest are banked here - BX too, because this is the one handler
; that does not come through fdlg_hook.
fdlg_h_up:
    push bx
    push cx
    push dx
    push si
    push di
    xor ax, ax                  ; take it and clear it, os88ui_fire's shape
    xchg al, [fdlg_down]
    or al, al
    jz .out
    mov bx, [fdlg_win]
    FDX fm_layout               ; the layout cache, for THIS window
    call fdlg_drawbtn           ; up again - and os88ui_krect is left holding
    mov bx, os88ui_krect        ; its rect, which the hit test wants
    FDX os88ui_bhit
    jc .out                     ; slid off: thought better of
    mov bx, [fdlg_blk]          ; what fdlg_hook hands every other handler
    call fdlg_btn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
%endif

; =============================================================================
; .bss
; =============================================================================
section .bss

fdlg_mode:  resb 1              ; 0 = Open, 1 = Save
fdlg_act:   resb 1              ; the posted answer: 0, FDA_COMMIT, FDA_CANCEL;
                                ; AFTER fdlg_mode, which fdlg_reap's commit
                                ; reads as one word with this 0 for AH
fdlg_ttl:   resw 1              ; its caption, for fm_kinit to adopt
fdlg_gdis:  resw 1              ; the Open button's flags as last drawn
fdlg_rqwin: resw 1              ; the requester's window...
fdlg_rqcb:  resw 1              ; ...its near completion proc...
fdlg_rqrec: resw 1              ; ...its instance record...
fdlg_rqsp:  resw 1              ; ...and that record's I_SPTR: the staleness
                                ; triple of SPEC.md 38.6
fdlg_name:  resb FD_NAMEMAX+1   ; the answer, and the Save box between edits
fdlg_sdrv:  resb 1              ; where the chooser OPENED - and, from
fdlg_scwd:  resw 1              ; fdlg_reap's close on, where it STOOD
%ifdef KERN_SMALL
fdlg_down:  resb 1              ; the column button drawn pressed, 0 = none
%endif

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`

; --- os88ui.inc IS RESIDENT, and that is what makes the split possible ------
; Its kernel copy is SHARED: apps.inc near-calls ui_krect4, files.inc and
; ctrl.inc draw with it, and os88ui_btn / _arm / _armed / _fire / _krect are
; used by five other files between them. Moving it into FDLG.DRV would take
; the standard controls off every one of them, so it stays here, the image
; far-calls in through fdx_go (and os88ui_btn_f), and tools/os88ovlchk.py's own
; `EXTRA` table - which files this include as `.cold` - stays true.
%define OS88UI_KERNEL
%define OS88UI_SCROLL          ; SPEC.md 13.10: the Disk window's bar uses it
                               ; OS88UI_SBDRAG - SPEC.md 13.10.5's thumb drag -
                               ; is NOT defined here. It has to be resolved
                               ; before files.inc, which is included first;
                               ; kernel.asm carries it beside WM_ANIM and
                               ; OS88_THEME and says why
%include "os88ui.inc"
%if OS88UI_DEF != 2 || OS88UI_DIS != 1 || OS88UI_FILL & 3
  %error "fdlg_blabel adds OS88UI_DEF and OS88UI_DIS to OS88UI_FILL with inc"
%endif

; -----------------------------------------------------------------------------
; ui_krect4 - stage a whole inclusive rect into the kernel's ONE button rect
; in:  AX = x1, BX = y1, CX = x2, DX = y2
; out: os88ui_krect filled; ALL registers preserved
;
; os88ui_krect is declared in .bss by os88ui.inc and there is exactly one of
; it, which is legal because every writer is on the UI task under the gfx lock
; (SPEC.md 13). apps.inc's Timer button stages a rect it already holds WHOLE
; and follows it with `mov bx, os88ui_krect / call os88ui_kbtn`.
; -----------------------------------------------------------------------------
ui_krect4:
    mov [os88ui_krect+0], ax
    mov [os88ui_krect+2], bx
    mov [os88ui_krect+4], cx
    mov [os88ui_krect+6], dx
    ret

fdf_fdlg_open:          call fdlg_open_x
                    retf

; -----------------------------------------------------------------------------
; fdlg_hook - files.inc's ONE door into the chooser (SPEC.md 38.1.1, 38.9)
; in:  AL = FDH_*, the rest per event
; out: CF=1 the acting Disk window IS the chooser and it took the event;
;      CF=0 otherwise, with nothing changed
;
; RESIDENT ON BOTH BUILDS, and the compare is on this side for 38.1.1's
; reason: fm_layout raises FDH_ARM on every paint of every Disk window, and a
; window that is not the chooser must not pay a far call to be told so.
; [fdlg_blk] is 0 with no chooser up, and no pool block is at offset 0.
; -----------------------------------------------------------------------------
fdlg_hook:
    push bx
    mov bx, [fm_vp]
    cmp bx, [fdlg_blk]          ; ...and on a match BX IS the chooser's block,
    clc                         ; which every handler takes (fdlg_hook_x)
    jne .no
%ifdef FDLG_MOD
    call far [FDFP + FDE_HOOK*4]
%else
    call fdlg_hook_x
%endif
.no:
    pop bx
    ret

%ifdef FDLG_MOD
; =============================================================================
; THE RESIDENT HALF (SPEC.md 2.8.1) - kern_small only
; =============================================================================
; --- fdlg_open: the ONE place a human waits, so the ONE place that loads ----
; Its CF=1 already means "refused - one is already up, or the table is full",
; and every caller treats that as "the command does nothing". A module that
; will not load is the same answer.
fdlg_open_x:
    push ax
    mov al, MOD_FDLG
    call mod_need               ; CF=1: the image is not in RAM
    pop ax
    jc .no                      ; CF=1 already: the refusal
    call far [FDFP + FDE_OPEN*4]
.no:
    ret

fdlg_reap_x:
    cmp word [fdlg_win], 0
    je .drop                    ; ALREADY GONE, and the image is what is left
    call far [FDFP + FDE_REAP*4]
    cmp word [fdlg_win], 0
    jne .no                     ; still up - or a callback opened another -
.drop:                          ; so the image stays
    push ax
    mov al, MOD_FDLG
    call mod_drop
    pop ax
.no:
    retf

fdlg_grab_x:
    cmp word [fdlg_win], 0      ; CF=0 either way, which with no chooser is
    je .no                      ; fdlg_gate's `.none` answer: nothing to
    call far [FDFP + FDE_GRAB*4] ; swallow, so the press passes through
.no:
    retf

fdlg_top_x:
    cmp word [fdlg_win], 0
    je .no
    call far [FDFP + FDE_TOP*4]
.no:
    retf                        ; BX unchanged, which is wm_top's own answer

; fdlg_onup - the chooser's W_ONMOUSEUP (SPEC.md 38.3). Installed only on
; the chooser's window, which exists only while the image is loaded, so it
; needs no guard of its own
fdlg_onup:
    push ax
    mov al, FDH_UP
    call far [FDFP + FDE_HOOK*4]
    pop ax
    ret

    ; (NO far entries for the image here any more: FDX is fdx_go, which
    ; reaches every .cold body it names through COLD_SEG and cold_kretf)
%endif

section .text
