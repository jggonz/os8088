; =============================================================================
; os8088 - apps/syscheck/disk.inc - Check the disk, repair it, defragment it
;                                   (SPEC.md 118.14)
;
; The chooser's three disk tasks. The work is FSCHECK.DRV's, which is kernel
; code (a package has no sector read or write, SPEC.md 118.9): this file
; reads that image from the folder SYSCHECK.O88 was launched from - beside it
; on util360's root and in SYSTEM\ everywhere else - claims the workspace
; the engine asks for, and calls OSAPI_VOL_CHECK once a step from the same
; wake slices the release checks run in (SPEC.md 118.3). A finding arrives
; as a row, a severity, a code, two numbers and a path; the words are made
; here, so the window and the report say the same thing and the kernel
; carries no English for it.
; =============================================================================

; the disk task's rows (SPEC.md 118.14): nine, as the release checks' are
SD_FAT      equ 0
SD_REPAIR   equ 8

TASK_REL    equ 0                   ; the chooser's tasks
TASK_CHECK  equ 1
TASK_FIX    equ 2
TASK_DEFRAG equ 3
SC_NTASK    equ 4

; -----------------------------------------------------------------------------
; sc_fsk - OSAPI_VOL_CHECK with the image and the workspace
; in:  AL = verb (AH, CX its arguments); out CF, AX; every other register kept
; -----------------------------------------------------------------------------
sc_fsk:
    push dx
    push es
    mov es, [sc_fsel]
    mov dx, [sc_wseg]
    call OSAPI_VOL_CHECK
    pop es
    pop dx
    ret

