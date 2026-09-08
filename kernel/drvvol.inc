; =============================================================================
; os8088 - kernel/drvvol.inc - the volume plumbing (SPEC.md 51.0)
;
; FOUR ROUTINES THAT LIVED IN driver.inc AND ARE NOT DRIVER CODE. They were
; written there because the driver loader was their first caller - it has to
; get to A: to find a `.DRV`, and to put the user's volume back afterwards
; (SPEC.md 51.5.2) - and they stayed there while three other subsystems grew
; callers of their own:
;
;   drv_mounted     make the boot volume current, if it can be
;   drv_vol_bank    ...and bank / restore whatever the user was looking at
;   drv_vol_back
;   drv_find        a file's size, straight out of the directory sectors
;                   rather than out of the mount snapshot (SPEC.md 19.6)
;
; `mod.inc` calls three of them on the path that loads an ON-DEMAND MODULE,
; `instance.inc` and `ui.inc` call the bank pair, and `filecp.inc` and
; `disk.inc` call drv_mounted. So they are what SPEC.md 51.0 could not take
; away: kern_small has no loadable drivers at all and needs these four MORE
; than kern_big does, because the Control Panel, Format, Clone, Cut/Copy/Paste
; and the Standard File dialog are all files on the disk over there.
;
; THE NAMES KEEP THEIR `drv_` PREFIX, against the label-hygiene rule in
; CLAUDE.md, and deliberately: they have ~15 call sites across seven files and
; renaming them would put a diff over all of those to say nothing. The prefix
; is now a historical fact about where they were written rather than a claim
; about what they do, which this banner is here to record.
;
; A PURE MOVE. Not a line of the four bodies changed, section directives
; included - which is what lets `kernsize` say the split cost nothing.
; =============================================================================

; -----------------------------------------------------------------------------
; drv_mounted - make drive A: the mounted volume, if it can be
; in:  nothing
; out: CF = 0 mounted; CF = 1 no readable system disk
; clobbers: nothing else (flags)
;
; A full remount every time, deliberately: the disk can be swapped between
; two Control Panel clicks, and every other mount in this OS is a full
; remount for exactly that reason (SPEC.md 18.3). A driver load is not the
; place to invent a caching rule.
; -----------------------------------------------------------------------------
section .cold                   ; ...and back: drv_mounted is CTRL.DRV's too
drv_mounted:
    push ax
    push dx
    mov dl, [dsk_bootvol]       ; the disk we booted from - A: on a floppy
                                ; machine, where this byte is 0 and this is
                                ; the `xor dl, dl` it replaced, and the
                                ; installed PARTITION on a machine that boots
                                ; from its hard disk (SPEC.md 52.10.3)
    xor ax, ax                  ; ...at its root, which is where 19.6 puts
                                ; KERNEL.SYS, SYSTEM.CFG and every *.DRV
    call dsk_chdir_q_x            ; QUIET (SPEC.md 18.9): all three callers here
                                ; read or write a file BY NAME - drv_find is
                                ; dskw_stat and drv_cfg_* are dskw_read /
                                ; dskw_write_sys, none of which look at the
                                ; listing - so the scan, the sort and one
                                ; icon-harvest read per file in A:'s root are
                                ; all bought for nothing. ui_tm_open used to
                                ; be named here as the caller that DOES need
                                ; a listing, because it handed the loader a
                                ; directory index; SPEC.md 21.4 gave it
                                ; ld_run_name and SPEC.md 28.3.1 spent it, so
                                ; that exception is gone and every mount on
                                ; the path to a system file is quiet. It still
                                ; mounts for itself rather than coming through
                                ; here, because it has to reach a SUBDIRECTORY
                                ; (SPEC.md 28.3) and this stops at the root
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_vol_bank / drv_vol_back - put the user's volume back (SPEC.md 51.5.2)
; in (bank):  nothing.  in (back): the banked pair
; out: nothing. BOTH PRESERVE EVERY REGISTER AND THE FLAGS.
;
; drv_mounted goes to A: because that is where the system files are, and for
; a long time nothing put the volume back - so a user working on B: was
; silently moved to A: by a Control Panel click. Nothing said so: the next
; Standard File dialog simply opened on the wrong disk, and a package
; resolving a file name resolved it there. It is the same shape of bug the
; hard disk's Mount had (SPEC.md 52.6) and the same answer ui_tm_open already
; used for the Task Manager (SPEC.md 28.3) - which is why this is a routine
; and not a third open-coded dance.
;
; FLAGS ARE PRESERVED, and that is the load-bearing part rather than
; politeness: both callers return a result in CF and in AX, the restore runs
; at their common exit, and a remount that failed must not be mistaken for a
; driver that failed to load. A failed restore is therefore SWALLOWED - the
; volume is left at its root with the write gate shut, exactly as after any
; failed mount, which is ui_tm_back's answer too and a survivable place to be.
;
; THE COMPARE IS WHAT MAKES IT FREE. A remount is a floppy's worth of sectors
; (SPEC.md 19.2), so putting back a volume that never moved would put one on
; the boot path: drv_boot calls drv_load once per wanted driver, and each of
; those would have paid a mount to arrive where it already was. Banked equal
; to current means nothing moved and there is nothing to undo.
;
; One static pair, like ui_tm_cwd and for the same reason: this is UI-task
; context and the two brackets never nest - cp_flush_x is the only caller of
; drv_cfg_save, and drv_load reaches no path that writes the settings file.
; In .text with real initialisers, because -f bin zeroes no .bss and A:'s root
; is the right answer before anything has been banked.
;
; **IT READS THE MACHINE'S OWN POSITION AND NOT osapi_file_here'S.** That cell
; answers for the CALLING INSTANCE (SPEC.md 19.2.1) and only falls back to the
; machine when there is no instance stamp - and the stamp is set for the length
; of any dispatched callback, which is where every one of these brackets
; actually runs. So a verb inside a window's own AM_ONCMD banked THAT WINDOW'S
; instance directory, and the restore then MOVED the machine there: SPEC.md
; 22.22's Compress read a file on B: and wrote the result to A:, which is the
; exact bug this routine exists to prevent, arriving through the routine.
;
; What it wants is the two words drv_mounted is about to overwrite, and those
; are [dsk_cwd] and [disk_drive]. It was invisible for as long as both callers
; worked by DRIVE - a format and a driver load do not resolve a name in a
; directory - and the first caller that wrote a file BY NAME after a module
; load found it immediately. Four bytes shorter, and no far call.
; -----------------------------------------------------------------------------
drv_vol_bank:
    push ax
    mov ax, [dsk_cwd]
    mov [drv_vol_cwd], ax
    mov al, [disk_drive]
    mov [drv_vol_drv], al
    pop ax
    ret

