; =============================================================================
; os8088 - apps/syscheck/scan.inc - the nine checks as a state machine
;                                   (SPEC.md 118.1, 118.3)
;
; sc_step does ONE bounded piece of the scan - one FIND, one header read, one
; 4KB chunk of a sum, one ASSOC.DAT row - and returns; sc_onwake calls it for
; up to a tick. Every pass leaves the instance standing where its next step
; expects (sc_goto is cheap when it is already there), and sc_scan_end puts it
; back where it was before the scan. Nothing here draws: it records findings
; and sets the D_* bits ui.inc's sc_drawowed reads.
;
; THE PASSES, and the rows each one runs and finishes:
;   P_INIT   the volume, SYSTEM\ and STIKAPPS\, ASSOC.DAT's program count
;   P_MAN    every manifest file, summed as stored (118.2)  -> rows 0, 1
;   P_LEFT   KPAD.TMP, SYSTEM\APPDATA                       -> row 5
;   P_RET    the retired drivers, asked for by name
;   P_WALK   every folder, every *.O88                      -> rows 2, 3, 4
;   P_CFG    SYSTEM.CFG                                     -> row 6
;   P_ASSOC  ASSOC.DAT, a program row a step                -> row 7
;   P_HIB    HIBERNAT.PTR and .IMG                          -> row 8
; =============================================================================

P_INIT      equ 0
P_MAN       equ 1
P_LEFT      equ 2
P_RET       equ 3
P_WALK      equ 4
P_CFG       equ 5
P_ASSOC     equ 6
P_HIB       equ 7

; -----------------------------------------------------------------------------
; sc_scan_begin - a fresh scan: everything back to "not checked"
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
sc_scan_begin:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor ax, ax                      ; the per-scan state, word by word
    mov di, sc_rbrk
    mov cx, SC_NCHK * 2             ; sc_rbrk and sc_rwrn
    rep stosw
    mov [sc_nf], ax
    mov [sc_flost], ax
    mov [sc_nrm], ax
    mov [sc_nfiles], ax
    mov [sc_nfold], ax
    mov [sc_npkg], ax
    mov [sc_nascapp], ax
    mov [sc_nfound], ax
    mov [sc_nnames], ax
    mov [sc_mbytes], ax
    mov [sc_prog], ax
    mov [sc_rdone], ax
    mov [sc_rrun], ax
    mov [sc_csys], ax
    mov [sc_cstik], ax
    mov [sc_freelo], ax
    mov [sc_freehi], ax
    mov [sc_stop], al
    mov [sc_stopped], al
    mov [sc_foreign], al
    mov [sc_kstate], al
    mov [sc_ascbad], al
    mov [sc_pass], al               ; P_INIT
    mov [sc_sub], al
    mov [sc_seluser], al
    mov [sc_sel], al
    mov [sc_sb+12], ax              ; the findings back at the top
    mov di, sc_mstat
    mov cx, 32
    rep stosw
    mov word [sc_btlbl], sc_s_stop  ; the first button is Stop while running
    mov byte [sc_run], 1
    call OSAPI_GET_TICKS
    mov [sc_tstart], ax
    mov word [sc_dirty], 0xFFFF     ; everything owed
    mov word [sc_drows], 0xFFFF
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_step - one piece of the scan
; in:  nothing; the UI task, gfx lock NOT held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (keeps BP)
; -----------------------------------------------------------------------------
sc_step:
    push es
    push ds
    pop es                          ; ES = ours: every file slot's buffer is
    cmp byte [sc_stop], 0
    je .go
    mov byte [sc_stopped], 1
    call sc_scan_end
    jmp short .out
.go:
    mov bl, [sc_pass]
    xor bh, bh
    shl bx, 1
    call [sc_passtab+bx]
.out:
    pop es
    ret

sc_passtab: dw sc_p_init, sc_p_man, sc_p_left, sc_p_ret
            dw sc_p_walk, sc_p_cfg, sc_p_assoc, sc_p_hib

; sc_nextpass - finish the rows AX names, run the rows DX names, go to pass CL
sc_nextpass:
    or [sc_rdone], ax
    or [sc_drows], ax
    mov ax, [sc_rrun]
    or [sc_drows], ax               ; the rows that stop running...
    mov [sc_rrun], dx
    or [sc_drows], dx               ; ...and the ones that start
    mov [sc_pass], cl
    mov byte [sc_sub], 0
    mov word [sc_idx], 0
    or word [sc_dirty], SYD_BAR
    mov cl, [sc_sel]                ; the selected row's state moved: its
    mov ax, 1                       ; empty list's "Checking..." line with it
    shl ax, cl
    test [sc_drows], ax
    jz .out
    or word [sc_dirty], SYD_FIND
.out:
    ret

; sc_goto - stand in a folder of the volume being checked
; in:  DX = its first cluster (0 = the root); out CF = 1 it would not stand
sc_goto:
    push ax
    push bx
    mov bl, [sc_vol]
    call OSAPI_FILE_GOTO_QM
    pop bx
    pop ax
    ret

; sc_probe - is a file here? (SPEC.md 118.3: a zero-capacity read)
; in:  SI = a NUL 8.3 name in the folder we stand in
; out: AL = 0 absent, 1 present, 2 unreadable; DX = its KB when present
;      (0 for an empty file); other registers kept
sc_probe:
    push bx
    push cx
    mov bx, sc_buf
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ
    mov bl, al
    mov al, 1
    jnc .empty                      ; CF = 0 with a zero capacity: 0 bytes
    cmp bl, FERR_BIG
    je .out                         ; ...it is there, DX = the KB it needs
    xor dx, dx
    mov al, 0
    cmp bl, FERR_NOENT
    je .out
    mov al, 2
    jmp short .out
.empty:
    xor dx, dx
.out:
    pop cx
    pop bx
    ret

; =============================================================================
; P_INIT
; =============================================================================
sc_p_init:
    call OSAPI_FILE_HERE            ; where we were, to go back to
    mov [sc_homedx], dx
    mov [sc_homebl], bl
    mov bl, [sc_vol]                ; the drive the chooser gave us (118.13)
    call OSAPI_VOL_SYS              ; ...and whether it is the boot one
    mov al, 0
    cmp bl, [sc_vol]
    jne .notboot
    inc al
.notboot:
    mov [sc_isboot], al
    mov bl, [sc_vol]
    mov al, bl
    add al, 'A'
    mov [sc_vlet], al
    mov al, bl
    call OSAPI_VOL_KIND             ; AL = VK_REMOVABLE / VK_FIXED
    mov ah, 0
    jc .kind
    cmp al, VK_FIXED
    jne .kind
    inc ah
.kind:
    mov [sc_fixed], ah
    xor dx, dx
    call sc_goto
    jnc .stood
    mov si, sc_r_novol              ; the volume will not even stand: say so
    call sc_rootpath                ; and stop, rather than report nine
    mov ax, (SV_BROKEN << 8) | SC_KERN ; checks of nothing
    xor bl, bl
    call sc_addf
    jmp sc_scan_end