; -----------------------------------------------------------------------------
; sc_dstep - one piece of a disk task (sc_step's, when [sc_task] is one)
; -----------------------------------------------------------------------------
sc_dstep:
    cmp byte [sc_dsub], 0
    jne .step
    jmp sc_dinit
.step:
    mov al, OSAPI_FSK_STEP
    call sc_fsk
    jc .err
    push ax
    call sc_dcounts
    pop ax
    cmp ax, FSKS_FIND
    je .find
    cmp ax, FSKS_DONE
    je .done
    ret
.find:
    jmp sc_dfound
.err:
    jmp sc_dabort
.done:
    ; --- the Repairs row says what was done, or that nothing was asked ---
    push es
    mov es, [sc_wseg]
    mov ax, [es:FSW_FIXED]
    pop es
    mov si, sc_dr_none
    cmp byte [sc_task], TASK_FIX
    jne .rep
    mov si, sc_dr_nofix
    or ax, ax
    jz .rep
    mov di, sc_line
    call dec_u16
    mov si, sc_dr_fixed
    call sc_scpy
    mov si, sc_line
.rep:
    call sc_dvpath
    mov ax, (SV_NOTE << 8) | SD_REPAIR
    xor bl, bl
    xor dx, dx
    call sc_addf
    mov word [sc_rdone], (1 << SC_NCHK) - 1
    jmp sc_scan_end                 ; ...which ends the engine (sc_dfinish)

; -----------------------------------------------------------------------------
; sc_dinit - the first step: the volume, the engine, the workspace, BEGIN
; -----------------------------------------------------------------------------
sc_dinit:
    call OSAPI_FILE_HERE            ; where we were, to go back to
    mov [sc_homedx], dx
    mov [sc_homebl], bl
    mov bl, [sc_vol]
    call OSAPI_VOL_SYS
    mov al, 0
    cmp bl, [sc_vol]
    jne .nb
    inc al
.nb:
    mov [sc_isboot], al
    mov al, [sc_vol]
    add al, 'A'
    mov [sc_vlet], al
    mov al, [sc_vol]
    call OSAPI_VOL_KIND
    mov ah, 0
    jc .k
    cmp al, VK_FIXED
    jne .k
    inc ah
.k:
    mov [sc_fixed], ah
    or word [sc_dirty], SYD_HEAD | SYD_SYS
    call sc_fload                   ; the engine, from beside us
    jnc .have
    jmp sc_dfail                    ; SI = why not
.have:
    ; --- the volume's root, its free space, and what the engine needs ----
    xor dx, dx
    call sc_goto
    jnc .stood
    mov si, sc_r_novol
    jmp sc_dfail
.stood:
    call OSAPI_VOL_STAT
    jc .nostat
    mov cl, 0
    cmp dx, 4085
    jb .f12
    inc cl
.f12:
    mov [sc_fat16], cl
    mul bx
    shr dx, 1
    rcr ax, 1
    mov [sc_freelo], ax
    mov [sc_freehi], dx
.nostat:
    mov word [sc_wseg], 0
    mov al, OSAPI_FSK_SIZE
    call sc_fsk                     ; AX = the workspace's KB
    jc sc_dabort
    mov [sc_wkb], ax
    mov cx, ax                      ; all of it page-safe: the engine moves
    call OSAPI_MEM_CLAIM_DMA        ; sectors straight into it
    jnc .ws
    mov si, sc_t_nomem
    jmp sc_dfail
.ws:
    mov [sc_wseg], dx
    mov ah, 0                       ; the mode: Check, Fix, or Defragment
    cmp byte [sc_task], TASK_FIX
    jne .m1
    mov ah, FSKM_FIX
.m1:
    cmp byte [sc_task], TASK_DEFRAG
    jne .m2
    mov ah, FSKM_DEFRAG
.m2:
    mov al, OSAPI_FSK_BEGIN
    mov cx, [sc_wkb]
    call sc_fsk
    jc sc_dabort
    mov byte [sc_dsub], 1
    mov byte [sc_dphase], 0FFh
    mov ax, (1 << SC_NCHK) - 1      ; every row runs until the end
    xor dx, dx
    xchg ax, dx
    mov cl, P_INIT                  ; (the pass is not read in a disk task)
    jmp sc_nextpass

; -----------------------------------------------------------------------------
; sc_fload - FSCHECK.DRV, read once from the folder we were launched from
;            (SPEC.md 118.14.1), for the disk tasks and for Clean Up (118.8.1)
; out: CF=0 [sc_fseg]/[sc_fsel] hold it; CF=1 SI = why not. Every other
;      register kept; the folder is the launch folder after a first read
; -----------------------------------------------------------------------------
sc_fload:
    cmp word [sc_fseg], 0
    je .read
    clc
    ret
.read:
    push ax
    push bx
    push cx
    push dx
    push di
    mov dx, [sc_ldx]
    mov bl, [sc_lbl]
    call OSAPI_FILE_GOTO_QM
    jc .noimg
    mov si, sc_n_fsck
    call sc_probe                   ; DX = its KB
    cmp al, 1
    jne .noimg
    or dx, dx
    jz .noimg
    mov ax, dx
    push ax
    call OSAPI_MEM_CLAIM            ; DX = the segment
    pop ax
    jc .nomem
    mov [sc_fseg], dx
    push es
    mov es, dx
    xor bx, bx
    mov dx, ax                      ; DX:CX = the claim's bytes
    mov cl, 6
    shr dx, cl
    and ax, 63
    mov cl, 10
    shl ax, cl
    xchg ax, cx
    mov si, sc_n_fsck
    call OSAPI_FILE_READ
    pop es
    jc .rdfail
    ; ...and the selector to RUN it through (SPEC.md 118.14.8): on kern_mega
    ; a claim above 1MB is a data selector alone, and the kernel far-calls
    ; the image through a code half - so ask for a code/data PAIR over it.
    ; A real-mode kernel answers the claim's segment, which is both
    mov dx, [sc_fseg]
    mov [sc_fsel], dx
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUBC
    call OSAPI_SEG
    jc .ok
    mov [sc_fsel], dx
.ok:
    clc
    jmp short .out
.rdfail:
    mov dx, [sc_fseg]
    call OSAPI_MEM_FREE
    mov word [sc_fseg], 0
.noimg:
    mov si, sc_de_noimg
    jmp short .fail
.nomem:
    mov si, sc_t_nomem
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_dcounts - the counter line and the bar from the workspace's record
sc_dcounts:
    push es
    mov es, [sc_wseg]
    mov ax, [es:FSW_FILES]
    mov [sc_nfiles], ax
    mov ax, [es:FSW_DIRS]
    mov [sc_nfold], ax
    mov ax, [es:FSW_MOVED]
    mov [sc_npkg], ax
    or word [sc_dirty], SYD_CNT
    mov bl, [es:FSW_PHASE]
    xor bh, bh
    cmp bl, 7
    jbe .ph
    mov bl, 7
.ph:
    cmp bl, [sc_dphase]             ; a new phase: the path line names it
    je .same
    mov [sc_dphase], bl
    push bx
    shl bx, 1
    mov si, [sc_dphn+bx]
    mov di, sc_cpath
    call sc_scpy
    or word [sc_dirty], SYD_PATH
    pop bx
.same:
    shl bx, 1
    mov ax, [sc_dpbase+bx]          ; the bar: each phase's share, and inside
    cmp bl, 3 * 2                   ; the FAT scan, its progress
    jne .set
    push ax
    mov ax, [es:FSW_DONE]
    mov cx, 200
    mul cx
    mov cx, [es:FSW_TOTAL]
    jcxz .z
    cmp dx, cx
    jae .z
    div cx
    jmp short .add
.z:
    xor ax, ax
.add:
    pop cx
    add ax, cx
.set:
    pop es
    jmp sc_setprog

; -----------------------------------------------------------------------------
; sc_dfound - the engine has a finding: words, a path, sc_addf
; -----------------------------------------------------------------------------
sc_dfound:
    push es
    mov es, [sc_wseg]
    mov di, sc_cpath                ; "C:" + its path, "C:" alone for the volume
    call sc_drive2
    mov si, FSW_FPATH
    mov cx, FR_PATHN - 3
.p:
    mov al, [es:si]
    or al, al
    jz .pe
    mov [di], al
    inc si
    inc di
    loop .p
.pe:
    mov byte [di], 0
    ; --- the reason, from the code's template (\1 = A, \2 = B) ------------
    mov bl, [es:FSW_FCODE]
    xor bh, bh
    dec bx
    cmp bx, 23
    jb .code
    xor bx, bx
.code:
    shl bx, 1
    mov si, [sc_dftab+bx]
    mov di, sc_line
.t:
    lodsb
    cmp al, 1
    jne .t2
    mov ax, [es:FSW_FA]
    mov dx, [es:FSW_FA+2]
    jmp short .num
.t2:
    cmp al, 2
    jne .t3
    mov ax, [es:FSW_FB]
    mov dx, [es:FSW_FB+2]
.num:
    push si
    call dec_u32
    pop si
    jmp short .t
.t3:
    mov [di], al
    or al, al
    jz .tend
    inc di
    jmp short .t
.tend:
    cmp byte [es:FSW_FFIX], 0
    je .nofix
    cmp byte [es:FSW_FCODE], FSKF_SAVED_C ; these two ARE repairs
    jae .nofix
    mov si, sc_df_fixed
    call sc_scpy
.nofix:
    mov al, [es:FSW_FROW]
    mov ah, [es:FSW_FSEV]
    cmp byte [es:FSW_FFIX], 0       ; a REPAIRED finding is a note: it is no
    je .sev                         ; longer broken, and the rows and the
    mov ah, SV_NOTE                 ; result say what is still wrong
.sev:
    pop es
    xor bl, bl
    xor dx, dx
    mov si, sc_line
    jmp sc_addf
FSKF_SAVED_C equ 13                 ; FSKF_SAVED (SPEC.md 118.14.3)

; sc_dvpath - sc_cpath = "C:" (a finding about the volume)
sc_dvpath:
    mov di, sc_cpath
    call sc_drive2
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; sc_dabort - the engine refused (AX = FSKE_*): say why, and end
; sc_dfail  - ...or we could not start it (SI = why)
; -----------------------------------------------------------------------------
sc_dabort:
    dec ax
    cmp ax, 8
    jb .k
    mov ax, 3                       ; (an I/O error, for a code we do not know)
.k:
    mov bx, ax
    shl bx, 1
    mov si, [sc_detab+bx]
sc_dfail:
    call sc_dvpath
    mov ax, (SV_BROKEN << 8) | SD_FAT
    xor bl, bl
    xor dx, dx
    call sc_addf
    mov byte [sc_stopped], 1
    jmp sc_scan_end

; sc_dfinish - the engine's END, and the workspace back (sc_scan_end's)
sc_dfinish:
    cmp word [sc_wseg], 0
    je .out
    cmp byte [sc_dsub], 0
    je .free
    mov al, OSAPI_FSK_END
    call sc_fsk
    ; FOLDERS MOVED (118.14.5): the folder we came from may be one of them,
    ; and the cluster we banked for it names nothing now - so home, on the
    ; volume that was defragmented, is its root
    push es
    mov es, [sc_wseg]
    cmp word [es:FSW_MOVEDF], 0
    pop es
    je .free
    mov al, [sc_vol]
    cmp al, [sc_homebl]
    jne .lf
    mov word [sc_homedx], 0
