; =============================================================================
; os8088 - apps/os88data.inc
;
; SYSTEM\APPDATA ON OUR OWN VOLUME (SPEC.md 19.9, 19.9.1): read a small file
; there, write it back, and stand where we were afterwards.
;
; This is the walker five packages carried as private copies (Cyclone, Tank
; Attack, Dot Delirium, Clear Skies, PIXELSTEIN 3D - pxset.inc counted them),
; and the copies all made the same expensive choice for a reason that was
; true when it was made and has not been since. docs/plans/NAV-COST-PLAN.md is
; the account; the short form:
;
;  - **Every step was OSAPI_FILE_GOTO, which is a REMOUNT for DISPLAY** - the
;    BPB, the directory scan, the sort and an icon harvest - and it also
;    DROPS SPEC.md 19.2.3's cached directory window on the way past. Four of
;    them a save and four a load. The fork owner measured Tank Attack at about
;    six seconds to save a 46-byte score file.
;  - The copies refused OSAPI_FILE_GOTO_Q correctly: it moves the GLOBAL cwd
;    and not the instance's, so the very next file cell undid it. The slot
;    that answers that refusal is **OSAPI_FILE_GOTO_QM** (SPEC.md 74.1): the
;    same quiet stand, and the instance moves with it. Inside the volume we
;    are on it is a WORD - fcp_goto's own path - once dsk_media_ok has
;    agreed the floppy cannot have been swapped (the motor is still turning);
;    otherwise it is a QUIET mount, which reads the boot sector, keeps the
;    BPB and the FAT window when the signature matches, and keeps the
;    directory cache. No scan, no sort, no harvest.
;
; Then two things this file adds, because a score table is written again and
; again in one session:
;
;  - **THE FOLDER IS BANKED.** The walk's answer - APPDATA's first cluster on
;    our drive - is kept, and the next visit is one GOTO_QM to it instead of a
;    walk. That is the same trust the kernel already extends to the
;    instance's own folder (inst_vol_enter is a word compare against a banked
;    cluster), guarded by the same media test inside GOTO_QM; the copies'
;    comment refusing a cached cluster ("a write into whatever cluster 3 is on
;    somebody else's floppy") predates that test (SPEC.md 18.9.1.1). And a
;    banked visit never CREATES anything: see the next point.
;  - **A REWRITE IS ONE SECTOR.** OSAPI_FILE_WRITE replaces: it frees the old
;    chain, allocates a new one, writes the FAT (both copies) and the entry.
;    For a file whose length has not changed, OSAPI_FILE_WRITE_AT at offset 0
;    is SPEC.md 18.4.7's INSIDE arm: no cluster allocated, no FAT sector
;    written, and with the end exactly at the size no entry store either -
;    the data sector and nothing else. Its own refusal is the witness the
;    bank needs (NAV-COST-PLAN 4.4): FERR_NOENT at a banked folder means the
;    folder is not the one we banked, and the write falls back to a fresh walk
;    and a full OSAPI_FILE_WRITE.
;
; WHEN THE IN-PLACE WRITE IS TAKEN, and why not always. WRITE_AT is RAW
; (SPEC.md 20.14.3) and leaves the entry's compression hint alone (20.14.4),
; so overwriting a file that something else stored COMPRESSED would leave a
; hint describing bytes that are no longer there. So the fast arm is taken
; only for a file this package has itself written IN FULL since the folder
; was last walked - the full write's commit derived the hint from our plain
; bytes and so cleared it. The caller hands over one byte per file (DI) and
; this file stamps it with the walk's generation; any later walk, for any
; reason, makes every stamp stale. So the FIRST save of a session is a full
; write - quiet, walk-free once banked, but a full write - and every save
; after it is one sector. kern_small has no WRITE_AT (18.4.7.4): its stub
; answers CF=1, and every save there takes the full arm, still with no
; display remount in it. A count that is a WHOLE number of sectors takes the
; full arm too: WRITE_AT refuses a write ending short of the size only when
; its last sector is partial (18.4.7), so 512 bytes into a file of 1,024
; would answer CF=0 and leave the old size and the old tail.
;
; WHAT YOU GET
;   od_read   SI = a NUL 8.3 name, ES:BX = buffer, DX:CX = its capacity.
;             out exactly OSAPI_FILE_READ's (CF=0, DX:AX = bytes), or CF=1
;             AX = FERR_* - FERR_NOENT when there is no SYSTEM\APPDATA here.
;   od_write  SI = a NUL 8.3 name, ES:BX = bytes, CX = count (1..65535),
;             DI = YOUR state byte for this file (in DS, 0 at load, never
;             written by you afterwards). out CF=0 AX=0, or CF=1 AX = FERR_*.
; Both preserve every register except their outputs, and leave the instance
; standing where it stood - including on every refusal. UI-task context only,
; like every file cell (SPEC.md 20.6 rule 7); a bracket IS the UI task
; (SPEC.md 53.7), so both are legal inside one.
;
; THE BOOT VOLUME INSTEAD, by `%define OD_BOOTVOL` before the include. 19.9's
; rule is the program's OWN volume, and it holds for every package that only
; ever reads back what it wrote. It does not hold for REDLINE (SPEC.md
; 103.6.4): its REDLINE.CFG is read by RLAPPLY, which lives in SYSTEM/ on the
; disk the machine booted from - so both of them name OSAPI_VOL_SYS's volume,
; and on a one-drive machine a Save asks for the system disk. The visit still
; ends where the instance stood, on whatever volume that was.
;
; It keeps 30 bytes of state in YOUR IMAGE (the banked pair, the generation,
; one OSAPI_FILE_FIND record), so it must be %included where data may sit -
; anywhere a package's strings already do. Every label is od_-prefixed.
; =============================================================================

od_s_system: db 'SYSTEM', 0
od_s_appdat: db 'APPDATA', 0
od_home:     dw 0               ; where the instance stood before this visit
od_homedrv:  db 0
%ifdef OD_BOOTVOL
od_tdrv:     db 0               ; the volume visited: the boot volume
 %define OD_TDRV od_tdrv
%else
 %define OD_TDRV od_homedrv     ; ...or, by default, our own
%endif
od_drv:      db 0               ; the banked folder: its drive...
od_clus:     dw 0               ; ...and its first cluster
od_gen:      db 0               ; 0 = nothing banked; else the walk's number
od_fresh:    db 0               ; 1 = od_enter WALKED on this call
od_find:     times OSAPI_FIND_SZ db 0

; -----------------------------------------------------------------------------
; od_enter - stand in SYSTEM\APPDATA on the instance's own volume
; out: CF=0 standing there (the instance marked there too, so the next file
;      cell resolves there), [od_fresh] = 1 if this call walked;
;      CF=1 not there, and the instance is back where it stood
; preserves every register
; -----------------------------------------------------------------------------
od_enter:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE            ; DX = our folder, BL = our drive
    mov [od_home], dx
    mov [od_homedrv], bl
%ifdef OD_BOOTVOL
    call OSAPI_VOL_SYS              ; BL = the volume the machine booted from
    mov [od_tdrv], bl
%endif
    mov byte [od_fresh], 0
    cmp byte [od_gen], 0
    je .walk                        ; nothing banked
    cmp bl, [od_drv]
    jne .walk                       ; banked on another volume: not ours now
    mov dx, [od_clus]
    call od_qm                      ; a word, or a quiet mount
    jnc .ok
.walk:
    call od_walk
    jnc .ok
    call od_back                    ; nothing to stand in: home, and refuse
    stc
.ok:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; od_walk - root -> SYSTEM -> APPDATA on [OD_TDRV], quietly, and bank it
; out: CF=0 standing there and banked under a NEW generation; CF=1 the bank
;      is dropped and the volume is somewhere on the way (od_back fixes that)
; clobbers: AX, BX, DX, flags
; -----------------------------------------------------------------------------
od_walk:
    mov byte [od_gen], 0            ; whatever was banked is not trusted now
    mov bl, [OD_TDRV]
    xor dx, dx                      ; the ROOT of our own volume
    call od_qm
    jc .out
    push si
    mov si, od_s_system
    call od_dive
    jc .pop
    mov si, od_s_appdat
    call od_dive                    ; DX = APPDATA's cluster on CF=0
.pop:
    pop si
    jc .out
    mov [od_clus], dx
    mov al, [OD_TDRV]
    mov [od_drv], al
    inc byte [od_lastgen]           ; a new generation: every DI stamp from
    jnz .gen                        ; the last walk is stale. 0 means
    inc byte [od_lastgen]           ; "nothing banked", so it is skipped
.gen:
    mov al, [od_lastgen]
    mov [od_gen], al
    mov byte [od_fresh], 1
    clc
.out:
    ret
od_lastgen:  db 0

; od_dive - step into the folder named at SI (a quiet, marked move)
; out: CF=0 and DX = its first cluster; CF=1 there is none
; clobbers: AX, BX, DX, flags
od_dive:
    push cx
    push di
    push es
    xor cx, cx                      ; the ordinal, 0 to start
.each:
    push ds                         ; ES EVERY TIME: nothing promises it
    pop es                          ; survives a far call into the kernel
    mov di, od_find
    call OSAPI_FILE_FIND
    jc .out
    cmp word [od_find + 14], OSAPI_FT_DIR
    jne .each                       ; only a FOLDER, and type 1 is a PACKAGE
    push si                         ; the name, compared - it is NUL-ended
    mov di, od_find                 ; on both sides, as FILE_FIND spells it
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .differ
    inc si
    inc di
    or al, al
    jnz .cmp
    pop si
    mov dx, [od_find + 16]
    mov bl, [OD_TDRV]
    call od_qm
    jmp short .out                  ; CF is GOTO_QM's answer
.differ:
    pop si
    jmp short .each
.out:
    pop es
    pop di
    pop cx
    ret

; od_qm - OSAPI_FILE_GOTO_QM with CX, SI and DI kept. On a REDIRECTED volume
; the slot reaches the driver's FSV_CHDIR and those three are the driver's
; (os88api.inc says so at OSAPI_FILE_GOTO_Q) - and every caller here is
; holding the name, the count or the stamp in one of them.
od_qm:
    push cx
    push si
    push di
    call OSAPI_FILE_GOTO_QM
    pop di
    pop si
    pop cx
    ret

; od_back - stand where od_enter found us. Preserves everything, flags too.
od_back:
    pushf
    push ax
    push bx
    push dx
    mov dx, [od_home]
    mov bl, [od_homedrv]
    call od_qm                      ; a word: we never left the volume. A
    pop dx                          ; refusal here has nobody to tell, as
    pop bx                          ; the old FILE_GOTO's had not either
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; od_read - OSAPI_FILE_READ, in SYSTEM\APPDATA
; in/out: OSAPI_FILE_READ's; CF=1 AX = FERR_NOENT when there is no folder
; -----------------------------------------------------------------------------
od_read:
    call od_enter
    jc .nofolder
    push cx                         ; the capacity, for a retry - and CX is
    push dx                         ; the caller's to keep
    call OSAPI_FILE_READ
    jnc .done
    cmp ax, FERR_NOENT              ; not here AND we came by the bank: the
    jne .fail                       ; folder may not be the one we banked,
    cmp byte [od_fresh], 0          ; so walk to the real one and ask once
    jne .fail                       ; more (a walked folder's NOENT is true)
    push bx
    call od_walk
    pop bx
    jc .gone
    pop dx
    pop cx
    push cx
    push dx
    call OSAPI_FILE_READ
    jmp short .done
.gone:
    mov ax, FERR_NOENT
.fail:
    stc                             ; EXPLICITLY: the compares above leave
.done:                              ; CF describing themselves, not the read
    call od_back                    ; flags kept: the read's answer
    pop cx                          ; the banked DX is dropped - DX is the
    pop cx                          ; read's output either way - and CX is
    ret                             ; put back; `pop` writes no flag
.nofolder:
    mov ax, FERR_NOENT              ; (CF=1 from od_enter)
    ret

; -----------------------------------------------------------------------------
; od_write - write the file in SYSTEM\APPDATA, in place when it can be
; in:  SI = name, ES:BX = bytes, CX = count (>= 1), DI = this file's state
;      byte in DS
; out: CF=0 AX=0, or CF=1 AX = FERR_*
; -----------------------------------------------------------------------------
od_write:
    push dx
    call od_enter
    jc .nofolder
    mov al, [od_gen]                ; ONE SECTOR, when the last full write of
    cmp al, [di]                    ; this file is of the folder we stand in
    jne .full
    test cx, 511                    ; ...and not a WHOLE number of sectors:
    jz .full                        ; WRITE_AT lets such a count end short
    xor ax, ax                      ; offset 0 (DX:AX), CX bytes
    xor dx, dx
    call OSAPI_FILE_WRITE_AT        ; INSIDE (18.4.7): no FAT, no entry
    jnc .done
.full:                              ; (a stamp that WAS current is stale
    cmp byte [od_fresh], 0          ; by the walk below: in-place is only
    jne .write                      ; tried on a banked visit)
    push bx                         ; WE CAME BY THE BANK: a create or a
    call od_walk                    ; replace goes where a walk says, never
    pop bx                          ; where we remember
    jnc .write
    mov ax, FERR_NOENT              ; no folder now: refused
    jmp short .done                 ; (CF=1 from the walk)
.write:
    xor dx, dx                      ; DX:CX = the count
    call OSAPI_FILE_WRITE
    jc .done
    mov al, [od_gen]                ; IT LANDED, plain: the next write of the
    mov [di], al                    ; same length may be in place
    xor ax, ax
.done:
    call od_back                    ; flags kept: the write's answer
    pop dx
    ret
.nofolder:
    mov ax, FERR_NOENT
    pop dx
    ret