.stood:
    call OSAPI_VOL_STAT             ; AX = sectors a cluster, BX = free
    jc .nostat                      ; clusters, DX = all of them
    mov cl, 0
    cmp dx, 4085                    ; FAT16 from 4,085 clusters (SPEC.md 19)
    jb .f12
    inc cl
.f12:
    mov [sc_fat16], cl
    mul bx                          ; DX:AX = free sectors
    shr dx, 1                       ; / 2 = free KB (512-byte sectors,
    rcr ax, 1                       ; OSAPI_VOL_STAT's CX on every volume)
    mov [sc_freelo], ax
    mov [sc_freehi], dx
.nostat:
    ; --- SYSTEM\ and STIKAPPS\ in the root, by FIND ----------------------
    xor cx, cx
.root:
    mov di, sc_fbuf
    call OSAPI_FILE_FIND            ; CX = the next ordinal
    jc .rootdone
    cmp word [sc_fbuf+14], OSAPI_FT_DIR
    jne .root
    mov si, sc_fbuf
    mov di, sc_n_system
    call sc_streq
    jne .stik
    mov ax, [sc_fbuf+16]
    mov [sc_csys], ax
    jmp short .root
.stik:
    mov di, sc_n_stik
    call sc_streq
    jne .root
    mov ax, [sc_fbuf+16]
    mov [sc_cstik], ax
    jmp short .root
.rootdone:
    ; --- ASSOC.DAT's program count: the walk's progress estimate ----------
    mov si, sc_n_assoc
    mov bx, sc_buf
    mov cx, SC_BUFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    jc .noasc
    cmp word [sc_buf], 'OS'
    jne .noasc
    mov al, [sc_buf+7]
    xor ah, ah
    mov [sc_nascapp], ax
.noasc:
    or word [sc_dirty], SYD_HEAD | SYD_SYS
    mov word [sc_mp], sc_man
    mov word [sc_mi], 0
    xor ax, ax
    mov dx, (1 << SC_KERN) | (1 << SC_DRV) | (1 << SC_REL)
    mov cl, P_MAN
    jmp sc_nextpass

; =============================================================================
; P_MAN - every manifest file, summed as stored (SPEC.md 118.2)
; sub 0: stand in its folder and start; 1: a chunk; 2: read the whole file
; for its expanded header (a packed driver whose sum did not match)
; =============================================================================
sc_p_man:
    mov si, [sc_mp]
    cmp byte [si], 0
    jne .entry
    mov ax, (1 << SC_KERN) | (1 << SC_DRV)
    mov dx, (1 << SC_LEFT) | (1 << SC_REL)
    mov cl, P_LEFT
    jmp sc_nextpass
.entry:
    cmp byte [sc_sub], 1
    je sc_man_chunk
    ja sc_man_expand
    ; --- sub 0: the folder, the path, a cold cursor --------------------------
    call sc_manpath                 ; sc_cpath, and DX = the folder, CF none
    jc .absent
    call sc_goto
    jc .absent
    xor ax, ax
    mov [sc_s1], ax
    mov [sc_s2], ax
    mov [sc_offlo], ax
    mov [sc_offhi], ax
    mov di, sc_cursor
    call os88_rseq_new
    mov byte [sc_sub], 1
    or word [sc_dirty], SYD_PATH
    ret
.absent:                            ; its folder is not on the volume
    jmp sc_man_missing

; sc_man_chunk - read and sum one chunk; at the end, judge the file
sc_man_chunk:
    mov si, [sc_mp]
    add si, 10                      ; the name
    mov bx, sc_buf
    mov cx, SC_CHUNK
    mov ax, [sc_offlo]
    mov dx, [sc_offhi]
    mov di, sc_cursor
    call os88_rseq                  ; DX:AX = bytes delivered
    jnc .got
    cmp ax, FERR_NOENT
    jne .bad
    mov ax, [sc_offlo]
    or ax, [sc_offhi]
    jz sc_man_missing               ; not there at all
.bad:
    mov si, sc_r_unread             ; there, and it will not read
    mov ax, (SV_BROKEN << 8) | SC_REL
    call sc_manaddf
    jmp sc_man_next
.got:
    or ax, ax
    jz sc_man_judge                 ; the end
    cmp word [sc_offlo], 0
    jne .sum
    cmp word [sc_offhi], 0
    jne .sum
    push ax                         ; the first chunk: keep its first eight
    mov si, sc_buf                  ; bytes, the header as stored
    mov di, sc_hdr
    movsw
    movsw
    movsw
    movsw
    pop ax
.sum:
    add [sc_offlo], ax
    adc word [sc_offhi], 0
    mov cx, ax
    mov si, sc_buf
    mov bx, [sc_s1]
    mov dx, [sc_s2]
    xor ax, ax                      ; AH stays 0: lodsb writes AL alone
.add:
    lodsb
    add bx, ax                      ; s1 += b
    add dx, bx                      ; s2 += s1
    loop .add
    mov [sc_s1], bx
    mov [sc_s2], dx
    call sc_manprog
    ret

; sc_man_judge - the whole file has been summed: is it this release's?
sc_man_judge:
    mov si, [sc_mp]
    mov bx, [sc_mi]
    or byte [sc_mstat+bx], 1        ; present
    mov ax, [sc_offlo]
    cmp ax, [si+2]
    jne .differs
    mov ax, [sc_offhi]
    cmp ax, [si+4]
    jne .differs
    mov ax, [sc_s1]
    cmp ax, [si+6]
    jne .differs
    mov ax, [sc_s2]
    cmp ax, [si+8]
    jne .differs
    or byte [sc_mstat+bx], 2        ; ...and this release's bytes
    cmp byte [si], SCK_KERNEL
    jne .same
    mov byte [sc_kstate], 1
    or word [sc_dirty], SYD_SYS
.same:
    cmp byte [si], SCK_BOOT
    jne sc_man_next
    jmp sc_man_bootshape            ; BOOTHD.SYS: is it a boot sector?
.differs:
    mov al, [si]
    cmp al, SCK_KERNEL
    je .kern
    cmp al, SCK_BOOT
    je .boot
    cmp al, SCK_PKG
    je .pkg
    ; --- a driver or a module: what version is it? --------------------------
    cmp word [sc_hdr], 'O8'
    je sc_man_drvhdr                ; stored plain: the header is right here
    cmp word [sc_hdr], 'CZ'
    jne .drvdiff
    mov byte [sc_sub], 2            ; packed: read it whole, next step
    ret
.drvdiff:
    mov si, sc_r_differs
    mov ax, (SV_WARN << 8) | SC_DRV
    call sc_manaddf
    jmp sc_man_next
.kern:
    mov ax, [sc_offlo]
    or ax, [sc_offhi]
    jnz .kother
    mov si, sc_r_kempty             ; an install died between 52.10.15's
    mov ax, (SV_BROKEN << 8) | SC_KERN ; steps 2 and 5
    call sc_manaddf
    mov byte [sc_kstate], 3
    jmp short .kdone
.kother:
    mov si, sc_r_kother
    mov ax, (SV_WARN << 8) | SC_KERN
    call sc_manaddf
    mov byte [sc_kstate], 2
    mov byte [sc_foreign], 1        ; the release-relative checks soften
.kdone:
    or word [sc_dirty], SYD_SYS
    jmp sc_man_next
.boot:
    mov si, sc_r_bother
    mov ax, (SV_WARN << 8) | SC_KERN
    call sc_manaddf
    jmp sc_man_bootshape
.pkg:
    cmp byte [sc_foreign], 0
    jne sc_man_next                 ; another release: of course it differs
    mov si, sc_r_differs
    mov ax, (SV_WARN << 8) | SC_REL
    call sc_manaddf
    jmp sc_man_next

; sc_man_bootshape - BOOTHD.SYS must be ONE sector ending 55AA (52.10.16)
sc_man_bootshape:
    mov si, [sc_mp]
    add si, 10
    mov di, sc_n_boothd
    call sc_streq
    jne sc_man_next
    cmp word [sc_offhi], 0
    jne .bad
    cmp word [sc_offlo], 512
    jne .bad
    cmp word [sc_buf+510], 0xAA55   ; the one chunk is still in the buffer
    je sc_man_next
.bad:
    mov si, sc_r_notboot
    mov ax, (SV_BROKEN << 8) | SC_KERN
    call sc_manaddf
    jmp sc_man_next

; sc_man_expand - sub 2: a packed driver read whole, for its header
sc_man_expand:
    mov si, [sc_mp]
    add si, 10
    call sc_probe                   ; DX = the KB it expands to
    cmp al, 1
    jne .unknown
    or dx, dx
    jz .unknown
    mov ax, dx
    push ax
    call OSAPI_MEM_CLAIM            ; DX = the segment
    pop ax
    jc .unknown
    push dx                         ; the claim
    push es
    mov es, dx
    mov dx, ax                      ; DX:CX = KB * 1024: the high word is
    mov cl, 6                       ; KB >> 6...
    shr dx, cl
    mov bx, ax
    mov cl, 10
    shl bx, cl                      ; ...and the low word KB << 10
    mov cx, bx
    xor bx, bx                      ; ES:BX = the claim's first byte
    call OSAPI_FILE_READ
    jc .rdfail
    mov ax, [es:0]                  ; the expanded header's first words
    mov [sc_hdr], ax
    mov ax, [es:2]
    mov [sc_hdr+2], ax
    mov ax, [es:4]
    mov [sc_hdr+4], ax
    pop es
    pop dx
    call OSAPI_MEM_FREE
    jmp sc_man_drvhdr
.rdfail:
    pop es
    pop dx
    call OSAPI_MEM_FREE
.unknown:
    mov si, sc_r_vunk               ; it differs, and we cannot say how
    mov ax, (SV_WARN << 8) | SC_DRV
    call sc_manaddf
    jmp sc_man_next

; sc_man_drvhdr - judge a driver or module from its header in sc_hdr
sc_man_drvhdr:
    mov si, [sc_mp]
    cmp word [sc_hdr], 'O8'
    jne .diff
    mov al, [sc_hdr+2]              ; the header's version byte
    cmp byte [si], SCK_MOD
    je .mod
    cmp al, DRV_VER
    je .diff
    mov dx, DRV_VER
    mov bx, sc_r_drvver             ; "built for driver version N, ..."
    jmp short .vers
.mod:
    cmp al, MOD_VER
    je .build
    mov dx, MOD_VER
    mov bx, sc_r_modver
.vers:
    xor ah, ah
    call sc_rsn_nn                  ; sc_rsn = BX's text with AX and DX
    mov si, sc_rsn
    mov ax, (SV_BROKEN << 8) | SC_DRV
    call sc_manaddf
    jmp sc_man_next
.build:
    mov ax, [sc_hdr+4]              ; MOD_H_BUILD: mod_check refuses any
    cmp ax, BUILD_NUM               ; build but its own (SPEC.md 2.8.2)
    je .diff
    mov bx, sc_r_modbld
    xor dx, dx
    call sc_rsn_nn
    mov si, sc_rsn
    mov ax, (SV_BROKEN << 8) | SC_DRV
    call sc_manaddf
    jmp sc_man_next
.diff:
    mov si, sc_r_differs
    mov ax, (SV_WARN << 8) | SC_DRV
    call sc_manaddf
    jmp sc_man_next

; sc_man_missing - a manifest file is not on the volume
sc_man_missing:
    mov si, [sc_mp]
    mov al, [si]
    cmp al, SCK_KERNEL
    jne .notk
    mov si, sc_r_kmiss
    mov ax, (SV_BROKEN << 8) | SC_KERN
    call sc_manaddf
    mov byte [sc_kstate], 3
    or word [sc_dirty], SYD_SYS
    jmp short sc_man_next
.notk:
    cmp al, SCK_BOOT
    jne .rel
    mov si, sc_r_bmiss              ; only an install reads it (52.10.16)
    mov ax, (SV_WARN << 8) | SC_KERN
    call sc_manaddf
    jmp short sc_man_next
.rel:
    mov si, sc_r_missing
    mov ax, (SV_BROKEN << 8) | SC_REL
    cmp byte [sc_foreign], 0        ; another release: it may not ship this
    je .add
    mov ah, SV_WARN
.add:
    call sc_manaddf
    ; ...and falls into sc_man_next

; sc_man_next - on to the next manifest entry
sc_man_next:
    mov si, [sc_mp]
    mov ax, [si+2]                  ; this file's KB, done whatever happened
    mov dx, [si+4]
    mov cx, 1024
    cmp dx, cx
    jae .big
    div cx
    add [sc_mbytes], ax
.big:
    add si, 10
.skip:
    lodsb
    or al, al
    jnz .skip
    mov [sc_mp], si
    inc word [sc_mi]
    mov byte [sc_sub], 0
    mov word [sc_offlo], 0
    mov word [sc_offhi], 0
    ; ...and falls into sc_manprog

; sc_manprog - the bar during P_MAN: 0..400 by KB summed of the manifest's
sc_manprog:
    mov ax, [sc_offlo]
    mov dx, [sc_offhi]
    mov cx, 1024
    cmp dx, cx
    jae .out
    div cx
    add ax, [sc_mbytes]
    mov cx, 400
    mul cx
    mov cx, SC_MANKB
    cmp dx, cx
    jae .out
    div cx
    cmp ax, 400
    jbe .set
    mov ax, 400
.set:
    jmp sc_setprog
.out:
    ret

; sc_manaddf - record a finding about the manifest file being read
; in:  AL = check, AH = severity, SI = the reason; out: nothing, keeps all
sc_manaddf:
    push bx
    push dx
    xor bl, bl
    mov dx, [sc_curfold]
    call sc_addf
    pop dx
    pop bx
    ret

; sc_manpath - sc_cpath for the manifest entry at [sc_mp]; DX = its folder
; out: CF = 1 its folder is not on the volume; clobbers AX, SI, DI
sc_manpath:
    mov si, [sc_mp]
    mov al, [si+1]
    xor dx, dx
    mov di, sc_cpath
    call sc_drive                   ; "C:\"
    cmp al, SCF_SYSTEM
    jne .stik
    mov dx, [sc_csys]
    mov si, sc_n_system
    jmp short .folder
.stik:
    cmp al, SCF_STIK
    jne .file
    mov dx, [sc_cstik]
    mov si, sc_n_stik
.folder:
    call sc_scpy
    mov byte [di], '\'
    inc di
.file:
    mov [sc_curfold], dx
    mov si, [sc_mp]
    add si, 10
    call sc_scpy
    or dx, dx                       ; the root, or a folder that was found
    jnz .ok
    mov si, [sc_mp]
    cmp byte [si+1], SCF_ROOT
    je .ok
    stc
    ret
.ok:
    clc
    ret

; =============================================================================
; P_LEFT - KPAD.TMP in the root, SYSTEM\APPDATA
; =============================================================================
sc_p_left:
    xor dx, dx
    mov [sc_curfold], dx
    call sc_goto
    mov si, sc_n_kpad
    call sc_probe
    cmp al, 1
    jne .appdata
    mov di, sc_cpath
    call sc_drive
    call sc_scpy
    mov si, sc_r_kpad
    mov ax, (SV_WARN << 8) | SC_LEFT
    mov bl, FL_RM
    xor dx, dx
    call sc_addf
.appdata:
    mov dx, [sc_csys]
    or dx, dx
    jz .done                        ; no SYSTEM\: the manifest said so
    call sc_goto
    jc .done
    xor cx, cx
.find:
    mov di, sc_fbuf
    call OSAPI_FILE_FIND
    jc .noapp
    cmp word [sc_fbuf+14], OSAPI_FT_DIR
    jne .find
    mov si, sc_fbuf
    mov di, sc_n_appdata
    call sc_streq
    jne .find
    jmp short .done
.noapp:
    mov di, sc_cpath
    call sc_drive
    mov si, sc_n_system
    call sc_scpy
    mov byte [di], '\'
    inc di
    mov si, sc_n_appdata
    call sc_scpy
    mov si, sc_r_noapp
    mov ax, (SV_WARN << 8) | SC_REL
    xor bl, bl
    mov dx, [sc_csys]
    call sc_addf
.done:
    mov ax, 1 << SC_REL
    mov dx, (1 << SC_RET)
    mov cl, P_RET
    jmp sc_nextpass

; =============================================================================
; P_RET - the retired drivers, by name: a package cannot LIST a hidden file
; (SPEC.md 19.7.1), so the names are the only way to find one (118.2.2)
; =============================================================================
sc_p_ret:
    xor dx, dx
    call sc_goto
    mov si, sc_retired
.next:
    cmp byte [si], 0
    je .done
    push si
    call sc_isdrv                   ; ZF = a .DRV name
    jne .skip
    call sc_probe
    cmp al, 1
    jne .skip
    mov di, sc_cpath
    call sc_drive
    call sc_scpy
    mov si, sc_r_retdrv             ; hidden + system: Clean Up cannot touch it
    mov ax, (SV_WARN << 8) | SC_RET
    xor bl, bl
    xor dx, dx
    call sc_addf
.skip:
    pop si
.nul:
    lodsb
    or al, al
    jnz .nul
    jmp short .next
.done:
    ; --- the walk begins at the root --------------------------------------
    xor ax, ax
    mov [sc_depth], al
    mov [sc_wplen], ax
    mov [sc_wpath], al
    mov [sc_curfold], ax
    mov [sc_wstk+WS_CLUS], ax
    mov [sc_wstk+WS_ORD], ax
    mov [sc_wstk+WS_PLEN], ax
    call sc_foldadd                 ; the root is folder 0
    xor ax, ax
    mov dx, (1 << SC_PKG) | (1 << SC_LEFT) | (1 << SC_RET)
    mov cl, P_WALK
    jmp sc_nextpass

; =============================================================================
; P_WALK - every folder, every package: one FIND a step
; =============================================================================
sc_p_walk:
    mov dx, [sc_curfold]
    call sc_goto
    jnc .stood
    jmp .up                         ; a folder that will not stand: leave it
.stood:
    mov bl, [sc_depth]
    xor bh, bh
    mov ax, WS_SIZE
    mul bx
    mov bx, ax                      ; BX = this level's stack entry
    mov cx, [sc_wstk+bx+WS_ORD]
    mov di, sc_fbuf
    call OSAPI_FILE_FIND
    jc .up                          ; the end of this folder
    mov [sc_wstk+bx+WS_ORD], cx
    mov ax, [sc_fbuf+14]
    cmp ax, OSAPI_FT_DIR
    je .dir
    cmp ax, OSAPI_FT_DIR
    ja .ret                         ; '..' (never listed, SPEC.md 19.7.1)
    inc word [sc_nfiles]
    or word [sc_dirty], SYD_CNT
    mov si, sc_fbuf                 ; BY NAME, not by type: FIND calls a file
    call sc_iso88                   ; a package only when the loader would take
    jne .ret                        ; it (SPEC.md 19.7.1), and the ones it
    inc word [sc_npkg]              ; would not are what this check is for
    call sc_itempath                ; sc_cpath = this package
    or word [sc_dirty], SYD_PATH
    call sc_chkpkg
    call sc_walkprog
.ret:
    ret
.dir:
    inc word [sc_nfold]
    or word [sc_dirty], SYD_CNT
    mov al, [sc_depth]
    inc al
    cmp al, SC_MAXDEP
    jae .ret                        ; deeper than the walk goes: counted only
    mov [sc_depth], al
    add bx, WS_SIZE                 ; the child's entry
    mov ax, [sc_fbuf+16]
    mov [sc_wstk+bx+WS_CLUS], ax
    mov word [sc_wstk+bx+WS_ORD], 0
    mov dx, [sc_wplen]
    mov [sc_wstk+bx+WS_PLEN], dx    ; the parent's path, to put back
    mov [sc_curfold], ax
    mov di, sc_wpath                ; "\NAME" onto the path
    add di, dx
    mov si, sc_fbuf
    mov cx, SC_PATHN - 14
    cmp dx, cx
    jae .long
    mov byte [di], '\'
    inc di
    call sc_scpy
    sub di, sc_wpath
    mov [sc_wplen], di
.long:
    call sc_foldadd
    ret
.up:
    mov al, [sc_depth]
    or al, al
    jz .done
    mov bl, al
    xor bh, bh
    mov ax, WS_SIZE
    mul bx
    mov bx, ax
    mov ax, [sc_wstk+bx+WS_PLEN]    ; the parent's path back
    mov [sc_wplen], ax
    mov si, ax
    mov byte [sc_wpath+si], 0
    dec byte [sc_depth]
    sub bx, WS_SIZE
    mov ax, [sc_wstk+bx+WS_CLUS]
    mov [sc_curfold], ax
    call sc_foldfind                ; sc_curfi for the parent
    ret
.done:
    mov ax, (1 << SC_PKG) | (1 << SC_LEFT) | (1 << SC_RET)
    mov dx, 1 << SC_CFG
    mov cl, P_CFG
    mov word [sc_prog], 850
    jmp sc_nextpass

; sc_walkprog - the bar during the walk: 400..850 by packages found against
; ASSOC.DAT's program count, which is the volume's own count of them
sc_walkprog:
    mov ax, [sc_npkg]
    mov cx, [sc_nascapp]
    jcxz .folders
    cmp ax, cx
    jbe .frac
    mov ax, cx
.frac:
    mov dx, 450
    mul dx
    div cx
    jmp short .set
.folders:                           ; no ASSOC.DAT: by folders, capped
    mov ax, [sc_nfold]
    mov cl, 3
    shl ax, cl
    cmp ax, 440
    jbe .set
    mov ax, 440
.set:
    add ax, 400
    jmp sc_setprog

; sc_chkpkg - one package's header, its name against the retired list, and
;             the duplicate test (SPEC.md 118.1)
sc_chkpkg:
    mov si, sc_fbuf
    mov bx, sc_buf
    mov cx, 512                     ; one sector: the header is clear in a
    xor ax, ax                      ; packed file (SPEC.md 20.13.1)
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jnc .read
    mov si, sc_r_unread
    jmp short .broken
.read:
    cmp ax, 3
    jb .damaged
    cmp word [sc_buf], 'CZ'         ; wrapped whole: the loader's transparent
    je .named                       ; read expands it - nothing to judge here
    cmp word [sc_buf], 'O8'
    jne .damaged
    mov al, [sc_buf+2]
    cmp al, PKG_FMT
    je .named
    xor ah, ah
    mov dx, PKG_FMT
    mov bx, sc_r_pkgfmt             ; "format N, kernel takes 10 - ..."
    call sc_rsn_nn
    mov si, sc_rsn
    jmp short .broken
.damaged:
    mov si, sc_r_damaged
.broken:
    mov ax, (SV_BROKEN << 8) | SC_PKG
    xor bl, bl
    mov dx, [sc_curfold]
    call sc_addf
.named:
    ; --- retired? ----------------------------------------------------------
    mov si, sc_retired
.ret:
    cmp byte [si], 0
    je .dup
    mov di, si
    push si
    mov si, sc_fbuf
    call sc_streq                   ; SI = the name, DI = a retired one
    pop si
    jne .rnext
    mov si, sc_r_retpkg
    mov ax, (SV_WARN << 8) | SC_RET
    mov bl, FL_RM
    mov dx, [sc_curfold]
    call sc_addf
    jmp short .dup
.rnext:
    lodsb
    or al, al
    jnz .rnext
    jmp short .ret
.dup:
    ; --- the same program in another folder? -------------------------------
    mov cx, [sc_nnames]
    mov di, sc_names
    jcxz .add
.look:
    mov si, sc_fbuf
    call sc_streq
    je .found
    add di, NM_SIZE
    loop .look
.add:
    cmp word [sc_nnames], SC_MAXNAME
    jae .out
    mov si, sc_fbuf
    push di
    call sc_scpy
    pop di
    mov al, [sc_curfi]
    mov [di+13], al
    inc word [sc_nnames]
.out:
    ret
.found:
    mov al, [di+13]                 ; the first copy's folder
    cmp al, [sc_curfi]
    je .out
    mov di, sc_rsn                  ; "another copy is in C:\APPS"
    mov si, sc_r_dupin
    call sc_scpy
    cmp al, 0FFh
    je .anon
    mov ah, FO_SIZE
    mul ah
    mov si, ax
    add si, sc_fold + FO_PATH
    push si
    call sc_drive2                  ; "C:"
    pop si
    cmp byte [si], 0
    jne .p
    mov si, sc_r_root               ; the root
.p:
    mov cx, sc_rsn + FR_RSNN - 1    ; the reason's own field bounds it
    sub cx, di
    call sc_scpyc
    jmp short .rec
.anon:
    mov si, sc_r_elsewh
    call sc_scpy
.rec:
    mov si, sc_rsn
    mov ax, (SV_WARN << 8) | SC_LEFT
    xor bl, bl
    mov dx, [sc_curfold]
    call sc_addf
    ret

; sc_foldadd - remember the folder the walk now stands in; sets sc_curfi
sc_foldadd:
    mov ax, [sc_nfound]
    cmp ax, SC_MAXFOLD
    jb .room
    mov word [sc_curfi], 0FFFFh
    ret
.room:
    mov [sc_curfi], ax
    mov cx, FO_SIZE
    mul cx
    mov di, ax
    add di, sc_fold
    mov ax, [sc_curfold]
    mov [di+FO_CLUS], ax
    add di, FO_PATH
    mov si, sc_wpath
    mov cx, FO_SIZE - FO_PATH - 1
.cp:
    lodsb
    stosb
    or al, al
    jz .done
    loop .cp
    mov byte [di], 0
.done:
    inc word [sc_nfound]
    ret

; sc_foldfind - sc_curfi for the folder sc_curfold names (FFFF: not kept)
sc_foldfind:
    mov ax, [sc_curfold]
    call sc_foldof
    mov [sc_curfi], cx
    ret

; sc_foldof - the folder table's index for cluster AX
; out: CX = the index, FFFF = not kept; SI = its path when kept
sc_foldof:
    mov cx, [sc_nfound]
    jcxz .none
    mov si, sc_fold
    xor cx, cx
.look:
    cmp [si+FO_CLUS], ax
    je .hit
    add si, FO_SIZE
    inc cx
    cmp cx, [sc_nfound]
    jb .look
.none:
    mov cx, 0FFFFh
    ret
.hit:
    add si, FO_PATH
    ret

; sc_itempath - sc_cpath = "C:" + the walk's path + "\" + sc_fbuf's name
sc_itempath:
    mov di, sc_cpath
    call sc_drive2
    mov si, sc_wpath
    call sc_scpyn
    mov byte [di], '\'
    inc di
    mov si, sc_fbuf
    jmp sc_scpyn

; =============================================================================
; P_CFG - SYSTEM.CFG (SPEC.md 51.5): its shape, and the drivers its DW record
; wants that are not on the volume
; =============================================================================
sc_p_cfg:
    xor dx, dx
    mov [sc_curfold], dx
    call sc_goto
    mov di, sc_cpath
    call sc_drive
    mov si, sc_n_cfg
    call sc_scpy
    mov si, sc_n_cfg
    mov bx, sc_buf
    mov cx, SC_BUFSZ
    xor dx, dx
    call OSAPI_FILE_READ            ; DX:AX = its size
    jnc .read
    cmp al, FERR_NOENT
    je .done                        ; nothing saved yet: not a finding
    mov si, sc_r_unread
    jmp short .warn
.read:
    or dx, dx
    jnz .shape
    mov cx, ax                      ; CX = the size
    cmp cx, 10
    jb .shape
    mov si, sc_buf
    mov di, sc_cfgsig
    push cx
    mov cx, 8
    repe cmpsb
    pop cx
    jne .shape
    cmp word [sc_buf+8], 3          ; the container generation (SPEC.md 51.5)
    jne .shape
    mov si, sc_buf + 10
    add cx, sc_buf                  ; CX = the end
.rec:
    mov ax, si
    add ax, 2
    cmp ax, cx
    ja .shape                       ; a key past the end
    mov ax, [si]
    or ax, ax
    jz .done                        ; the key of 0 that ends it
    mov dx, si
    add dx, 4
    cmp dx, cx
    ja .shape                       ; a record header past the end
    mov dl, [si+3]                  ; its length
    xor dh, dh
    cmp ax, 'DW'
    jne .skip
    cmp dl, 2
    jne .skip
    mov ax, [si+4]                  ; the driver-wanted bitmap
    push dx                         ; (the record's length)
    call sc_cfgwants
    pop dx
.skip:
    add si, 4
    add si, dx
    cmp si, cx
    jbe .rec
.shape:
    mov si, sc_r_cfgbad
.warn:
    mov ax, (SV_WARN << 8) | SC_CFG
    xor bl, bl
    xor dx, dx
    call sc_addf
.done:
    mov ax, 1 << SC_CFG
    mov dx, 1 << SC_ASC
    mov cl, P_ASSOC
    mov word [sc_prog], 860
    jmp sc_nextpass

; sc_cfgwants - every wanted driver must be on the volume
; in:  AX = DW's bitmap; keeps SI and CX
sc_cfgwants:
    push si
    push cx
    mov cx, ax
    mov bx, sc_cfgdrv               ; the kern_big bit order (drv_cfgbit)
.bit:
    or cx, cx
    jz .out
    shr cx, 1
    jnc .nb
    mov si, [bx]
    or si, si
    jz .nb
    call sc_probe
    cmp al, 0
    jne .nb
    push bx
    push cx
    mov di, sc_rsn                  ; "wants ETHER.DRV, which is missing"
    push si
    mov si, sc_r_wants
    call sc_scpy
    pop si
    call sc_scpy
    mov si, sc_r_wants2
    call sc_scpy
    mov si, sc_rsn
    mov ax, (SV_BROKEN << 8) | SC_CFG
    xor bl, bl
    xor dx, dx
    call sc_addf
    pop cx
    pop bx
.nb:
    inc bx
    inc bx
    cmp bx, sc_cfgdrv + 16
    jb .bit
.out:
    pop cx
    pop si
    ret

; =============================================================================
; P_ASSOC - ASSOC.DAT (SPEC.md 54.7): sub 0 reads it, then a program row a step
; =============================================================================
sc_p_assoc:
    cmp byte [sc_sub], 0
    jne .row
    xor dx, dx
    call sc_goto
    mov di, sc_cpath
    call sc_drive
    mov si, sc_n_assoc
    call sc_scpy
    mov si, sc_n_assoc
    mov bx, sc_buf
    mov cx, SC_BUFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    jnc .read
    mov si, sc_r_ascmiss
    cmp al, FERR_NOENT
    je .bad
    mov si, sc_r_unread
    jmp short .bad
.read:
    mov si, sc_buf
    mov di, sc_ascsig
    mov cx, 6
    repe cmpsb
    jne .old
    cmp byte [sc_buf+6], 2          ; the row version (SPEC.md 54.7)
    jne .old
    mov al, [sc_buf+7]
    cmp al, 55
    ja .old
    xor ah, ah
    mov [sc_nascapp], ax
    mov byte [sc_sub], 1
    ret
.old:
    mov si, sc_r_ascold
.bad:
    mov ax, (SV_WARN << 8) | SC_ASC
    mov bl, FL_ASSOC
    xor dx, dx
    call sc_addf
    jmp .done
.row:
    mov ax, [sc_idx]
    cmp ax, [sc_nascapp]
    jae .done
    mov cx, 88                      ; a program row
    mul cx
    mov si, ax
    add si, sc_buf + 16             ; SI = the row: stem 8, size 2, cluster 2
    mov ax, [si+10]
    mov [sc_curfold], ax
    call sc_stem83                  ; sc_name = "STEM.O88"
    mov di, sc_cpath                ; the path, from the walk's folders
    call sc_drive2
    mov ax, [sc_curfold]
    push di
    call sc_foldof                  ; SI = the folder's path, CX = FFFF none
    pop di
    cmp cx, 0FFFFh
    je .q
    call sc_scpyn
    jmp short .nm
.q:
    mov si, sc_r_qfold
    call sc_scpyn
.nm:
    mov byte [di], '\'
    inc di
    mov si, sc_name
    call sc_scpyn
    mov dx, [sc_curfold]
    call sc_goto
    jc .stale
    mov si, sc_name
    call sc_probe
    cmp al, 0
    jne .next
.stale:
    mov si, sc_r_ascstale
    mov ax, (SV_WARN << 8) | SC_ASC
    mov bl, FL_ASSOC
    mov dx, [sc_curfold]
    call sc_addf
.next:
    inc word [sc_idx]
    mov ax, [sc_idx]                ; the bar: 860..980
    mov dx, 120
    mul dx
    mov cx, [sc_nascapp]
    jcxz .p
    div cx
.p:
    add ax, 860
    jmp sc_setprog
.done:
    mov ax, 1 << SC_ASC
    mov dx, 1 << SC_HIB
    mov cl, P_HIB
    mov word [sc_prog], 980
    jmp sc_nextpass

; sc_stem83 - sc_name = the 8 space-padded stem bytes at SI, then ".O88"
sc_stem83:
    mov di, sc_name
    mov cx, 8
.c:
    lodsb
    cmp al, ' '
    jbe .end
    stosb
    loop .c
.end:
    mov si, sc_x_o88
    jmp sc_scpy

; =============================================================================
; P_HIB - HIBERNAT.PTR and HIBERNAT.IMG (SPEC.md 87.3)
; =============================================================================
sc_p_hib:
    xor dx, dx
    mov [sc_curfold], dx
    call sc_goto
    mov si, sc_n_himg
    call sc_probe
    mov [sc_sub], al                ; the image: 1 = there (a scratch byte -
    mov si, sc_n_hptr               ; this pass is one step)
    mov bx, sc_buf
    mov cx, 512
    xor dx, dx
    call OSAPI_FILE_READ
    jc .noptr
    cmp byte [sc_sub], 1
    jne .orphptr
    cmp word [sc_buf], 'HI'         ; 'HIB1' and this build (SPEC.md 87.3)
    jne .other
    cmp word [sc_buf+2], 'B1'
    jne .other
    cmp word [sc_buf+4], BUILD_NUM
    jne .other
    mov si, sc_n_hptr
    call sc_hpath
    mov si, sc_r_hthis
    mov ax, (SV_NOTE << 8) | SC_HIB
    xor bl, bl
    call sc_hadd
    jmp short .done
.other:
    mov si, sc_n_hptr
    call sc_hpath
    mov si, sc_r_hother
    call sc_hrm
    mov si, sc_n_himg
    call sc_hpath
    mov si, sc_r_hother
    call sc_hrm
    jmp short .done
.orphptr:
    mov si, sc_n_hptr
    call sc_hpath
    mov si, sc_r_hnoimg
    call sc_hrm
    jmp short .done
.noptr:
    cmp byte [sc_sub], 1
    jne .done
    mov si, sc_n_himg
    call sc_hpath
    mov si, sc_r_hnoptr
    call sc_hrm
.done:
    mov word [sc_prog], 1000
    or word [sc_dirty], SYD_BAR
    mov ax, 1 << SC_HIB
    xor dx, dx
    call sc_nextpass
    jmp sc_scan_end

sc_hpath:
    mov di, sc_cpath
    call sc_drive
    jmp sc_scpy
sc_hrm:
    mov ax, (SV_WARN << 8) | SC_HIB
    mov bl, FL_RM
sc_hadd:
    xor dx, dx
    jmp sc_addf

; =============================================================================
; sc_scan_end - the scan is over (finished, stopped, or the volume refused):
;               go home, and say so
; =============================================================================
sc_scan_end:
    mov dx, [sc_homedx]
    mov bl, [sc_homebl]
    call OSAPI_FILE_GOTO_QM
    mov byte [sc_run], 0
    mov byte [sc_ended], 1
    mov word [sc_rrun], 0
    mov word [sc_btlbl], sc_s_scan
    call OSAPI_GET_TICKS
    sub ax, [sc_tstart]
    mov dx, 10                      ; ticks to seconds: * 10 / 182
    mul dx
    mov cx, 182
    cmp dx, cx
    jae .long
    div cx
    jmp short .secs
.long:
    mov ax, 0FFFFh
.secs:
    mov [sc_secs], ax
    cmp byte [sc_seluser], 0        ; the selection: the worst row, unless
    jne .owed                       ; the user chose one
    xor bx, bx
.b:
    cmp word [sc_rbrk+bx], 0
    jne .pick
    inc bx
    inc bx
    cmp bx, 2 * SC_NCHK
    jb .b
    xor bx, bx
.w:
    cmp word [sc_rwrn+bx], 0
    jne .pick
    inc bx
    inc bx
    cmp bx, 2 * SC_NCHK
    jb .w
    jmp short .owed
.pick:
    shr bx, 1
    mov [sc_sel], bl
    mov word [sc_sb+12], 0
.owed:
    mov word [sc_dirty], 0xFFFF     ; heading, counts, buttons, status, list
    mov word [sc_drows], 0xFFFF
    ret

; sc_setprog - the bar's target, AX = 0..1000
sc_setprog:
    cmp ax, [sc_prog]
    je .out
    mov [sc_prog], ax
    or word [sc_dirty], SYD_BAR
.out:
    ret

; -----------------------------------------------------------------------------
; sc_addf - record a finding (SPEC.md 118.3)
; in:  AL = check, AH = severity, BL = FL_* flags, SI = the reason (NUL),
;      DX = the folder it is in; the path is sc_cpath
; out: nothing; every register kept
; -----------------------------------------------------------------------------
sc_addf:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov cl, al
    xor ch, ch
    mov di, cx
    shl di, 1                       ; DI = the check's word index
    cmp ah, SV_BROKEN
    jne .w
    inc word [sc_rbrk+di]
.w:
    cmp ah, SV_WARN
    jne .counted
    inc word [sc_rwrn+di]
.counted:
    test bl, FL_ASSOC
    jz .rows
    mov byte [sc_ascbad], 1
.rows:
    push ax
    mov ax, 1
    shl ax, cl
    or [sc_drows], ax               ; the row's result moved
    pop ax
    or word [sc_dirty], SYD_STAT | SYD_BTN
    cmp word [sc_nf], SC_MAXF
    jb .room
    inc word [sc_flost]
    jmp .out
.room:
    test bl, FL_RM
    jz .keep
    inc word [sc_nrm]
.keep:
    push ax
    mov ax, [sc_nf]
    mov di, FR_SIZE
    push dx
    mul di
    pop dx
    mov di, ax
    add di, sc_ftab
    pop ax
    mov [di+FR_CHK], al
    mov [di+FR_SEV], ah
    mov [di+FR_FLG], bl
    mov [di+FR_CLUS], dx
    push si
    push di
    lea di, [di+FR_RSN]
    mov cx, FR_RSNN - 1
    call sc_scpyc                   ; the reason, cut to its field
    pop di
    pop si
    push di
    lea di, [di+FR_PATH]
    mov si, sc_cpath
    mov cx, FR_PATHN - 1
    call sc_scpyc
    pop di
    inc word [sc_nf]
    ; --- where it shows: the selected check's list, which may follow it ----
    cmp byte [sc_seluser], 0
    jne .shown
    cmp al, [sc_sel]
    je .shown
    mov bl, [sc_sel]                ; the selection follows the scan while the
    xor bh, bh                      ; selected check has nothing to show
    shl bx, 1
    mov dx, [sc_rbrk+bx]
    or dx, [sc_rwrn+bx]
    jnz .shown
    call sc_selcount                ; ...notes included
    or cx, cx
    jnz .shown
    mov cl, al                      ; the new row and the old one redraw
    mov bx, 1
    shl bx, cl
    or [sc_drows], bx
    mov cl, [sc_sel]
    mov bx, 1
    shl bx, cl
    or [sc_drows], bx
    mov [sc_sel], al
    mov word [sc_sb+12], 0
    or word [sc_dirty], SYD_FIND
    jmp short .out
.shown:
    cmp al, [sc_sel]
    jne .out
    call sc_selcount                ; CX = this check's findings, the new
    dec cx                          ; one last
    cmp word [sc_dfind], 0FFFFh
    jne .have
    mov [sc_dfind], cx
.have:
    or word [sc_dirty], SYD_FADD
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_selcount - CX = the findings the selected check has kept
sc_selcount:
    push ax
    push si
    mov al, [sc_sel]
    xor cx, cx
    mov si, sc_ftab
    mov ah, [sc_nf]                 ; (SC_MAXF < 256)
    or ah, ah
    jz .out
.c:
    cmp [si+FR_CHK], al
    jne .n
    inc cx
.n:
    add si, FR_SIZE
    dec ah
    jnz .c
.out:
    pop si
    pop ax
    ret

; sc_fnth - SI = the selected check's finding number AX, CF = 1 none
sc_fnth:
    push ax
    push cx
    mov cl, [sc_sel]
    mov si, sc_ftab
    mov ch, [sc_nf]
    or ch, ch
    jz .none
.c:
    cmp [si+FR_CHK], cl
    jne .n
    or ax, ax
    jz .hit
    dec ax
.n:
    add si, FR_SIZE
    dec ch
    jnz .c
.none:
    stc
    jmp short .out
.hit:
    clc
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; strings and names
; =============================================================================
; sc_scpy - copy the NUL string at SI to DI; DI is left on the NUL
sc_scpy:
    push ax
.c:
    lodsb
    mov [di], al
    or al, al
    jz .e
    inc di
    jmp short .c
.e:
    pop ax
    ret

; sc_scpyn - sc_scpy, but never past sc_cpath's field (a path's guard)
sc_scpyn:
    push cx
    mov cx, sc_cpath + FR_PATHN - 1
    sub cx, di
    jbe .full
    call sc_scpyc
    jmp short .out
.full:
    mov byte [di], 0
.out:
    pop cx
    ret

; sc_scpyc - copy at most CX characters of SI to DI, then a NUL; DI on it
sc_scpyc:
    push ax
    jcxz .e
.c:
    lodsb
    or al, al
    jz .e
    mov [di], al
    inc di
    loop .c
.e:
    mov byte [di], 0
    pop ax
    ret

; sc_streq - ZF = 1 the NUL strings at SI and DI are equal; keeps everything
sc_streq:
    push ax
    push si
    push di
.c:
    mov al, [si]
    cmp al, [di]
    jne .out
    inc si
    inc di
    or al, al
    jnz .c
.out:
    pop di
    pop si
    pop ax
    ret

; sc_iso88 - ZF = 1 the NUL name at SI ends ".O88"; keeps everything
sc_iso88:
    push si
.e:
    cmp byte [si], 0
    je .chk
    inc si
    jmp short .e
.chk:
    cmp word [si-4], '.O'
    jne .out
    cmp word [si-2], '88'
.out:
    pop si
    ret

; sc_isdrv - ZF = 1 the NUL name at SI ends ".DRV"; keeps everything
sc_isdrv:
    push si
.e:
    cmp byte [si], 0
    je .chk
    inc si
    jmp short .e
.chk:
    cmp word [si-4], '.D'
    jne .out
    cmp word [si-2], 'RV'
.out:
    pop si
    ret

; sc_drive - "C:\" at DI; sc_drive2 - "C:" at DI. DI is left after them
sc_drive:
    call sc_drive2
    mov byte [di], '\'
    inc di
    mov byte [di], 0
    ret
sc_drive2:
    push ax
    mov al, [sc_vlet]
    mov ah, ':'
    mov [di], ax
    inc di
    inc di
    mov byte [di], 0
    pop ax
    ret

; sc_rootpath - sc_cpath = "C:\"
sc_rootpath:
    push di
    mov di, sc_cpath
    call sc_drive
    pop di
    ret

; sc_rsn_nn - sc_rsn = the template at BX with '#' replaced by AX and the
;             next '#' by DX (decimal)
sc_rsn_nn:
    push ax
    push bx
    push si
    push di
    mov di, sc_rsn
    mov si, bx
    xchg ax, dx                     ; DX = the first number, AX the second
    push ax
.c:
    lodsb
    cmp al, '#'
    je .num
    mov [di], al
    or al, al
    jz .done
    inc di
    jmp short .c
.num:
    mov ax, dx
    call dec_u16                    ; DI on the NUL
    pop dx                          ; the second number for the next '#'
    push dx
    jmp short .c
.done:
    pop ax
    pop di
    pop si
    pop bx
    pop ax
    ret

sc_n_system:  db 'SYSTEM', 0
sc_n_stik:    db 'STIKAPPS', 0
sc_n_appdata: db 'APPDATA', 0
sc_n_assoc:   db 'ASSOC.DAT', 0
sc_n_cfg:     db 'SYSTEM.CFG', 0
sc_n_kpad:    db 'KPAD.TMP', 0
sc_n_boothd:  db 'BOOTHD.SYS', 0
sc_n_hptr:    db 'HIBERNAT.PTR', 0
sc_n_himg:    db 'HIBERNAT.IMG', 0
sc_x_o88:     db '.O88', 0
sc_cfgsig:    db 'O88CFG', 0, 0
sc_ascsig:    db 'OS88AC'

; SYSTEM.CFG's DW bits on kern_big (kernel/driver.inc, drv_cfgbit)
sc_cfgdrv:  dw sc_d_sound, sc_d_hdd, sc_d_ram, sc_d_net
            dw sc_d_ether, sc_d_vmm, sc_d_usbm, sc_d_ems
sc_d_sound: db 'SOUND.DRV', 0
sc_d_hdd:   db 'HDD.DRV', 0
sc_d_ram:   db 'RAMDISK.DRV', 0
sc_d_net:   db 'NET.DRV', 0
sc_d_ether: db 'ETHER.DRV', 0
sc_d_vmm:   db 'VMMOUSE.DRV', 0
sc_d_usbm:  db 'USBMOUSE.DRV', 0
sc_d_ems:   db 'EMS.DRV', 0

; the reasons (SPEC.md 118.1) - each fits FR_RSNN - 1 = 45 characters
sc_r_novol:    db 'the volume will not read', 0
sc_r_unread:   db 'there, but it will not read', 0
sc_r_kmiss:    db 'missing - this volume cannot boot', 0
sc_r_kempty:   db 'empty - an install stopped part way', 0
sc_r_kother:   db 'not this release', 39, 's - install again', 0
sc_r_bmiss:    db 'missing - only an install reads it', 0
sc_r_bother:   db 'not this release', 39, 's boot sector', 0
sc_r_notboot:  db 'not one sector ending 55AA', 0
sc_r_differs:  db 'differs from this release', 0
sc_r_missing:  db 'missing - install again', 0
sc_r_drvver:   db 'driver version #, kernel takes #', 0
sc_r_modver:   db 'module version #, kernel takes #', 0
sc_r_modbld:   db 'from build # - the kernel refuses it', 0
sc_r_vunk:     db 'differs; its version is unknown', 0
sc_r_kpad:     db 'left by an install that stopped', 0
sc_r_noapp:    db 'missing - programs keep settings there', 0
sc_r_retdrv:   db 'retired - install with Erase', 0
sc_r_retpkg:   db 'retired - Clean Up removes it', 0
sc_r_pkgfmt:   db 'format #, kernel takes # - will not load', 0
sc_r_damaged:  db 'damaged header - not a package', 0
sc_r_dupin:    db 'another copy is in ', 0
sc_r_elsewh:   db 'another folder', 0
sc_r_cfgbad:   db 'not a settings file this kernel reads', 0
sc_r_wants:    db 'wants ', 0
sc_r_wants2:   db ', which is missing', 0
sc_r_ascmiss:  db 'missing - Clean Up rebuilds it', 0
sc_r_ascold:   db 'old format - Clean Up rebuilds it', 0
sc_r_ascstale: db 'the program is not there', 0
sc_r_qfold:    db '\?', 0
sc_r_root:     db '\', 0
sc_r_hthis:    db 'a saved session of this build', 0
sc_r_hother:   db 'another build', 39, 's - discarded at start', 0
sc_r_hnoimg:   db 'no image beside it', 0
sc_r_hnoptr:   db 'no pointer - nothing resumes it', 0