.lf:
    cmp al, [sc_lbl]
    jne .free
    mov word [sc_ldx], 0
.free:
    mov dx, [sc_wseg]
    call OSAPI_MEM_FREE
    mov word [sc_wseg], 0
.out:
    mov byte [sc_dsub], 0
    ret

; --- the chooser's tasks (SPEC.md 118.14) ---------------------------------------
sc_tnames:  dw sc_tn0, sc_tn1, sc_tn2, sc_tn3
sc_tn0:     db 'Release files - all one release?', 0
sc_tn1:     db 'Check the disk', 0
sc_tn2:     db 'Check and repair the disk', 0
sc_tn3:     db 'Defragment the disk', 0
sc_h_task:  db 'What to do (Tab, 1-4):', 0

; --- the disk rows ---------------------------------------------------------------
sc_dnames:  dw sc_dn0, sc_dn1, sc_dn2, sc_dn3, sc_dn4, sc_dn5, sc_dn6, sc_dn7
            dw sc_dn8
sc_dn0:     db 'File allocation table', 0
sc_dn1:     db 'Folders', 0
sc_dn2:     db 'File chains', 0
sc_dn3:     db 'Cross-linked files', 0
sc_dn4:     db 'Lost clusters', 0
sc_dn5:     db 'Free space', 0
sc_dn6:     db 'Fragmentation', 0
sc_dn7:     db 'Defragment', 0
sc_dn8:     db 'Repairs', 0