drv_vol_back:
    pushf
    push ax
    push dx
    mov ax, [drv_vol_cwd]       ; dsk_chdir's arguments, loaded before the
    mov dl, [drv_vol_drv]       ; compare because they ARE the compare
    cmp dl, [disk_drive]        ; still where the user left us? then there is
    jne .go                     ; nothing to undo, and the remount is the
    cmp ax, [dsk_cwd]           ; whole cost
    je .out
.go:
    call dsk_chdir_q_x            ; CF = the remount failed; swallowed by the
                                ; popf below, deliberately.
                                ;
                                ; QUIET (SPEC.md 18.9), because putting the
                                ; user's VOLUME back is not the same as
                                ; putting a LISTING on screen: every Disk
                                ; window paints from its own view cache
                                ; (22.1), so nothing here is drawn from the
                                ; global snapshot, and whoever reads it next
                                ; pays for the rebuild if it is ever needed
.out:
    pop dx
    pop ax
    popf
    ret
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


drv_vol_cwd: dw 0               ; the directory and drive to go back to
drv_vol_drv: db 0
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


; -----------------------------------------------------------------------------
; drv_find - a file's byte size, straight out of the directory
; in:  SI -> a NUL display name (upper case, as dsk_synth writes them);
;      drive A: already mounted
; out: CF = 0 and AX = its size; CF = 1 not found
; clobbers: AX (the output), flags
;
; **dskw_stat, NOT dsk_find_name** (SPEC.md 19.6). The latter walks the mount
; SNAPSHOT, and the snapshot is a display listing: dsk_synth filters the
; hidden and system entries out of it, which is exactly what a .DRV is now.
; Through it every driver on the disk reads as "not on the system disk" -
; and the Control Panel says so on its Drivers page, which is a long way
; from the cause. dskw_stat walks the directory sectors themselves and so
; sees a file whether or not it is meant to be shown.
;
; What stays here is the one thing a DRIVER wants out of the entry: its size,
; which is what lets the claim be made at exactly the right size before a
; byte of the file is read. DX:CX is a 32-bit size and a driver is capped at
; DRV_MAX_KB, so a non-zero high word is simply not one of ours.
;
; **THE SIZE IS WHAT THE FILE BECOMES, NOT WHAT IT OCCUPIES** (SPEC.md
; 20.13.3.1): a compressed .DRV or module is a 'CZ' file, dskw_read_x will
; expand it into the claim, and the claim has to be cut from the size the
; read is going to deliver. That is the directory hint's whole purpose
; (SPEC.md 20.14.1), and dskw_stat_x has just left the raw entry in dskw_raw
; - so the hint costs a compare and two loads, and no I/O.
; -----------------------------------------------------------------------------
drv_find:
    push bx
    push cx
    push dx
    push si
    call dskw_stat_x              ; AL = attribute, BX = first cluster,
    jc .no                      ; DX:CX = the size it OCCUPIES...
    mov al, [dskw_raw+DSK_R_CZM]    ; ...and the hint says what it BECOMES:
    sub al, DSK_CZ_MARK             ; the mark less its format (SPEC.md
    cmp al, LZ_LZB                  ; 20.14.1), 0x5A and 0x5B pass
    ja .plain
    mov cx, [dskw_raw+DSK_R_CZL]
    mov dl, [dskw_raw+DSK_R_CZH]
    mov dh, 0
.plain:
    test dx, dx
    jnz .no
    mov ax, cx
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
section .text                   ; SPEC.md 1: every file ends where it began