sc_dphn:    dw sc_dp0, sc_dp1, sc_dp2, sc_dp3, sc_dp4, sc_dp5, sc_dp6, sc_dp7
sc_dp0:     db 'Starting', 0
sc_dp1:     db 'Comparing the two FAT copies', 0
sc_dp2:     db 'Walking every folder and chain', 0
sc_dp3:     db 'Scanning the FAT', 0
sc_dp4:     db 'Saving lost chains', 0
sc_dp5:     db 'Copying the FAT', 0
sc_dp6:     db 'Defragmenting', 0
sc_dp7:     db 'Finishing', 0
sc_dpbase:  dw 0, 0, 100, 500, 700, 800, 800, 1000

; --- what a finding says (SPEC.md 118.14.3): byte 1 = A, byte 2 = B ------------
sc_dftab:   dw sc_df1, sc_df2, sc_df3, sc_df4, sc_df5, sc_df6, sc_df7, sc_df8
            dw sc_df9, sc_df10, sc_df11, sc_df12, sc_df13, sc_df14, sc_df15
            dw sc_df16, sc_df17, sc_df18, sc_df19, sc_df20, sc_df21
            dw sc_df22, sc_df23
sc_df1:     db 'FAT copies differ in ', 1, ' of ', 2, ' sectors', 0
sc_df2:     db 'dot entry names cluster ', 1, ', not ', 2, 0
sc_df3:     db 'entry ', 1, ' is not a valid entry', 0
sc_df4:     db 'folder names cluster ', 1, ', off the disk', 0
sc_df5:     db 'nested ', 1, ' deep - its contents not checked', 0
sc_df6:     db 'chain leaves the disk at link ', 2, 0
sc_df7:     db 'chain runs into free cluster ', 1, 0
sc_df8:     db 'chain runs into bad cluster ', 1, 0
sc_df9:     db 1, ' clusters, its size needs ', 2, 0
sc_df10:    db 'size ', 1, ' bytes, chain holds ', 2, 0
sc_df11:    db 'shares cluster ', 1, ' with another file', 0
sc_df12:    db 1, ' lost clusters in ', 2, ' chains', 0
sc_df13:    db 'lost chain of ', 1, ' clusters saved', 0
sc_df14:    db 1, ' lost clusters freed', 0
sc_df15:    db 1, ' KB free of ', 2, ' KB', 0
sc_df16:    db 1, ' clusters marked bad', 0
sc_df17:    db 1, ' of ', 2, ' files are in pieces', 0
sc_df18:    db 1, ' files moved, ', 2, ' left in pieces', 0
sc_df19:    db 'not run: ', 1, ' errors - run Check and repair', 0
sc_df20:    db 'not asked', 0
sc_df21:    db 'folders not moved: close other programs (', 1, ')', 0
sc_df22:    db 1, ' folders moved', 0
sc_df23:    db "not done: a folder's contents were not checked", 0
sc_df_fixed: db ' (fixed)', 0
sc_dr_none:  db 'not asked', 0
sc_dr_nofix: db 'nothing needed repairing', 0
sc_dr_fixed: db ' repairs made', 0

; ...and why a run ended early (FSKE_* - 1)
sc_detab:   dw sc_de1, sc_de2, sc_de3, sc_de4, sc_de5, sc_de6, sc_de7, sc_de8
sc_de1:     db 'this kernel has no disk checker', 0
sc_de2:     db 'FSCHECK.DRV is from another build', 0
sc_de3:     db 'not a FAT disk this can check', 0
sc_de4:     db 'a disk read or write failed', 0
sc_de5:     db 'the disk changed - scan again', 0
sc_de6:     db 'not enough memory', 0
sc_de7:     db 'the disk is write-protected', 0
sc_de8:     db 'the disk checker could not do that', 0
sc_de_noimg: db 'FSCHECK.DRV is not beside SYSCHECK.O88', 0
sc_n_fsck:  db 'FSCHECK.DRV', 0
