; =============================================================================
; os8088 - files.inc
;
; The Disk window - a minimal file manager for the FAT data floppy
; (SPEC.md 22). FOUR windows may be open at once (KIND_FILES, cap 4), each
; on its own drive and its own folder. No boot-time window; app_launch
; creates one on demand (fm_kinit is the KD_INIT), and CMD_FILES dispatch /
; desk_click reach it through files_open / files_open_drive, which front an
; existing window on the target, or create one, or - at the cap - move the
; frontmost one there.
;
; THE RULE THE WHOLE MODULE RESTS ON (SPEC.md 22): paints read the window's
; cache; actions re-sync the global snapshot first. disk_dir / disk_icons /
; disk_nfiles stay exactly ONE global mount snapshot, and are still the only
; thing loader.inc and diskw.inc ever resolve an index against. Every window
; additionally owns a byte-for-byte copy of a snapshot in its VIEW_SEG slot
; (SPEC.md 2.3/22.1) - that is what W_PAINT, fm_layout and the hit-tester
; read, so a repaint, a drag, a raise or a wm_paint_all costs ZERO floppy
; I/O. Anything that acts calls fmv_sync first, which remounts only when the
; acting window's (drive, cwd) is not the one currently mounted.
;
; Per-window state is a 16-byte KD_POOL block (fm_pool, 4 x 16) that
; app_launch allocates and hands to fm_kinit in DI. fm_vp_set publishes the
; acting window's block, cache segment and instance record into [fm_vp] /
; [fm_vseg] / [fm_vinst], and fm_layout mirrors the fields the 400-line
; painter reads on its hot path - it uses every register including BP and
; has none free to thread a pointer through.
;
; The window is RESIZABLE (KD_WFLAG = WF_SIZABLE, the one built-in that is)
; and the directory SCROLLS, in either of two views: a list (16px rows:
; icon + name + size) or an icon grid (78x40 cells). Nothing may bake in
; the template's 320x200: fm_layout derives every coordinate both the
; painter and the hit-tester use from the live window record, in one
; place, so they can never disagree (SPEC.md 22 "Live layout").
;
; Opening an entry - a double-click, Enter, or File > Open - goes through
; ONE routine, fm_open_sel, which branches on the SPEC.md 19 type word: a
; package posts index+1 to [ld_pending] AND its own state block to
; [ld_pwin] (ui.inc runs loader_run after the lock drops, and loader_run
; syncs to that window before resolving the index - the index means nothing
; anywhere else); a FOLDER re-lists this window and nothing else - listing,
; cache, title, selection, scroll.
;
; The window also owns the menu bar. fm_menus (below) is Locator's SECOND
; SPEC.md 12.2 menu set: same AM_NAME, so the bar still reads 'Locator',
; but a real AM_ONCMD - fm_oncmd - so File/Folder/View/Special are this
; window's commands rather than the desktop's. fm_oncmd runs under the gfx
; lock exactly like W_ONCLICK, which is why three of its commands are
; POSTED (inst_launch_post / ui_post_cmd) instead of called: see the jump
; table's per-entry comments, and SPEC.md 22 for the binding list.
;
; A RIGHT-press in the content pops a context menu at the pointer instead
; (fm_rclick, SPEC.md 12.4). Three static descriptors - file row, folder
; row, empty space - feed the SAME FMC_* ids through the SAME fm_jmp table,
; so no command has a second implementation and none can drift into
; disagreeing with itself about which side of the lock it is on. That
; matters most where there is no bar at all: a WF_FULL window (SPEC.md
; 11.2) has no menu bar, and the popup plus the keyboard are all it has.
;
; The three commands that need an answer - New Folder..., Rename... and
; Delete - ask for it in the status line rather than in a dialog (there is
; no dialog kind and no focus concept): FS_EDIT turns W_ONKEY into an input
; line that SWALLOWS EVERY KEY - binding, because otherwise the first letter
; of "BAK" would switch to drive B: - and the two naming modes filter each
; character through diskw.inc's own dskw_char, so the line shows exactly the
; bytes that will be stored.
;
; Delete's line is a CONFIRMATION, not an editor, and it is the only thing
; standing between a menu click and a file that is gone: this system has no
; undo and no trash. So it names the file, and Enter is the only key that
; means yes - every other one, and every click, means no.
;
; The paint/onkey/onclick procs and fm_oncmd all run with the gfx lock
; already held by the caller (SPEC.md 11/12.2) and never lock, block or
; spawn; files_open / files_open_drive / files_refresh take the lock
; themselves.
; =============================================================================

; --- layout constants (content-relative, SPEC.md 22) -------------------------
FM_ROW_H    equ 16              ; list row height, px
FM_ROW_Y0   equ 22              ; row area top (both views)
FM_DBLCLK   equ 9               ; double-click window, ticks
FM_SB_W     equ 14              ; scroll bar width
FMI_CELL_W  equ 78              ; icon-view cell
FMI_CELL_H  equ 40

; Everything else - buttons, status line, rows that fit, columns - comes
; from fm_layout at run time (the window resizes).

; --- per-window state block (KD_POOL, 4 x 16 bytes; SPEC.md 22/29.3) ---------
; One block per open window, allocated and cascaded by app_launch, handed to
; fm_kinit in DI, and reached everywhere else through [fm_vp]. FS_CLKT moves
; with FS_SEL or not at all: shared, a click on row 3 in one window followed
; within FM_DBLCLK ticks by a click on row 3 in ANOTHER would compose into a
; double-click and launch a package.
FS_SEL      equ 0               ; word: selected DIRECTORY index (not a row),
                                ; 0xFFFF = none
FS_SCRL     equ 2               ; word: first visible row
FS_CLKT     equ 4               ; word: birth tick of this window's last click
FS_N        equ 6               ; word: entries in this window's cache, 0..32
FS_CWD      equ 8               ; word: the folder it shows (first cluster,
                                ; 0 = the root)
FS_DRV      equ 10              ; byte: the drive it shows
FS_MOK      equ 11              ; byte: 1 = that listing came from a good mount
FS_VIEW     equ 12              ; byte: 0 = list, 1 = icon grid
FS_IDX      equ 13              ; byte: its VIEW_SEG slot, 0..3 - derived once
                                ; by fm_kinit and never again
FS_EDIT     equ 14              ; byte: the status-line editor is armed here
FS_FERR     equ 15              ; byte: FERR_* of the last file operation IN
                                ; THIS WINDOW, 0 = none
FS_SIZE     equ 16              ; = the Disk kind's KD_SSIZE

FM_MAXWIN   equ VIEW_SLOTS      ; = KD_CAP: one cache slot per window
FV_ICONS    equ 1024            ; icon images start here inside a slot (2.3)
FM_HDRMAX   equ 48              ; fm_hdrbuf's size: the header and the status
                                ; line are both staged whole and truncated
                                ; afterwards, and the longest of them is the
                                ; delete confirmation (SPEC.md 22)

; -----------------------------------------------------------------------------
; files_init - reset module state (no window is created at boot, SPEC.md 22)
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; .bss is not zeroed by the loader: every state variable is set here.
; -----------------------------------------------------------------------------
files_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor al, al                  ; every block reads as a window that never
    mov di, fm_pool             ; existed until fm_kinit fills one
    mov cx, FM_MAXWIN * FS_SIZE
    cld
    rep stosb
    mov byte [fm_elen], 0       ; nothing is being typed anywhere, nothing is
    mov byte [fm_ebuf], 0       ; awaiting a confirmation, and no window has
    mov byte [fm_onam], 0       ; asked how much room is left
    mov byte [fm_odir], 0
    mov word [fm_msgwin], 0
    mov byte [fm_msgbuf], 0
    mov byte [fm_full], 0
    mov word [fm_vp], fm_pool   ; no acting window yet: point the three
    mov word [fm_vseg], VIEW_SEG    ; published words at slot 0 so a stray
    mov word [fm_vinst], 0          ; read cannot leave the pool or the cache
    mov byte [fm_seed_drv], 1   ; the seed the first launch would consume
    mov word [fm_seed_cwd], 0
    mov word [fm_seed_nam], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_kinit - KD_INIT for the Disk kind (SPEC.md 29.3): a whole new window
; in:  BX = window ptr, DI = state ptr, SI = instance record
; out: nothing (all registers preserved)
;
; The title is aimed at the INSTANCE RECORD's I_NAME rather than at the
; template's literal, which is what makes the window's caption, its dock
; tile (SPEC.md 30) and its Task Manager row (SPEC.md 28) all follow the
; folder from one 16-byte write. app_launch has already copied KD_NAME
; ('Disk') in there, so the caption is never blank even if the mount fails.
;
; This is also where the new window's listing comes from: app_launch has no
; argument channel, so the caller parked the target in fm_seed_* (the
; [cp_sel]-before-KIND_CTRL precedent, SPEC.md 31) and fmv_load pulls it in
; here. KD_INIT runs on the UI task with NO lock held, which is exactly the
; context a mount needs.
; -----------------------------------------------------------------------------
fm_kinit:
    push ax
    push cx
    push dx
    push si

    mov ax, di                  ; the cache slot IS the pool slot: derive it
    sub ax, fm_pool             ; once, here, so nothing else has to know how
    mov cl, 4                   ; app_launch picked the block
    shr ax, cl
    mov [di+FS_IDX], al

    mov word [di+FS_SEL], 0xFFFF    ; a fresh window has no selection,
    mov word [di+FS_CLKT], 0        ; nothing listed, list view, scrolled to
    mov word [di+FS_SCRL], 0        ; the top, and no half-typed name - a
    mov word [di+FS_N], 0           ; reopened window never resumes one
    mov byte [di+FS_VIEW], 0
    mov byte [di+FS_EDIT], 0
    mov byte [di+FS_FERR], 0

    mov ax, si
    add ax, I_NAME              ; the record's 16-byte name field, which
    mov [bx + W_TITLE], ax      ; inst_set_name zero-filled and NUL-capped
    mov word [bx + W_MENUS], fm_menus
                                ; the Disk browser IS Locator's window
                                ; (SPEC.md 12.3): fronting it keeps Locator
                                ; in the bar rather than swapping it for an
                                ; app named 'Disk' - it just swaps Locator's
                                ; desktop menus for its file-manager ones

    call fm_vp_set              ; BX = the window: make it the acting one, so
                                ; fm_settitle below finds its record
    mov al, [fm_seed_drv]
    mov dx, [fm_seed_cwd]
    call fmv_load               ; DI = block: mount, then fill its own cache
    mov si, [fm_seed_nam]       ; the folder's name when the caller knew it
    call fm_settitle
    mov byte [fm_full], 0       ; app_launch's wm_show repaints everything
                                ; anyway - do not make the first self-repaint
                                ; pay for a second full pass

    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; The view cache (SPEC.md 2.3/22.1)
;
; Each window owns one 4KB VIEW_SEG slot holding a byte-for-byte image of
; disk_dir (at slot offset 0) and disk_icons (at FV_ICONS). Neither end of
; that copy is addressable through DS - the source is LOW_SEG, the
; destination VIEW_SEG - which is why it goes through disk.inc's
; dsk_copy_out / dsk_copy_seg rather than a plain rep movsw.
; =============================================================================

; -----------------------------------------------------------------------------
; fm_vp_set - publish the acting window's view
; in:  BX = window ptr
; out: nothing (all registers preserved); [fm_vp] = its state block,
;      [fm_vseg] = its cache segment, [fm_vinst] = its instance record (0 if
;      the window is unowned)
;
; Everything in this module that reads per-window state reads it through
; [fm_vp], and every listing read goes through [fm_vseg]. Both are published
; HERE and nowhere else - the same "computed in one place so they can never
; disagree" rule fm_layout already lives by. A painter or hit-tester that
; skips this reads the PREVIOUS window's directory: wrong names, wrong icons,
; and a double-click that opens the wrong file.
; -----------------------------------------------------------------------------
fm_vp_set:
    push bx
    push di
    call inst_win_owner         ; DI = record, or 0; BX preserved
    mov [fm_vinst], di
    or di, di
    jz .none
    mov di, [di+I_SPTR]         ; the kind's KD_POOL block...
    cmp di, fm_pool             ; ...which for a file-manager window is one
    jb .none                    ; of ours. Bounded rather than trusted: every
    cmp di, fm_pool + FM_MAXWIN*FS_SIZE     ; write below goes through it.
    jb .have
.none:
    mov di, fm_pool             ; cannot happen - app_launch binds the window
                                ; before it runs KD_INIT - but slot 0 keeps
                                ; every access below inside the pool if it does
.have:
    mov [fm_vp], di
    mov bl, [di+FS_IDX]
    and bl, VIEW_SLOTS-1        ; whatever that byte holds, the segment must
    add bl, VIEW_SEG >> 8       ; land inside the block SPEC.md 2.3 pins
    mov bh, bl
    xor bl, bl
    mov [fm_vseg], bx
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; fmv_copy_in - CX bytes from the acting window's cache into kernel scratch
; in:  SI = slot offset, DI = kernel offset, CX = byte count (even)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fmv_copy_in:
    push ax
    push dx
    mov dx, [fm_vseg]
    mov ax, cs                  ; this code is .text, so CS is the kernel
    call dsk_copy_seg
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_get_dir - stage one entry out of THIS window's cache
; in:  AX = entry index (0..31; callers bound it by FS_N)
; out: SI -> dsk_ent, holding that entry's 32 bytes
; clobbers: SI (the output), flags
;
; Deliberately lands in disk.inc's OWN dsk_ent rather than a second buffer:
; every consumer in this module keeps the plain "SI ->" pointer it had back
; when the listing was a single global.
; -----------------------------------------------------------------------------
fmv_get_dir:
    push ax
    push cx
    push di
    mov cl, 5                   ; index * 32
    shl ax, cl
    mov si, ax
    mov di, dsk_ent
    mov cx, DSK_DE_SIZE
    call fmv_copy_in
    pop di
    pop cx
    pop ax
    mov si, dsk_ent
    ret

; -----------------------------------------------------------------------------
; fmv_get_icon - stage one icon slot out of THIS window's cache
; in:  AX = entry index (0..31)
; out: SI -> dsk_ico, holding 64 bytes (all-zero = no icon)
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
fmv_get_icon:
    push ax
    push cx
    push di
    mov cl, 6                   ; index * 64, past the entry images
    shl ax, cl
    add ax, FV_ICONS
    mov si, ax
    mov di, dsk_ico
    mov cx, DSK_ICO_SIZE
    call fmv_copy_in
    pop di
    pop cx
    pop ax
    mov si, dsk_ico
    ret

; -----------------------------------------------------------------------------
; fmv_store - copy the global mount snapshot into one window's slot
; in:  DI = state block
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fmv_store:
    push ax
    push cx
    push si
    push di
    mov al, [di+FS_IDX]
    and al, VIEW_SLOTS-1
    add al, VIEW_SEG >> 8
    mov ah, al
    xor al, al                  ; AX = this window's slot segment
    mov si, disk_dir            ; both ends are LOW_SEG offsets (SPEC.md 2.1)
    xor di, di
    mov cx, 1024
    call dsk_copy_out
    mov si, disk_icons
    mov di, FV_ICONS
    mov cx, 2048
    call dsk_copy_out
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_load - mount a directory and fill ONE window's cache (SPEC.md 22.1)
; in:  AL = drive, DX = the directory's first cluster (0 = root),
;      DI = state block. Real floppy I/O; UI task only.
; out: nothing (all registers preserved)
;
; The global snapshot is rebuilt by the one validated pipeline (dsk_chdir ->
; disk_mount) and then copied into the window's slot. What lands in the block
; afterwards is what the mount ACTUALLY produced, not what was asked for: a
; failed mount leaves the volume at the root with the write gate shut
; (SPEC.md 19.2), and a window claiming to be inside a folder it never
; entered is a lie the user could act on.
; -----------------------------------------------------------------------------
fmv_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bl, al                  ; dsk_chdir wants the drive in DL and the
    mov ax, dx                  ; cluster in AX
    mov dl, bl
    call dsk_chdir              ; a remount with [dsk_keepcwd] raised

    mov al, [disk_drive]        ; ...and now record what really happened
    mov [di+FS_DRV], al
    mov ax, [dsk_cwd]
    mov [di+FS_CWD], ax
    mov byte [di+FS_MOK], 0
    cmp byte [dsk_mntok], 0     ; the write gate IS the mount's verdict, and
    je .listed                  ; unlike CF it survives the stores above
    mov byte [di+FS_MOK], 1
.listed:
    mov ax, [disk_nfiles]
    mov [di+FS_N], ax
    mov word [di+FS_SEL], 0xFFFF    ; every index into the old listing is
    mov word [di+FS_SCRL], 0        ; meaningless now
    mov byte [di+FS_FERR], 0
    call fmv_store

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_sync - make the global snapshot BE this window's directory (SPEC.md 22.1)
; in:  DI = state block
; out: nothing (all registers preserved)
;
; The other half of "paints read the cache, actions re-sync". Everything that
; resolves against disk_dir / disk_nfiles / [dsk_cwd] - the loader, every
; dskw_*, dsk_dotdot - has to run against the ACTING window's directory, not
; against whichever window happened to navigate last. In the common case (you
; act in the window you last moved) this is a compare and a ret; when it is
; not, it costs exactly the mount that operation would have needed anyway.
;
; The selection and the scroll position are banked across the reload, which
; fmv_load itself would clear. A sync is NOT navigation - the window is being
; re-listed at exactly the place it already was - and clearing them would
; mean that double-clicking a package in a scrolled background window
; silently scrolled it back to the top. Both are bounds-checked at every use
; (FS_SEL against FS_N, FS_SCRL by fm_clamp_scroll), so restoring an index
; that a changed directory has invalidated is harmless.
; -----------------------------------------------------------------------------
fmv_sync:
    push ax
    push bx
    push dx
    mov al, [di+FS_DRV]
    cmp al, [disk_drive]
    jne .load
    mov dx, [di+FS_CWD]
    cmp dx, [dsk_cwd]
    je .out
.load:
    mov al, [di+FS_DRV]
    mov dx, [di+FS_CWD]
    mov bx, [di+FS_SEL]
    push bx
    mov bx, [di+FS_SCRL]
    push bx
    call fmv_load
    pop bx
    mov [di+FS_SCRL], bx
    pop bx
    mov [di+FS_SEL], bx

    ; A sync is meant to re-list the window exactly where it already was, so
    ; the caption normally needs nothing - and MUST get nothing, because we
    ; have no name to give it here and fm_settitle with SI = 0 would demote
    ; a perfectly good 'TOOLS' to 'Folder'. There is one case where the
    ; window did move: the folder was deleted under us, and the mount fell
    ; back to the root (SPEC.md 19.2). fmv_load records that in FS_CWD, so
    ; comparing it against where we asked to be is exactly the test - and
    ; then the root titles itself, which is the one caption fm_settitle can
    ; always name without help. Every OTHER path that changes FS_CWD is
    ; navigation and retitles itself with a real name.
    cmp dx, [di+FS_CWD]
    je .out
    push si
    xor si, si
    call fm_settitle
    pop si
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_bcast - push the current snapshot into every window showing it (22.1)
; in:  nothing; the globals must ALREADY be the new truth - every dskw_*
;      remounts on success (SPEC.md 18.4)
; out: nothing (all registers preserved)
;
; The acting window is included on purpose: its own cache is a listing taken
; before the write, so without this the folder it just created would not
; appear in the window that created it. Pure memory, no extra floppy I/O -
; which is what makes fixing the sibling case affordable at all.
; -----------------------------------------------------------------------------
fmv_bcast:
    push ax
    push cx
    push dx
    push si
    push di
    mov si, inst_tab
    mov cx, INST_MAX
.scan:
    cmp byte [si+I_STATE], 1
    jne .next
    cmp byte [si+I_KIND], KIND_FILES
    jne .next
    mov di, [si+I_SPTR]
    mov al, [di+FS_DRV]
    cmp al, [disk_drive]
    jne .next
    mov dx, [di+FS_CWD]
    cmp dx, [dsk_cwd]
    jne .next
    mov ax, [disk_nfiles]
    mov [di+FS_N], ax
    mov byte [di+FS_MOK], 1     ; we only get here after a successful write,
                                ; which ended in a successful remount (18.4)
    mov word [di+FS_SEL], 0xFFFF    ; the listing was rebuilt: indices into
    mov word [di+FS_SCRL], 0        ; the old one mean nothing
    call fmv_store
.next:
    add si, I_RECSZ
    loop .scan
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
fm_tpl:     dw 110, 80, 320, 200, fm_ttl, fm_paint, fm_onkey, fm_onclick
fm_ttl:     db 'Disk', 0

fm_s_drive:  db 'Drive ', 0     ; header: "Drive " letter ":  " N " files"
fm_s_colsp:  db ':  ', 0
fm_s_files:  db ' files', 0
fm_s_nodisk: db 'No os8088 disk (', 0 ; + letter + ':)' - 19 chars, short
                                ; enough to clear the buttons (SPEC.md 22)
fm_s_refresh: db 'Refresh', 0   ; button label, 7 chars = 56 px
fm_s_icons:   db 'Icons', 0     ; view-toggle labels: the view a click
fm_s_list:    db 'List', 0      ; switches TO (SPEC.md 22)

fm_s_loading: db 'Loading...', 0
fm_s_empty:   db 0              ; status line strings, indexed by [ld_status]
fm_s_dskerr:  db 'Disk error', 0
fm_s_badpkg:  db 'Bad package', 0
fm_s_toobig:  db 'Too large', 0
fm_s_ldfail:  db 'Load failed', 0
fm_s_nomem:   db 'Out of memory', 0
fm_s_needs:   db 'Needs ', 0    ; + the KB figure + 'K': status 6, LD_ENEED
fm_s_kaych:   db 'K', 0         ; (SPEC.md 21/22) - built live, see .st_need
fm_stattab:   dw fm_s_empty, fm_s_dskerr, fm_s_badpkg, fm_s_toobig, fm_s_ldfail
              dw fm_s_nomem

; --- FERR_* -> status line (SPEC.md 22) --------------------------------------
; Every dskw_* failure has to say something. Without this table a refused
; mkdir - name taken, disk full, write-protected media - looks exactly like
; a successful one, and for operations that touch the user's disk that is
; the worst possible outcome. Indexed by the FERR_* code; slot 0 is never
; drawn (fm_ferr = 0 means "nothing to report") and only exists so the
; index is the code. 'Disk error' is shared with the loader's table above.
fm_s_enodisk: db 'No disk', 0
fm_s_ename:   db 'Bad name', 0
fm_s_enoent:  db 'No such file', 0
fm_s_eexist:  db 'Name exists', 0
fm_s_efull:   db 'Disk full', 0
fm_s_edfull:  db 'Folder full', 0
fm_s_eprot:   db 'Protected', 0
fm_s_ewprot:  db 'Write protected', 0
fm_errtab:    dw fm_s_empty,  fm_s_enodisk, fm_s_dskerr, fm_s_ename
              dw fm_s_enoent, fm_s_eexist,  fm_s_efull,  fm_s_edfull
              dw fm_s_eprot,  fm_s_ewprot,  fm_s_toobig
FM_NERR       equ 11            ; FERR_OK .. FERR_BIG
FM_FREE       equ 255           ; the one FS_FERR value that is not a FERR_*:
                                ; "draw fm_msgbuf", the free-space figure.
                                ; It rides in FS_FERR rather than in a field
                                ; of its own because it wants exactly that
                                ; field's lifetime - cleared by fmv_load, by
                                ; fm_edit_arm and by the next verdict.

; --- the status-line input prompts (SPEC.md 22) ------------------------------
; The delete line is the ONLY thing between a menu click and a file that is
; gone, so it names the file, states the key that says yes and states the key
; that says no. Worst case it is 'Delete ' + 12 + 18 = 37 characters, which
; fits the default window's 38-char status line whole and truncates from the
; right - which is why the name and the word Enter come before Esc.
fm_s_newfld:  db 'New folder: ', 0
fm_s_renam:   db 'Rename ', 0
fm_s_rento:   db ' to: ', 0
fm_s_delpr:   db 'Delete ', 0
fm_s_delq:    db '? Enter=yes Esc=no', 0
fm_s_kbfree:  db ' KB free', 0      ; Folder > Free Space
fm_s_root:    db 'Disk', 0      ; window title at the root...
fm_s_folder:  db 'Folder', 0    ; ...and in a folder we arrived at from
                                ; below, whose name would cost a second
                                ; mount to learn (SPEC.md 22)

; -----------------------------------------------------------------------------
; fm_menus - Locator's file-manager menu set (SPEC.md 12.2/12.3/22)
;
; AM_NAME is menu_loc_name, the SAME 'Locator' string the desktop's set
; uses: one application, two menu sets, so the bar's identity never flickers
; as windows come and go. AM_ONCMD is a real handler, which makes this the
; first KERNEL kind on ui_dispatch's .app route - see the note there.
;
; The four titles are laid out at x 110/154/214/258 and end at 325, against
; menu_relayout's MENU_CLK_HX limit of 434: 108px of slack, so no title can
; be silently dropped. Item strings do not enter that arithmetic at all.
; -----------------------------------------------------------------------------
fm_s_mfile:   db 'File', 0
fm_s_mfold:   db 'Folder', 0
fm_s_mview:   db 'View', 0
fm_s_mspec:   db 'Special', 0

fm_s_open:    db 'Open', 0
fm_s_mkdir:   db 'New Folder...', 0
fm_s_rename:  db 'Rename...', 0
fm_s_delete:  db 'Delete', 0
fm_s_closew:  db 'Close Window', 0
fm_s_refr:    db 'Refresh', 0
fm_s_up:      db 'Up One Folder', 0
fm_s_rootf:   db 'Root Folder', 0
fm_s_drva:    db 'Drive A:', 0
fm_s_drvb:    db 'Drive B:', 0
fm_s_dfree:   db 'Free Space', 0
fm_s_aslist:  db 'as List', 0
fm_s_asicon:  db 'as Icons', 0
fm_s_newwin:  db 'New Window', 0
fm_s_opennew: db 'Open in New Window', 0

fm_items_file:
    dw fm_s_open, fm_s_mkdir, fm_s_rename, fm_s_delete, fm_s_closew
fm_items_fold:
    dw fm_s_newwin, fm_s_opennew
    dw fm_s_refr, fm_s_up, fm_s_rootf, fm_s_drva, fm_s_drvb, fm_s_dfree
fm_items_view:
    dw fm_s_aslist, fm_s_asicon
fm_items_spec:
    dw menu_s_clock, menu_s_bounce, menu_s_restart

fm_menus:
    dw menu_loc_name            ; AM_NAME: still 'Locator' (SPEC.md 12.3)
    dw fm_oncmd                 ; AM_ONCMD: our own handler
    dw 4                        ; AM_COUNT (= MENU_APPMAX, used exactly)
    dw fm_s_mfile, fm_items_file, 5
    dw fm_s_mfold, fm_items_fold, 8
    dw fm_s_mview, fm_items_view, 2
    dw fm_s_mspec, fm_items_spec, 3

; -----------------------------------------------------------------------------
; files_open - CMD_FILES dispatch entry (ui.inc); no lock held on entry
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; Opens where the volume already is - ([disk_drive], [dsk_cwd]) - and lets
; fm_choose decide whether that means fronting a window, creating one, or
; moving one (SPEC.md 22.1).
; -----------------------------------------------------------------------------
files_open:
    push ax
    push dx
    mov al, [disk_drive]
    mov dx, [dsk_cwd]
    jmp short fm_open_body      ; files_open_drive's body; pops the two above

; -----------------------------------------------------------------------------
; files_open_drive - show a drive's top level in a file-manager window
; in:  AL = drive (0 = A:, 1 = B:); no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
; Callers: CMD_FILES dispatch (via files_open) and desk_click (SPEC.md 26).
;
; A drive icon always means that drive's ROOT. It no longer mounts
; unconditionally: an existing window already showing that root is fronted
; instead, which is the "one window per place" rule the Finder has - and
; Refresh, one click away, is still the disk-swap answer it always was.
; -----------------------------------------------------------------------------
files_open_drive:
    push ax
    push dx
    xor dx, dx
fm_open_body:
    push bx
    push cx
    push si
    push di
    xor si, si                  ; no name known: the title derives (SPEC 22)
    call fm_choose
    pop di
    pop si
    pop cx
    pop bx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_choose - front, create, or move a window onto a directory (SPEC.md 22.1)
; in:  AL = drive, DX = first cluster (0 = root), SI = a NUL display name or
;      0 to derive one; NO gfx lock held, UI task
; out: nothing; clobbers AX, BX, CX, SI, DI (the callers save them)
;
; Three outcomes and no failure path: already open there, so front it; room
; for another, so seed and launch; or at the cap, in which case the
; frontmost file-manager window goes there instead. A dialog saying "too
; many windows" would be the only alternative and it would be useless -
; there is exactly one thing the user could do about it.
; -----------------------------------------------------------------------------
fm_choose:
    call fm_find_view           ; already showing (AL, DX)?
    jc .notopen
    call gfx_lock               ; front + un-minimize, exactly as app_launch's
    and byte [di+I_FLAGS], 0xFE ; own at-cap path does (SPEC.md 29.4)
    mov bx, [di+I_WIN]
    call wm_show
    call gfx_unlock
    ret
.notopen:
    call fm_count               ; CX = live file-manager instances
    cmp cx, FM_MAXWIN
    jae .inplace
    call fm_seed                ; park the target for fm_kinit
    mov al, KIND_FILES
    jmp app_launch              ; tail call; it takes its own locks
.inplace:
    call gfx_lock
    call fm_front_win           ; BX = the frontmost visible one
    jc .unlock                  ; every window minimized: nothing to move
    call fm_vp_set
    mov di, [fm_vp]
    call fmv_load               ; AL = drive, DX = cwd, DI = block
    call fm_settitle            ; SI = the name, still untouched
    mov si, bx
    call fm_repaint
.unlock:
    call gfx_unlock
    ret

; -----------------------------------------------------------------------------
; fm_find_view - a live window already showing a directory
; in:  AL = drive, DX = first cluster
; out: CF = 0 and DI = its INSTANCE record; CF = 1 = none. AX/BX/CX/DX/SI
;      preserved.
; -----------------------------------------------------------------------------
fm_find_view:
    push ax
    push cx
    push si
    mov si, inst_tab
    mov cx, INST_MAX
.scan:
    cmp byte [si+I_STATE], 1
    jne .next
    cmp byte [si+I_KIND], KIND_FILES
    jne .next
    mov di, [si+I_SPTR]
    cmp al, [di+FS_DRV]
    jne .next
    cmp dx, [di+FS_CWD]
    jne .next
    mov di, si                  ; the record, not the block: the caller wants
    clc                         ; I_WIN and I_FLAGS
    jmp short .out
.next:
    add si, I_RECSZ
    loop .scan
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_count - file-manager instances in use
; in:  nothing
; out: CX = the count; all other registers preserved
; Counts I_STATE != 0, dying instances included, because that is exactly what
; app_launch's cap check counts: seeding a launch that then fronts an
; existing window would silently drop the seed.
; -----------------------------------------------------------------------------
fm_count:
    push dx
    push si
    xor cx, cx
    mov si, inst_tab
    mov dx, INST_MAX
.scan:
    cmp byte [si+I_STATE], 0
    je .next
    cmp byte [si+I_KIND], KIND_FILES
    jne .next
    inc cx
.next:
    add si, I_RECSZ
    dec dx
    jnz .scan
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; fm_is_fmwin - is BX one of Locator's file-manager windows? (SPEC.md 13/22)
; in:  BX = window ptr
; out: CF = 0 yes, CF = 1 no. Preserves everything else.
;
; The W_MENUS = fm_menus identity test, made OWNER-AWARE - binding since v3
; (SPEC.md 13) at THIS routine's two callers and anywhere else the question
; is ever asked. fm_kinit is the only KERNEL code that ever stores
; fm_menus, so the word compare used to be exact on its own; but a v3
; package's W_MENUS is an org-0 offset in its OWN segment (SPEC.md 12.2)
; and can equal fm_menus by numeric coincidence, and such a window would be
; misclassified as Locator's and handed fm_rclick / fm_oncmd, which
; dereference FS_* state it does not have. A package owner (I_KIND bit 7)
; refuses the match - and so does an UNOWNED window whose wm_wseg creator
; stamp is not KERNEL_SEG (SPEC.md 11): since packages can wm_create
; through the API, "unowned" alone no longer proves "not a package's".
; -----------------------------------------------------------------------------
fm_is_fmwin:
    push di
    cmp word [bx+W_MENUS], fm_menus
    jne .no
    call inst_win_owner         ; DI = owning record, 0 = unowned
    or di, di
    jz .unowned
    test byte [di+I_KIND], KIND_PKG
    jnz .no
.yes:
    clc
    jmp short .out
.unowned:                       ; no fm window is unowned - but a package's
    push ax                     ; extra window can be (SPEC.md 11): only a
    call inst_cb_seg            ; KERNEL-created one passes. AX = the
    cmp ax, KERNEL_SEG          ; creator's segment (DI = 0, BX = window)
    pop ax                      ; POP touches no flags: the compare holds
    je .yes
.no:
    stc
.out:
    pop di                      ; POP touches no flags: CF survives
    ret

; -----------------------------------------------------------------------------
; fm_front_win - the frontmost VISIBLE file-manager window
; in:  nothing
; out: CF = 0 and BX = window ptr; CF = 1 = none. All else preserved.
;
; "Is this a file-manager window" is fm_is_fmwin's owner-aware W_MENUS
; test, not an instance walk (see above).
; -----------------------------------------------------------------------------
fm_front_win:
    push ax
    push cx
    push si
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    add si, cx
    jcxz .none
.next:
    dec si                      ; front to back (SPEC.md 11)
    mov al, [si]
    call wm_idx2ptr             ; BX = window ptr
    test word [bx+W_FLAGS], 2   ; visible?
    jz .cont
    call fm_is_fmwin
    jnc .found
.cont:
    loop .next
.none:
    stc
    jmp short .out
.found:
    clc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_seed - park a target directory for the next fm_kinit (SPEC.md 22.1)
; in:  AL = drive, DX = first cluster, SI = a NUL display name or 0
; out: nothing (all registers preserved)
;
; app_launch has no argument channel, so the target rides in .bss between the
; request and the KD_INIT that consumes it. The NAME is COPIED rather than
; pointed at: the only caller that has one (Open in New Window) posts its
; launch from under the gfx lock and then repaints, and that repaint reuses
; fm_name for every row it draws.
; -----------------------------------------------------------------------------
fm_seed:
    push ax
    push cx
    push si
    push di
    mov [fm_seed_drv], al
    mov [fm_seed_cwd], dx
    mov word [fm_seed_nam], 0
    or si, si
    jz .out
    mov di, fm_snam
    mov [fm_seed_nam], di
    mov cx, 16
.cp:
    lodsb                       ; DF=0 per SPEC.md 1
    mov [di], al
    inc di
    or al, al
    jz .out
    loop .cp
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; files_refresh - repaint after a load attempt (loader_run)
; in:  nothing; no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
;
; A bare wm_paint_all under its own lock. It used to look up "the" Disk
; instance with inst_find_kind and skip when that window was hidden; with
; four possible windows "the first one" is the wrong window, and the call was
; never about our window anyway - loader_run makes it right after wm_show
; raised the LOADED program's window, which may overlap any of them, so the
; repaint has to be the full back-to-front pass regardless.
; -----------------------------------------------------------------------------
files_refresh:
    call gfx_lock
    call wm_paint_all
    call gfx_unlock
    ret

; -----------------------------------------------------------------------------
; fm_layout - derive every layout value from the live window record
; in:  BX = window ptr
; out: nothing (all registers preserved); fills the fm_* layout scratch
;      below and clamps fm_scroll into range (SPEC.md 22 "Live layout"):
;        fm_cx/fm_cy    content origin (absolute)
;        fm_cw/fm_ch    content width/height
;        fm_rgt         content right column, absolute, inclusive
;        fm_staty       status line text top, content-relative
;        fm_listb       first row BELOW the row area, content-relative
;        fm_fit         whole rows that fit (0 = row area omitted)
;        fm_cols        grid columns (1 in list view)
;        fm_total       total rows (entries, or grid rows)
;        fm_nch         list-view name chars that fit
;
; It ALSO publishes the acting window (fm_vp_set) and mirrors the eight
; per-window fields the painter and the hit-tester read on their hot paths
; into fm_l* scratch. That is not a convenience: fm_draw_core uses AX, BX,
; CX, DX, SI, DI and BP - BP is the row-band cursor - and has no register
; left to carry a state-block pointer through 400 lines. The mirrors are
; read-only snapshots refreshed here; fm_lscr is the one that is written,
; and fm_clamp_scroll is the one place it is written BACK.
; -----------------------------------------------------------------------------
fm_layout:
    push ax
    push cx
    push dx
    push di

    call fm_vp_set              ; BX = window ptr -> [fm_vp]/[fm_vseg]
    mov di, [fm_vp]
    mov al, [di+FS_VIEW]
    mov [fm_lview], al
    mov al, [di+FS_MOK]
    mov [fm_lmok], al
    mov al, [di+FS_DRV]
    mov [fm_ldrv], al
    mov al, [di+FS_EDIT]
    mov [fm_ledit], al
    mov al, [di+FS_FERR]
    cmp al, FM_FREE             ; the free-space figure lives in ONE module
    jne .lferr                  ; buffer: only the window that asked for it
    mov cx, [fm_msgwin]         ; may draw it, or a second window would show
    cmp cx, di                  ; the first one's disk
    je .lferr
    xor al, al
.lferr:
    mov [fm_lferr], al
    mov ax, [di+FS_N]
    mov [fm_lnf], ax
    mov ax, [di+FS_SCRL]
    mov [fm_lscr], ax
    mov ax, [di+FS_SEL]
    mov [fm_lsel], ax

    call wm_content             ; AX = content left, DX = content top
    mov [fm_cx], ax
    mov [fm_cy], dx
    mov ax, [bx+W_W]
    sub ax, 2
    mov [fm_cw], ax             ; cw = W_W - 2
    add ax, [fm_cx]
    dec ax
    mov [fm_rgt], ax            ; content right, absolute inclusive
    mov ax, [bx+W_H]
    sub ax, TITLE_H+1
    mov [fm_ch], ax             ; ch = W_H - TITLE_H - 1
    sub ax, 17
    mov [fm_staty], ax          ; status line pinned above the bottom
    sub ax, 2
    mov [fm_listb], ax          ; row area ends here

    ; fit = (listb - 22) / row_h, 0 when the window is too short
    sub ax, FM_ROW_Y0
    jg .roompos
    xor ax, ax
    jmp .fit
.roompos:
    mov cx, FM_ROW_H
    cmp byte [fm_lview], 0
    je .fitdiv
    mov cx, FMI_CELL_H
.fitdiv:
    xor dx, dx
    div cx
.fit:
    mov [fm_fit], ax

    ; cols = 1 (list) or max(1, (cw-16)/78) (icons)
    mov ax, 1
    cmp byte [fm_lview], 0
    je .cols
    mov ax, [fm_cw]
    sub ax, 16
    jle .cols1
    mov cx, FMI_CELL_W
    xor dx, dx
    div cx
    or ax, ax
    jnz .cols
.cols1:
    mov ax, 1
.cols:
    mov [fm_cols], ax

    ; total rows = nfiles (list) or ceil(nfiles / cols) (icons)
    mov ax, [fm_lnf]
    cmp byte [fm_lview], 0
    je .tot
    add ax, [fm_cols]
    dec ax
    xor dx, dx
    div word [fm_cols]
.tot:
    mov [fm_total], ax

    ; list-view name budget: (cw - 88)/8 chars, clamped 0..16 (the name
    ; column must stop short of the size column, SPEC.md 22)
    mov ax, [fm_cw]
    sub ax, 88
    jg .nchpos
    xor ax, ax
    jmp .nchclamp
.nchpos:
    mov cl, 3
    shr ax, cl
.nchclamp:
    cmp ax, 16
    jbe .nch
    mov ax, 16
.nch:
    mov [fm_nch], ax

    call fm_clamp_scroll        ; a shrink-resize self-heals here

    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_clamp_scroll - clamp fm_lscr into [0, max(total - fit, 0)], then commit
; in:  fm_total/fm_fit valid (fm_layout ran)
; out: nothing (all registers preserved)
; Signed on purpose: click paths subtract a page first and clamp after.
;
; This is the ONE write-back of the scroll mirror into the window's own state
; block, and it is affordable to make it the only one because every scroll
; path already funnels through here - the arrows, the track pages, the four
; scan codes, and the shrink-resize self-heal above.
; -----------------------------------------------------------------------------
fm_clamp_scroll:
    push ax
    push di
    mov ax, [fm_total]
    sub ax, [fm_fit]
    jns .max
    xor ax, ax
.max:
    cmp [fm_lscr], ax
    jle .low
    mov [fm_lscr], ax
.low:
    cmp word [fm_lscr], 0
    jge .done
    mov word [fm_lscr], 0
.done:
    mov di, [fm_vp]
    mov ax, [fm_lscr]
    mov [di+FS_SCRL], ax
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_thumb - scroll-bar thumb geometry (shared by painter and hit-tester)
; in:  fm_* layout valid
; out: CF=1 no thumb (everything fits, or the track is degenerate); else
;      CF=0, AX = thumb top (absolute y), DX = thumb height
; clobbers: nothing else
; Track = content-relative rows 33 .. listb-12 (between the arrow rules),
; height listb-44. thumb h = max(8, fit*track/total), top rides fm_scroll.
; -----------------------------------------------------------------------------
fm_thumb:
    push bx
    push cx
    mov ax, [fm_total]
    cmp ax, [fm_fit]
    jbe .none
    mov bx, [fm_listb]
    sub bx, 44                  ; BX = track height
    cmp bx, 8                   ; shorter than the minimum thumb: bare
    jl .none                    ; track (SPEC.md 22 degenerate exception)
    mov ax, [fm_fit]
    mul bx
    div word [fm_total]         ; AX = proportional thumb height
    cmp ax, 8
    jge .hok
    mov ax, 8
.hok:
    mov cx, ax                  ; CX = thumb height
    mov ax, [fm_lscr]
    mul bx
    div word [fm_total]         ; AX = thumb offset in the track
    add ax, FM_ROW_Y0+11        ; track top, content-relative (= 33)
    mov dx, bx                  ; clamp: top <= track top + track - height
    add dx, FM_ROW_Y0+11
    sub dx, cx
    cmp ax, dx
    jle .tok
    mov ax, dx
.tok:
    add ax, [fm_cy]             ; absolute
    mov dx, cx
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fm_paint - W_PAINT for the Disk window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
fm_paint:
    push bx
    mov bx, si
    call fm_draw_core
    pop bx
    ret

; -----------------------------------------------------------------------------
; fm_onclick - W_ONCLICK for the Disk window
; in:  CX = x, DX = y (absolute), SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; Test order (SPEC.md 22): buttons -> scroll bar -> rows. Buttons are
; tested only when wide enough to be drawn (the same cw guards as the
; painter - nothing invisible is ever clickable). FS_SEL holds a DIRECTORY
; index; the row math adds the scroll position and, in icon view, the column.
;
; A click anywhere cancels an in-place name (SPEC.md 22), on the reasoning
; that clicking away from a rename is how every desktop abandons one - and
; because leaving the editor armed would swallow the next keystroke too. It
; cancels one armed in ANOTHER window too: the buffer is shared, so a name
; half-typed somewhere else has nowhere left to land.
; -----------------------------------------------------------------------------
fm_onclick:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, si                  ; publish the clicked window BEFORE anything
    call fm_vp_set              ; reads or writes per-window state

    call fm_edit_end            ; clicking away abandons a half-typed name

    mov bx, si
    push cx                     ; save click x
    mov cx, dx                  ; CX = click y
    call wm_content             ; AX = content left, DX = content top
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x
    call fm_layout              ; BX = window ptr

    ; --- Refresh button (drawn iff cw >= 76) ---
    mov ax, [fm_cw]
    cmp ax, 76
    jl .viewbtn
    sub ax, 68                  ; x1 = cw-68 .. x2 = cw-6
    cmp dx, ax
    jl .viewbtn
    add ax, 62
    cmp dx, ax
    jg .viewbtn
    cmp cx, 2
    jl .viewbtn
    cmp cx, 15
    jg .viewbtn
    mov dl, [fm_ldrv]           ; remount THIS window's drive in place
    call fm_mount               ; (under the gfx lock: stalls painters
    jmp .redraw                 ;  briefly; accepted by SPEC.md 22)

    ; --- view toggle (drawn iff cw >= 142) ---
.viewbtn:
    mov ax, [fm_cw]
    cmp ax, 142
    jl .sbar
    sub ax, 136                 ; x1 = cw-136 .. x2 = cw-74
    cmp dx, ax
    jl .sbar
    add ax, 62
    cmp dx, ax
    jg .sbar
    cmp cx, 2
    jl .sbar
    cmp cx, 15
    jg .sbar
    mov di, [fm_vp]             ; flip the view, back to the top; the
    xor byte [di+FS_VIEW], 1    ; selection is kept (SPEC.md 22). The mirrors
    mov word [di+FS_SCRL], 0    ; are re-read by the repaint's own fm_layout
    jmp .redraw

    ; --- scroll bar (present iff the row area is) ---
.sbar:
    cmp word [fm_fit], 0
    je .rows
    mov ax, [fm_cw]
    sub ax, FM_SB_W
    cmp dx, ax
    jl .rows                    ; left of the bar
    cmp cx, FM_ROW_Y0
    jl .rows                    ; above it
    cmp cx, [fm_listb]
    jge .rows                   ; below it
    cmp cx, FM_ROW_Y0+10        ; up-arrow cell, rows 22..32
    jle .sb_up
    mov ax, [fm_listb]
    sub ax, 11                  ; down-arrow cell, rows listb-11..listb-1
    cmp cx, ax
    jge .sb_down
    call fm_thumb               ; track: page against the thumb top
    jc .redraw                  ; no thumb: the track is inert
    sub ax, [fm_cy]             ; thumb top, content-relative
    cmp cx, ax
    jl .sb_pgup
    mov ax, [fm_fit]            ; at/below the thumb top: page down
    add [fm_lscr], ax
    jmp .sb_clamp
.sb_pgup:
    mov ax, [fm_fit]
    sub [fm_lscr], ax
    jmp .sb_clamp
.sb_up:
    dec word [fm_lscr]
    jmp .sb_clamp
.sb_down:
    inc word [fm_lscr]
.sb_clamp:
    call fm_clamp_scroll
    jmp .redraw

    ; --- the row area ---
.rows:
    call fm_hit                 ; AX = directory index, CF = 1 = not a row
    jc .clear

    mov bx, [fm_vp]             ; BX = the state block from here on, AX the
                                ; directory index
    cmp ax, [bx+FS_SEL]         ; second click on the selected entry?
    jne .select
    mov dx, [ui_click_t]        ; click birth tick (EV_C), not [ticks]:
    sub dx, [bx+FS_CLKT]        ; wrap-safe (SPEC.md 8); queued clicks
                                ; must not collapse into double-clicks.
                                ; PER WINDOW, or a click on row 3 here and
                                ; one on row 3 next door would compose
    cmp dx, FM_DBLCLK
    jae .select
    call fm_open_sel            ; double-click: a package goes to the
    jmp .redraw                 ; loader, a folder re-lists this window (22)

.select:
    mov [bx+FS_SEL], ax
    mov dx, [ui_click_t]
    mov [bx+FS_CLKT], dx
    jmp .redraw

.clear:
    mov bx, [fm_vp]
    mov word [bx+FS_SEL], 0xFFFF

.redraw:
    call fm_repaint             ; SI still = window ptr

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_hit - which directory entry is a content point in? (SPEC.md 22)
; in:  CX = content-relative y, DX = content-relative x; fm_layout already
;      run for the acting window
; out: CF = 0 and AX = directory index; CF = 1 = not on an entry (the header
;      band, past the last shown row, or - in icon view - right of the last
;      column)
; clobbers: AX, CX, DX, DI
;
; Extracted from fm_onclick so the left-click ladder and the context menu
; (SPEC.md 12.4/22) cannot disagree about which row a point is in - the same
; "computed in one place" argument fm_layout already won for the painter and
; the hit-tester. It reads the layout mirrors, so it is only meaningful
; between an fm_layout and the next one.
; -----------------------------------------------------------------------------
fm_hit:
    mov di, dx                  ; DI = content-relative x
    mov ax, cx
    sub ax, FM_ROW_Y0           ; rebase y to row 0
    js .none                    ; the header band is not a row
    mov cx, FM_ROW_H
    cmp byte [fm_lview], 0
    je .rowdiv
    mov cx, FMI_CELL_H
.rowdiv:
    xor dx, dx
    div cx                      ; AX = visible row
    cmp ax, [fm_fit]
    jae .none                   ; below the row area
    cmp byte [fm_lview], 0
    je .listidx
    push ax                     ; icon view: column from x
    mov ax, di
    mov cx, FMI_CELL_W
    xor dx, dx
    div cx
    mov di, ax                  ; DI = column
    pop ax
    cmp di, [fm_cols]
    jae .none                   ; right of the last column
    add ax, [fm_lscr]
    mul word [fm_cols]
    add ax, di                  ; AX = directory index
    jmp short .chk
.listidx:
    add ax, [fm_lscr]           ; AX = directory index
.chk:
    cmp ax, [fm_lnf]
    jae .none                   ; past THIS window's listing
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; fm_onkey - W_ONKEY for the Disk window
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; While this window's FS_EDIT is set the handler SWALLOWS EVERY KEY and none
; of the shortcuts below apply (SPEC.md 22). That is binding, not defensive:
; with the bare-letter shortcuts live, typing a folder named 'BAK' would
; switch to drive B: on its very first character and the name would be
; half-eaten.
;
; Otherwise: 'a'/'A' mounts drive 0, 'b'/'B' drive 1, 'r'/'R' remounts THIS
; window's drive (disk_mount under the gfx lock stalls painters briefly;
; accepted by SPEC.md 22). 'v'/'V' toggles the view. Up/Down/PgUp/PgDn (scan
; 48h/50h/49h/51h) scroll the view. Enter opens the selection like a
; double-click. Only the front window is sent keys at all (SPEC.md 13), so
; there is no focus concept to get wrong.
; -----------------------------------------------------------------------------
fm_onkey:
    push ax
    push bx
    push dx
    push di

    mov bx, si                  ; publish the window this key belongs to
    call fm_vp_set
    mov di, [fm_vp]

    cmp byte [di+FS_EDIT], 0
    jne .editing                ; naming: nothing below is reachable

    cmp al, 13
    je .enter
    cmp al, 'v'
    je .vtoggle
    cmp al, 'V'
    je .vtoggle
    cmp al, 8                   ; Backspace goes up, the way it does in every
    je .updir                   ; file manager since (SPEC.md 22)
    cmp al, 'n'
    je .newfld
    cmp al, 'N'
    je .newfld
    mov dl, 0
    cmp al, 'a'
    je .mount
    cmp al, 'A'
    je .mount
    mov dl, 1
    cmp al, 'b'
    je .mount
    cmp al, 'B'
    je .mount
    cmp al, 'r'
    je .remount
    cmp al, 'R'
    je .remount
    cmp ah, 0x48                ; Up
    je .scr_up
    cmp ah, 0x50                ; Down
    je .scr_dn
    cmp ah, 0x49                ; PgUp
    je .scr_pgup
    cmp ah, 0x51                ; PgDn
    je .scr_pgdn
    jmp .out

.editing:
    or al, al
    jz .out                     ; a bare scan code: swallowed, nothing moved
    call fm_editkey
    jmp .redraw

.remount:
    mov dl, [di+FS_DRV]         ; ours, not whichever drive is mounted

.mount:
    call fm_mount               ; DL = drive; the same body the Refresh
    jmp .redraw                 ; button and the Folder menu use

.vtoggle:
    xor byte [di+FS_VIEW], 1
    mov word [di+FS_SCRL], 0
    jmp .redraw

.updir:
    push si                     ; the menu body clobbers SI, which .redraw
    call fm_c_up                ; still needs as the window pointer
    pop si
    jmp .redraw

.newfld:
    mov al, 1                   ; DI is still this window's block
    call fm_edit_arm
    jmp .redraw

.scr_up:
    mov bx, si                  ; the scroll keys clamp against the live
    call fm_layout              ; layout (fm_fit/fm_total) and move the
    dec word [fm_lscr]          ; mirror; fm_clamp_scroll commits it
    jmp .scr_clamp
.scr_dn:
    mov bx, si
    call fm_layout
    inc word [fm_lscr]
    jmp .scr_clamp
.scr_pgup:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
    sub [fm_lscr], ax
    jmp .scr_clamp
.scr_pgdn:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
    add [fm_lscr], ax
.scr_clamp:
    call fm_clamp_scroll
    jmp .redraw

.enter:
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]           ; 0xFFFF (none) also fails this
    jae .out
    call fm_open_sel            ; same as a double-click

.redraw:
    call fm_repaint             ; SI still = window ptr

.out:
    pop di
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Navigation and the shared open path (SPEC.md 19.2/22)
; =============================================================================

; -----------------------------------------------------------------------------
; fm_open_sel - open one directory entry: THE open path (SPEC.md 22)
; in:  AX = index into THIS window's listing, already checked against FS_N;
;      gfx lock held, [fm_vp] published
; out: nothing (all registers preserved)
;
; Shared by the double-click, by Enter and by File > Open, so those three can
; never drift apart. The SPEC.md 19 type word decides: 2 is a folder and
; re-lists this window; anything else goes to the loader, which rejects a
; type-0 file as "Bad package" - the truthful verdict for double-clicking a
; data file.
;
; Before this branch existed a folder double-click reached ld_run_body, which
; rejects type != 1, and the window reported "Bad package" for an operation
; that had nothing to do with packages.
;
; The loader gets the poster's state block in [ld_pwin] as well as the index,
; and that pairing is binding: the index means nothing except inside this
; window's listing, and a background window's listing is a real listing it is
; entitled to act on. Without it a click here would load whichever file sits
; at that index in whatever happens to be mounted - and it would pass every
; SPEC.md 21 validation on the way.
;
; Both the cluster and the name are taken out of the staged entry BEFORE
; anything acts: fm_go remounts, and a mount rebuilds dsk_ent - the very
; buffer fmv_get_dir just handed back a pointer into.
; -----------------------------------------------------------------------------
fm_open_sel:
    push ax
    push bx
    push si
    push di
    mov di, [fm_vp]
    call fmv_get_dir            ; SI -> the staged 32-byte entry
    mov bx, [si+18]             ; its first cluster, banked before the mount
    cmp word [si+16], 2
    je .folder
    inc ax                      ; index + 1: ui.inc runs the loader once the
    mov [ld_pending], ax        ; lock drops (SPEC.md 21)
    mov [ld_pwin], di           ; ...resolved against THIS window (22.1)
    jmp short .out
.folder:
    call fm_stage_name          ; fm_name = the folder's display name, out
    mov ax, bx                  ; of the staged entry while it still exists
    mov si, fm_name
    call fm_go
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_go - move THIS window to a directory (SPEC.md 19.2/22)
; in:  AX = the directory's first cluster (0 = the root), SI = a NUL display
;      name to title the window with, or 0 = derive it; gfx lock held,
;      [fm_vp] published
; out: nothing (all registers preserved)
;
; Navigation is per window and nothing else moves: the drive comes out of the
; block, and fmv_load re-lists the volume, refills THIS window's cache and
; resets its selection, scroll and error line. Another window sitting in the
; folder we just left keeps painting its own cache, untouched.
; -----------------------------------------------------------------------------
fm_go:
    push ax
    push dx
    push di
    mov dx, ax
    mov di, [fm_vp]
    mov al, [di+FS_DRV]
    call fmv_load
    call fm_settitle            ; SI is still the caller's name (or 0)
    pop di
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_mount - point THIS window at a drive's root, from scratch (SPEC.md 22)
; in:  DL = drive (0 = A:, 1 = B:); gfx lock held, [fm_vp] published
; out: nothing (all registers preserved)
;
; The body behind the Refresh button, the a/b/r keys and the Folder menu's
; Refresh / Drive A: / Drive B: - one copy, so a drive change can never mean
; three slightly different things. Unlike fm_go this is NOT navigation: it
; lands at the ROOT deliberately, because a swapped disk has no meaningful
; "current folder".
; -----------------------------------------------------------------------------
fm_mount:
    push ax
    push dx
    push si
    push di
    mov al, dl
    xor dx, dx                  ; the root
    mov di, [fm_vp]
    call fmv_load
    xor si, si                  ; ...which titles itself
    call fm_settitle
    pop di
    pop si
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_settitle - point the window's caption at where the volume now is
; in:  SI = a NUL display name, or 0
; out: nothing (all registers preserved)
;
; The window's own FS_CWD is the authority, not SI. A failed mount leaves it
; at the root (SPEC.md 19.2), and titling the window with the folder we
; failed to ENTER would be a lie the user could act on - so the root always
; reads 'Disk'. Inside a folder SI names it when the caller knows the name,
; and 'Folder' when it does not, which is only the Up case: naming that
; directory would need the GRANDparent's listing, i.e. a second whole mount
; to display a string (SPEC.md 22).
;
; The caption, the dock tile and the Task Manager row are the same 16 bytes
; (the instance's I_NAME, aimed at by fm_kinit), so all three follow.
; -----------------------------------------------------------------------------
fm_settitle:
    push ax
    push cx
    push si
    push di
    mov di, [fm_vinst]
    or di, di
    jz .out                     ; unowned window: nothing to retitle
    add di, I_NAME              ; DI -> the 16 bytes the caption points at
    push di
    mov di, [fm_vp]
    cmp word [di+FS_CWD], 0
    pop di                      ; pop leaves the flags alone
    jne .named
    mov si, fm_s_root
    jmp short .copy
.named:
    or si, si
    jnz .copy
    mov si, fm_s_folder
.copy:
    mov byte [fm_full], 1       ; the caption lives in the FRAME, which a
                                ; content repaint never touches: ask the
                                ; next fm_repaint for the whole screen
    mov cx, 15                  ; I_NAME is 16 bytes with a permanent NUL
.ch:                            ; at byte 15 (SPEC.md 29.1)
    lodsb                       ; DF=0 per SPEC.md 1
    mov [di], al
    inc di
    or al, al
    jz .out
    loop .ch
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The menu bar: fm_menus' command handler (SPEC.md 12.2/22)
; =============================================================================

; --- command ids -------------------------------------------------------------
; Contiguous WITHIN a menu, which is what makes fm_oncmd's base + item
; arithmetic work. They are internal constants with no on-disk or cross-
; module meaning, so a menu that gains an item simply renumbers what follows.
FMC_OPEN    equ 0               ; File
FMC_MKDIR   equ 1
FMC_RENAME  equ 2
FMC_DELETE  equ 3
FMC_CLOSEW  equ 4
FMC_NEWWIN  equ 5               ; Folder
FMC_OPENNEW equ 6
FMC_REFRESH equ 7
FMC_UP      equ 8
FMC_ROOT    equ 9
FMC_DRVA    equ 10
FMC_DRVB    equ 11
FMC_FREE    equ 12
FMC_LIST    equ 13              ; View
FMC_ICONS   equ 14
FMC_CLOCK   equ 15              ; Special
FMC_BOUNCE  equ 16
FMC_RESTART equ 17
FMC_COUNT   equ 18

FM_NMENUS   equ 4               ; = fm_menus' AM_COUNT = MENU_APPMAX
fm_menu_base:
    db FMC_OPEN, FMC_NEWWIN, FMC_LIST, FMC_CLOCK

; The jump table. Every entry records WHICH SIDE OF THE GFX LOCK it is on,
; because fm_oncmd runs under that lock and the lock is a non-recursive spin
; only the UI task releases (SPEC.md 7): an entry that called app_launch,
; files_open, files_refresh or ui_cmd would hang the machine permanently -
; no beep, no watchdog, no recovery. This is the single easiest mistake to
; make in this module, which is why the note is per line and not per file.
fm_jmp:
    dw fm_c_open                ; inline (the loader is deferred; chdir is
                                ;         disk I/O under the lock, like
                                ;         Refresh already is)
    dw fm_c_mkdir               ; inline (arms the status-line editor)
    dw fm_c_rename              ; inline (arms it; the disk is touched at
                                ;         Enter, not here)
    dw fm_c_delete              ; inline (arms the CONFIRM line; likewise)
    dw fm_c_closew              ; POSTED - ui_cmd takes the lock
    dw fm_c_newwin              ; POSTED - app_launch takes the lock (29.4)
    dw fm_c_opennew             ; POSTED - ditto
    dw fm_c_refresh             ; inline (fmv_load, like the button)
    dw fm_c_up                  ; inline (fmv_sync + dsk_dotdot + fmv_load)
    dw fm_c_root                ; inline (fmv_load at the root)
    dw fm_c_drva                ; inline (fmv_load, drive 0)
    dw fm_c_drvb                ; inline (fmv_load, drive 1)
    dw fm_c_free                ; inline (dskw_dfree reads the resident FAT
                                ;         snapshot - no I/O at all)
    dw fm_c_list                ; inline (FS_VIEW = 0)
    dw fm_c_icons               ; inline (FS_VIEW = 1)
    dw fm_c_clock               ; POSTED - app_launch takes the lock
    dw fm_c_bounce              ; POSTED - app_launch takes the lock
    dw fm_c_restart             ; POSTED - ui_cmd takes the lock, forever

; -----------------------------------------------------------------------------
; fm_oncmd - AM_ONCMD for fm_menus (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0-based), SI = our window ptr,
;      BX = the menu set. gfx lock HELD, on the UI task, billed to the
;      instance - the W_ONCLICK contract verbatim.
; out: nothing (all registers preserved)
;
; (menu, item) collapses to one FMC_* and one near-indirect call. A single id
; space rather than a per-menu ladder is deliberate: it is what will let a
; context menu fire the same commands later without a second implementation
; of any of them.
;
; The window pointer is parked in .bss rather than kept in SI, because the
; handlers stage directory entries and call the disk, and SI is the staging
; register everywhere in this module. The closing repaint is NOT optional -
; the kernel does not repaint after an AM_ONCMD returns (SPEC.md 12.2).
; -----------------------------------------------------------------------------
fm_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [fm_cmdwin], si
    mov bx, si
    call fm_vp_set              ; the command acts on THIS window (SPEC 22.1)
    mov bl, ah
    xor bh, bh
    cmp bx, FM_NMENUS
    jae .out                    ; menu_relayout clamps the cell index to
                                ; AM_COUNT, so this cannot fire - it is what
                                ; keeps the table read in range anyway
    mov bl, [fm_menu_base+bx]   ; the menu's first id...
    add bl, al                  ; ...plus the item (menu_hover bounds AL by
                                ; the menu's own item count, SPEC.md 12)
    call fm_docmd
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_docmd - run one FMC_* against the published window and repaint it
; in:  BX = FMC_*, [fm_cmdwin] = the acting window, [fm_vp] published.
;      gfx lock HELD, on the UI task.
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (both callers saved them)
;
; The bar (fm_oncmd) and the context menu (fm_rcmd) both land here, which is
; the entire point of the single FMC_* id space: every command has exactly
; one implementation and exactly one answer to "which side of the gfx lock
; is it on", so the per-entry comments in fm_jmp cover both surfaces at once.
; The closing repaint is not optional - the kernel does not repaint after an
; AM_ONCMD returns (SPEC.md 12.2), and a popup's command has no repaint of
; its own either.
; -----------------------------------------------------------------------------
fm_docmd:
    cmp bx, FMC_COUNT
    jae .out
    shl bx, 1
    call [fm_jmp+bx]            ; CALL r/m16 near-indirect: 8086-legal
    mov si, [fm_cmdwin]
    call fm_repaint
.out:
    ret

; --- the command bodies ------------------------------------------------------
; Each is near-called by fm_docmd with the gfx lock held, may clobber
; anything (both of its callers saved it all), and draws nothing: fm_docmd
; repaints. A body may not assume WHICH surface picked it - the bar and the
; context menu (SPEC.md 12.4) reach every one of them.

fm_c_open:                      ; File > Open - the selection, if any
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]           ; 0xFFFF (no selection) also fails this
    jae .none
    call fm_open_sel
.none:
    ret

fm_c_mkdir:                     ; File > New Folder... - arm the editor
    mov di, [fm_vp]
    mov al, 1
    jmp fm_edit_arm

fm_c_rename:                    ; File > Rename... - arm it on the selection
    mov al, 2
    jmp short fm_arm_sel
fm_c_delete:                    ; File > Delete - arm the CONFIRM line, and
    mov al, 3                   ; nothing else: the disk is touched at Enter
fm_arm_sel:
    ; Both modes need an entry to act on, and both capture it HERE rather
    ; than re-reading FS_SEL at commit time. fm_name is rewritten for every
    ; row the painter draws, so the confirm line could not otherwise even
    ; display the name it is asking about (SPEC.md 22).
    push ax
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]           ; 0xFFFF (no selection) also fails this
    jae .none
    call fm_stage_onam          ; fm_onam + [fm_odir]
    pop ax
    jmp fm_edit_arm
.none:
    pop ax                      ; nothing selected: a no-op, like File > Open
    ret

fm_c_newwin:                    ; Folder > New Window - a second view of here
    mov di, [fm_vp]
    mov al, [di+FS_DRV]
    mov dx, [di+FS_CWD]
    xor si, si                  ; same folder, so the same derived title
    jmp short fm_newwin

fm_c_opennew:                   ; Folder > Open in New Window - the selection
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]
    jae fm_nw_none              ; 0xFFFF (no selection) also fails this
    call fmv_get_dir            ; SI -> the staged entry
    cmp word [si+16], 2
    jne fm_nw_none              ; only a folder has somewhere to open INTO
    mov dx, [si+18]             ; its first cluster
    call fm_stage_name          ; AX is still the index
    mov si, fm_name
    mov al, [di+FS_DRV]
fm_newwin:
    call fm_count               ; CX = windows already open
    cmp cx, FM_MAXWIN
    jae fm_nw_full
    call fm_seed                ; park the target for the new window's
    mov al, KIND_FILES          ; fm_kinit, then let ui_task do the launch
    jmp inst_launch_post        ; NOT app_launch: it takes the lock we hold
fm_nw_full:
    jmp snd_beep                ; at the cap app_launch would front an
                                ; existing window and silently drop the seed,
                                ; which reads as "the menu did nothing"
fm_nw_none:
    ret

fm_c_closew:                    ; File > Close Window
    mov ax, CMD_CLOSE
    jmp ui_post_cmd             ; NOT ui_cmd: it takes the lock we hold

fm_c_restart:                   ; Special > Restart
    mov ax, CMD_REBOOT
    jmp ui_post_cmd

fm_c_clock:                     ; Special > Clock
    mov al, KIND_CLOCK
    jmp inst_launch_post        ; NOT app_launch: it takes the lock (29.4)

fm_c_bounce:                    ; Special > Bounce
    mov al, KIND_BOUNCE
    jmp inst_launch_post

fm_c_refresh:                   ; Folder > Refresh
    mov di, [fm_vp]
    mov dl, [di+FS_DRV]
    jmp short fm_c_mount
fm_c_drva:                      ; Folder > Drive A:
    xor dl, dl
    jmp short fm_c_mount
fm_c_drvb:                      ; Folder > Drive B:
    mov dl, 1
fm_c_mount:
    jmp fm_mount

fm_c_up:                        ; Folder > Up One Folder
    mov di, [fm_vp]
    cmp word [di+FS_CWD], 0
    je .none                    ; already at the root: nothing is above it,
                                ; and saying so is not worth an error line
    call fmv_sync               ; dsk_dotdot walks [dsk_cwd], so that had
                                ; better be OUR folder (SPEC.md 22.1)
    call dsk_dotdot             ; AX = the parent's first cluster (19.2)
    jc .none                    ; a missing or corrupt '..': stay put
    xor si, si                  ; the parent cannot be named without the
    jmp fm_go                   ; grandparent's listing (SPEC.md 22)
.none:
    ret

fm_c_root:                      ; Folder > Root Folder
    xor ax, ax
    xor si, si
    jmp fm_go

fm_c_free:                      ; Folder > Free Space
    ; dskw_dfree counts the resident FAT snapshot, which belongs to the
    ; MOUNTED volume - so the sync is not optional: asked from a window on
    ; the other drive it would otherwise answer about this one.
    mov di, [fm_vp]
    call fmv_sync
    call dskw_dfree             ; DX:AX = free bytes, BX = spc (18.4)
    jc .bad
    mov cx, 10                  ; ...reported in KB: fm_utoa is 16-bit, and
.kb:                            ; even a 32MB FAT16 volume fits once shifted
    shr dx, 1
    rcr ax, 1
    loop .kb
    mov di, fm_msgbuf
    call fm_utoa                ; DI -> the stored NUL
    mov si, fm_s_kbfree
    call fm_scat
    mov ax, [fm_vp]
    mov [fm_msgwin], ax         ; one buffer, one figure: name its owner, or
                                ; a second window's status line would show
                                ; this window's disk (SPEC.md 22)
    mov di, [fm_vp]
    mov byte [di+FS_FERR], FM_FREE
    ret
.bad:
    mov [di+FS_FERR], al        ; FERR_NODISK, and it says so
    ret

fm_c_list:                      ; View > as List
    xor al, al
    jmp short fm_c_setview
fm_c_icons:                     ; View > as Icons
    mov al, 1
fm_c_setview:
    mov di, [fm_vp]
    mov [di+FS_VIEW], al
    mov word [di+FS_SCRL], 0    ; row heights changed: the old first row is
    ret                         ; not this view's first row

; =============================================================================
; The context menu (SPEC.md 12.4/22)
;
; A right-press in a file-manager window's content pops a menu at the
; pointer. This is not a convenience: under WF_FULL (SPEC.md 11.2) the ui
; ladder routes rows 0..19 to wm_hit and the bar is neither drawn nor
; clickable, so a FULLSCREEN file-manager window has no menu bar at all and
; the context menu plus the keyboard are its entire command surface. It is
; also why the in-window Refresh and view-toggle buttons stay: a one-button
; machine queues no EVT_RDOWN at all and must still be able to work.
;
; Three descriptors, chosen by what is under the pointer, all dispatching
; the same FMC_* ids the bar does through the same fm_jmp table - so no
; command gains a second implementation, and no command can drift into
; disagreeing with itself about which side of the gfx lock it is on.
; =============================================================================

MC_ITEMS    equ 0               ; word: array of near ptrs to NUL item strings
MC_CMDS     equ 2               ; word: parallel array of FMC_* bytes
MC_NITEM    equ 4               ; word: item count

; A descriptor is IMMUTABLE .text: four windows share these three, so
; nothing per-window (a selection, a disabled mask) may live in one. There
; is no disabled-item mask at all - a command with nothing to do (Up One
; Folder at the root, Rename with no disk) is the same silent no-op it
; already is from the bar, and one rule beats two.
fm_ctx_file:                    ; the row under the pointer is a file
    dw fm_cxi_file, fm_cxc_file, 3
fm_ctx_fold:                    ; ...or a folder, which can also be opened
    dw fm_cxi_fold, fm_cxc_fold, 4
fm_ctx_dir:                     ; empty space: the folder's own commands
    dw fm_cxi_dir,  fm_cxc_dir,  8

fm_cxi_file:
    dw fm_s_open, fm_s_rename, fm_s_delete
fm_cxc_file:
    db FMC_OPEN, FMC_RENAME, FMC_DELETE

fm_cxi_fold:
    dw fm_s_open, fm_s_opennew, fm_s_rename, fm_s_delete
fm_cxc_fold:
    db FMC_OPEN, FMC_OPENNEW, FMC_RENAME, FMC_DELETE

fm_cxi_dir:
    dw fm_s_mkdir, fm_s_refr, fm_s_up, fm_s_rootf
    dw fm_s_drva, fm_s_drvb, fm_s_aslist, fm_s_asicon
fm_cxc_dir:
    db FMC_MKDIR, FMC_REFRESH, FMC_UP, FMC_ROOT
    db FMC_DRVA, FMC_DRVB, FMC_LIST, FMC_ICONS

; -----------------------------------------------------------------------------
; fm_rclick - select what the right-press is about, then run the popup
; in:  CX = x, DX = y (absolute), SI = window ptr; gfx lock HELD, UI task
; out: CF = 0 and a command latched in [fm_rcmdid]; CF = 1 nothing chosen
; clobbers: nothing else (flags aside)
;
; Split from fm_rcmd for one reason: only the command is billed. The time a
; user spends holding the button is nobody's CPU cost, and charging the
; tracking loop to the instance would let one long press dominate its Task
; Manager row - which is exactly why menu_track is unbilled too (SPEC 13).
;
; FS_SEL moves so the row the menu is about is visibly the row under the
; pointer. FS_CLKT does NOT: it is the double-click detector's other half,
; and stamping it here would make a right-click followed within FM_DBLCLK
; ticks by a left click on the same row read as a double-click and launch a
; package the user never opened (SPEC.md 13).
; -----------------------------------------------------------------------------
fm_rclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [fm_cmdwin], si         ; fm_rcmd acts on this window; fm_docmd's
    mov bx, si                  ; repaint needs it too
    call fm_vp_set

    call fm_edit_end            ; a right-click abandons a half-typed name
                                ; exactly as a left one does (SPEC.md 22)

    push cx                     ; the popup anchors on the ABSOLUTE press
    push dx                     ; point; everything between here and the pop
                                ; works in content coordinates

    mov bx, si                  ; the same content-relative transform
    push cx                     ; fm_onclick makes: save press x...
    mov cx, dx                  ; CX = press y
    call wm_content             ; AX = content left, DX = content top
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x
    call fm_layout              ; BX = window ptr; publishes the mirrors
                                ; fm_hit reads

    call fm_hit                 ; AX = directory index, CF = 1 = not a row
    mov di, [fm_vp]
    jc .empty

    mov [di+FS_SEL], ax         ; the row under the pointer, selected
    mov di, fm_ctx_file
    call fmv_get_dir            ; SI -> the staged entry (SPEC.md 19)
    cmp word [si+16], 2
    jne .have
    mov di, fm_ctx_fold         ; a folder has somewhere to open INTO
    jmp short .have
.empty:
    mov word [di+FS_SEL], 0xFFFF
    mov di, fm_ctx_dir
.have:
    push di                     ; fm_draw_core is the 400-line painter and
                                ; uses every register there is - the chosen
                                ; descriptor does not survive it
    mov si, [fm_cmdwin]
    call fm_repaint             ; show the new selection BEFORE the popup -
                                ; the save-under would otherwise capture the
                                ; old one and put it straight back
    pop di
    pop dx                      ; the absolute press point again
    pop cx

    mov bx, [di+MC_ITEMS]
    mov ax, [di+MC_NITEM]
    call menu_popup             ; CF = 1 nothing chosen; CF = 0 and AL = the
    jc .out                     ; item index (DI, the descriptor, survives)

    xor ah, ah                  ; AL is bounded by MC_NITEM (menu_hover), so
    mov bx, [di+MC_CMDS]        ; the parallel command array is in range
    add bx, ax
    mov al, [bx]
    mov [fm_rcmdid], al
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_rcmd - run the command fm_rclick latched
; in:  nothing ([fm_rcmdid], [fm_cmdwin] and [fm_vp] set by fm_rclick);
;      gfx lock HELD, UI task, billed to the owning instance
; out: nothing (all registers preserved)
;
; The billed half of the pair. It goes through fm_docmd, so a command picked
; from the popup is byte-for-byte the command picked from the bar - same
; body, same deferral through inst_launch_post / ui_post_cmd for the ones
; that need the lock this handler is standing on.
; -----------------------------------------------------------------------------
fm_rcmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [fm_rcmdid]
    xor bh, bh
    call fm_docmd
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The status-line name editor (SPEC.md 22)
; =============================================================================

; -----------------------------------------------------------------------------
; fm_editkey - one keystroke while the front window's FS_EDIT is armed
; in:  AL = ascii (non-zero); gfx lock held, [fm_vp] published
; out: nothing (all registers preserved)
;
; In the two NAMING modes characters are filtered through diskw.inc's OWN
; dskw_char - the same routine dskw_name83 will apply at commit time - so the
; line shows exactly the bytes that will be stored and a name can never look
; accepted and then come back FERR_NAME.
;
; The DELETE confirmation (mode 3) is not a line editor at all and is handled
; first: Enter says yes and EVERY other key says no. That asymmetry is the
; point (SPEC.md 22). There is no undo and no trash on this system, so the
; mode accepts exactly one affirmative keystroke - a stray character can
; never leave a delete armed, waiting for an Enter the user meant for
; something else entirely.
; -----------------------------------------------------------------------------
fm_editkey:
    push ax
    push bx
    push di
    mov di, [fm_vp]
    cmp byte [di+FS_EDIT], 3
    je .confirm
    cmp al, 13
    je .commit
    cmp al, 27
    je .cancel
    cmp al, 8
    je .back

    mov bl, [fm_elen]
    xor bh, bh
    cmp bl, 12
    jae .out                    ; 8 + '.' + 3 is the whole 8.3 budget
    cmp al, '.'
    jne .ordinary
    or bl, bl
    jz .out                     ; a short name may not start with the dot
    call fm_hasdot
    jc .out                     ; ...and may not carry a second one
    jmp short .store
.ordinary:
    call dskw_char              ; upper-cases, or CF=1 = not in the FAT set
    jc .out
.store:
    mov [fm_ebuf+bx], al
    inc bx
    mov byte [fm_ebuf+bx], 0
    mov [fm_elen], bl
    jmp short .out

.back:
    mov bl, [fm_elen]
    xor bh, bh
    or bl, bl
    jz .out
    dec bl
    mov [fm_elen], bl
    mov byte [fm_ebuf+bx-1], 0
    jmp short .out

.confirm:
    cmp al, 13
    jne .cancel                 ; anything but Enter means no (SPEC.md 22)
.commit:
    call fm_edit_commit
    jmp short .out
.cancel:
    call fm_edit_end
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_hasdot - does the name being typed already carry a '.'? (internal)
; in:  nothing
; out: CF=1 if it does; all registers preserved
; -----------------------------------------------------------------------------
fm_hasdot:
    push si
    mov si, fm_ebuf
.scan:
    cmp byte [si], 0
    je .no
    cmp byte [si], '.'
    je .yes
    inc si
    jmp short .scan
.yes:
    pop si
    stc
    ret
.no:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; fm_edit_commit - Enter in the editor: do whatever was armed (SPEC.md 22)
; in:  [fm_vp] published; gfx lock held, UI task - dskw_*'s required context
; out: nothing (all registers preserved); the verdict lands in FS_FERR
;
; Every dskw_* resolves its name in [dsk_cwd], which is whichever window last
; navigated - so the sync is not a nicety: without it a New Folder typed in
; a background window would be created in another window's directory, and the
; only evidence would be that it never appeared in the listing. Read that
; sentence again with "deleted from" in place of "created in" and it is
; obvious why Delete needs it more than the other two do.
;
; Every dskw_* remounts on success with [dsk_cwd] preserved (SPEC.md 18.4),
; so the globals are already right afterwards and fmv_bcast is a pure memory
; copy into every window on this folder - ours first, because our own cache
; is a listing taken before the write, and it is fmv_bcast that clears the
; FS_SEL indices that pointed into it.
;
; Rename and Delete work from fm_onam, the name captured when the mode was
; armed, NOT from FS_SEL now: an 8.3 display name is a legal dskw_name83
; input (SPEC.md 19), and it cannot go stale under us the way an index can.
; -----------------------------------------------------------------------------
fm_edit_commit:
    push ax
    push si
    push di
    mov di, [fm_vp]
    mov al, [di+FS_EDIT]
    cmp al, 3
    je .del
    cmp byte [fm_elen], 0
    je .done                    ; nothing typed: Enter simply cancels
    call fmv_sync               ; act in OUR folder (SPEC.md 22.1)
    cmp al, 2
    je .ren
    mov si, fm_ebuf
    call dskw_mkdir             ; CF=1 with AX = FERR_* (SPEC.md 18.5)
    jmp short .verdict
.ren:
    mov si, fm_onam             ; old name...
    mov di, fm_ebuf             ; ...new name; the eleven name bytes and
    call dskw_rename            ; nothing else move (SPEC.md 18.4)
    jmp short .verdict
.del:
    call fmv_sync
    mov si, fm_onam
    cmp byte [fm_odir], 0
    je .delfile
    call dskw_rmdir             ; an EMPTY folder, else FERR_PROT (18.6)
    jmp short .verdict
.delfile:
    call dskw_delete
.verdict:
    mov di, [fm_vp]             ; .ren left DI on the new name (mov: no flags)
    jc .bad
    mov byte [di+FS_FERR], 0
    call fmv_bcast              ; the new listing reaches every window that
    jmp short .done             ; is showing this folder
.bad:
    mov [di+FS_FERR], al        ; every failure gets visible text (22)
.done:
    call fm_edit_end
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_stage_onam - capture the entry Rename/Delete is about to act on
; in:  AX = directory index, already checked against FS_N
; out: fm_onam = its NUL-terminated 8.3 display name, [fm_odir] = 1 when the
;      SPEC.md 19 type word says folder. All registers preserved.
;
; The copy out of fm_name is the whole point: fm_name is rewritten for every
; row fm_draw_core draws, so a confirm line that merely pointed at it would
; be asking about whichever entry the last repaint happened to end on.
; -----------------------------------------------------------------------------
fm_stage_onam:
    push ax
    push cx
    push si
    push di
    call fm_stage_name          ; fm_name + [fm_ftype]
    mov si, fm_name
    mov di, fm_onam
    mov cx, 12                  ; a display name is 12 chars at most (19)
.cp:
    lodsb                       ; DF=0 per SPEC.md 1
    mov [di], al
    inc di
    or al, al
    jz .typed
    loop .cp
    mov byte [di], 0
.typed:
    mov byte [fm_odir], 0
    cmp word [fm_ftype], 2
    jne .out
    mov byte [fm_odir], 1       ; Delete must call dskw_rmdir for this one
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_edit_arm - hand the status-line editor to one window (SPEC.md 22)
; in:  DI = state block, AL = mode (1 = new folder, 2 = rename, 3 = delete)
; out: nothing (all registers preserved)
;
; Arming disarms every other window, because the name being typed - and the
; name Rename/Delete was armed on - are ONE module-global buffer each: only
; the front window is sent keys, so a second armed window could never receive
; any, but it WOULD keep drawing "New folder:" with this window's characters
; in it, or a confirmation about this window's file.
; -----------------------------------------------------------------------------
fm_edit_arm:
    call fm_edit_end
    mov [di+FS_EDIT], al
    mov byte [di+FS_FERR], 0    ; the prompt replaces the old verdict
    ret

; -----------------------------------------------------------------------------
; fm_edit_end - leave the editor (internal; also the "cancel" path)
; in:  nothing
; out: nothing (all registers preserved)
;
; Clears EVERY block's flag rather than the acting window's: the buffer is
; shared, so a click that cancels an edit may well land in a different window
; from the one that armed it.
; -----------------------------------------------------------------------------
fm_edit_end:
    push cx
    push di
    mov di, fm_pool
    mov cx, FM_MAXWIN
.clr:
    mov byte [di+FS_EDIT], 0
    add di, FS_SIZE
    loop .clr
    mov byte [fm_elen], 0
    mov byte [fm_ebuf], 0
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; fm_repaint - white-fill own content, then redraw everything
; in:  SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; The Note Pad onkey idiom (SPEC.md 22): W_PAINT gets a pre-filled content
; from wm_paint_all, but self-initiated repaints must fill it themselves -
; from the LIVE record: the window resizes (SPEC.md 11.1).
;
; [fm_full] escalates to the whole screen, and only navigation raises it
; (fm_settitle): the window's CAPTION lives in the frame, which a content
; repaint never touches, so a folder change would otherwise leave the title
; bar naming the folder we just left. wm_paint_all under the caller's held
; lock is the ui_drag idiom (SPEC.md 13), and it is affordable precisely
; because the only thing that raises the flag has just paid for a whole
; disk mount - 16 sector reads that dwarf any repaint.
; -----------------------------------------------------------------------------
fm_repaint:
    push ax
    push bx
    push cx
    push dx

    cmp byte [fm_full], 0
    je .content
    mov byte [fm_full], 0
    call wm_paint_all           ; the frame too: the caption just changed
    jmp .done
.content:

    mov bx, si
    call wm_content             ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call gfx_fill               ; white-fill content

    mov bx, si
    call fm_draw_core
    call wm_grow_paint          ; the white fill above erased the grow box;
                                ; BX is still the window ptr (SPEC.md 11.1)
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_icon16 - one entry's 16x16 icon, with the generic fallback
; in:  AX = directory index, CX = x, DX = y (absolute, gfx lock held)
; out: nothing (all registers preserved)
; fmv_get_icon stages the entry from THIS window's cache (SPEC.md 22.1); an
; all-zero entry (the 32 words OR to 0) falls back to built-in ico_app16.
; -----------------------------------------------------------------------------
fm_draw_icon16:
    push ax
    push bx
    push cx
    push si
    call fmv_get_icon           ; SI -> staged 64-byte icon body
    mov bx, si
    push cx                     ; the x coordinate must survive the loop
    xor ax, ax                  ; counter below
    mov cx, 32
.orwords:
    or ax, [bx]
    inc bx
    inc bx
    loop .orwords
    pop cx
    test ax, ax
    jz .noicon
    call icon_draw16            ; body-only table entry (SPEC.md 25)
    jmp .done
.noicon:
    mov si, ico_app16           ; the built-in app icon (library record)
    call icon_draw
.done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_stage_name - stage a directory entry's name, type and size
; in:  AX = directory index
; out: fm_name = NUL-terminated copy of the 16-byte name field (which is
;      NUL-PADDED, not NUL-terminated - the copy forces a terminator),
;      [fm_ftype] = the SPEC.md 19 type word, [fm_fsize] = size in bytes.
;      All registers preserved.
; The type comes out with the name because both callers need it and the
; entry is staged only once: fm_open_sel branches on it and the list row
; suppresses the size column for a folder.
; -----------------------------------------------------------------------------
fm_stage_name:
    push ax
    push bx
    push cx
    push si
    call fmv_get_dir            ; SI -> staged 32-byte entry (SPEC.md 19),
                                ; out of this window's cache (SPEC.md 22.1)
    mov bx, fm_name
    mov cx, 16
.copy:
    lodsb                       ; DF=0 on entry per SPEC.md 1
    mov [bx], al
    inc bx
    loop .copy
    mov byte [fm_name+16], 0
    mov ax, [si]                ; type word (entry offset 16; SI advanced
    mov [fm_ftype], ax          ; past the name)
    mov ax, [si+4]              ; size word (entry offset 20)
    mov [fm_fsize], ax
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_core - draw the whole content (header, buttons, rows or grid,
; scroll bar, selection, status)
; in:  BX = window ptr (content already white, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Layout comes from fm_layout - nothing here may assume 320x200 (SPEC.md
; 22). Row area and scroll bar are omitted when fm_fit = 0; each button
; is drawn only when it fits (Refresh: cw >= 76, view toggle: cw >= 142),
; and the header is truncated to stop 8px short of the leftmost button.
; -----------------------------------------------------------------------------
fm_draw_core:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call fm_layout

    ; --- header line ---
    mov di, fm_hdrbuf
    cmp byte [fm_lmok], 0
    je .badhdr
    mov si, fm_s_drive          ; "Drive X:  N files"
    call fm_scat
    mov al, [fm_ldrv]
    add al, 'A'
    mov [di], al
    inc di
    mov si, fm_s_colsp
    call fm_scat
    mov ax, [fm_lnf]
    call fm_utoa
    mov si, fm_s_files
    call fm_scat
    jmp .hdrtrunc
.badhdr:
    mov si, fm_s_nodisk         ; "No os8088 disk (X:)"
    call fm_scat
    mov al, [fm_ldrv]
    add al, 'A'
    mov [di], al
    inc di
    mov byte [di], ':'
    inc di
    mov byte [di], ')'
    inc di
    mov byte [di], 0
.hdrtrunc:
    ; truncate to (leftmost drawn button - 8)/8 chars (SPEC.md 22)
    mov ax, [fm_cw]
    cmp ax, 142
    jl .hnoview
    sub ax, 136
    jmp .hdrcut
.hnoview:
    cmp ax, 76
    jl .hdrcut                  ; no buttons: bound by cw itself
    sub ax, 68
.hdrcut:
    sub ax, 14                  ; text starts at x=6, so ending 8px short
    jg .hdrpos                  ; of the button means chars <= (b-14)/8
    xor ax, ax
.hdrpos:
    mov cl, 3
    shr ax, cl                  ; AX = chars that fit
    cmp ax, FM_HDRMAX-1
    jae .drawhdr                ; a wide window fits the whole staged string;
    mov si, ax                  ; never poke past the buffer to say so
    mov byte [fm_hdrbuf+si], 0
.drawhdr:
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, 6
    mov si, fm_hdrbuf
    call font_str

    ; --- Refresh button (iff cw >= 76): frame (cw-68,2)..(cw-6,15) ---
    cmp word [fm_cw], 76
    jl .nobtns
    mov ax, [fm_rgt]
    sub ax, 67                  ; abs x1 = cx + cw - 68 = rgt - 67
    mov bx, [fm_cy]
    add bx, 2
    mov cx, [fm_rgt]
    sub cx, 5                   ; abs x2 = rgt - 5
    mov dx, [fm_cy]
    add dx, 15
    call gfx_frame              ; color still CBLACK from the header
    mov cx, ax
    add cx, 3                   ; 56px label centered in the 63px rect
    mov dx, [fm_cy]
    add dx, 4
    mov si, fm_s_refresh
    call font_str

    ; --- view toggle (iff cw >= 142): frame (cw-136,2)..(cw-74,15) ---
    cmp word [fm_cw], 142
    jl .nobtns
    mov ax, [fm_rgt]
    sub ax, 135                 ; abs x1 = rgt - 135
    mov bx, [fm_cy]
    add bx, 2
    mov cx, [fm_rgt]
    sub cx, 73                  ; abs x2 = rgt - 73
    mov dx, [fm_cy]
    add dx, 15
    call gfx_frame
    mov cx, ax
    add cx, 11                  ; "Icons" = 40px -> x1+11
    mov si, fm_s_icons
    cmp byte [fm_lview], 0
    je .vlabel
    add cx, 4                   ; "List" = 32px -> x1+15
    mov si, fm_s_list
.vlabel:
    mov dx, [fm_cy]
    add dx, 4
    call font_str
.nobtns:

    ; --- rows / grid + scroll bar (only when at least one row fits) ---
    cmp word [fm_fit], 0
    je .norows

    mov ax, [fm_total]          ; shown = min(total - scroll, fit)
    sub ax, [fm_lscr]
    jns .sh
    xor ax, ax
.sh:
    cmp ax, [fm_fit]
    jbe .shok
    mov ax, [fm_fit]
.shok:
    mov [fm_shown], ax

    cmp byte [fm_lview], 0
    jne .icons

    ; --- list rows ---
    xor di, di                  ; DI = visible row index
.lrow:
    cmp di, [fm_shown]
    jae .lsel
    mov ax, FM_ROW_H            ; BP = row band top (absolute)
    mul di
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bp, ax
    mov ax, di
    add ax, [fm_lscr]         ; AX = directory index

    push ax
    mov cx, [fm_cx]             ; 16x16 icon at x=4, row top
    add cx, 4
    mov dx, bp
    call fm_draw_icon16
    pop ax
    call fm_stage_name          ; fm_name + [fm_fsize]

    mov bx, [fm_nch]            ; truncate the name to the live budget
    cmp bx, 16
    jae .lname
    mov byte [fm_name+bx], 0
.lname:
    add bp, 4                   ; text y: row top + 4 centers 8px glyphs
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 24
    mov dx, bp
    mov si, fm_name
    call font_str               ; name at x = 24

    cmp word [fm_ftype], 2      ; a folder has no meaningful size (19), and
    je .lnosize                 ; a column of zeroes would only read as one
    mov ax, [fm_fsize]
    push di
    mov di, fm_numbuf
    call fm_utoa
    pop di
    mov si, fm_numbuf
    call font_width             ; AX = string width, px
    mov cx, [fm_rgt]            ; right-align ending at cw-22 (clear of
    sub cx, 20                  ; the scroll bar, SPEC.md 22)
    sub cx, ax
    mov dx, bp
    call font_str
.lnosize:

    inc di
    jmp .lrow

.lsel:
    ; selection bar (inverted row band), only when scrolled into view
    mov ax, [fm_lsel]
    cmp ax, [fm_lnf]       ; 0xFFFF (none) also fails this
    jae .bar
    sub ax, [fm_lscr]
    js .bar
    cmp ax, [fm_shown]
    jae .bar
    mov cx, FM_ROW_H
    mul cx
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = row band top
    mov dx, ax
    add dx, FM_ROW_H - 1        ; y2
    mov ax, [fm_cx]             ; row area width: x 0 .. cw-16
    mov cx, [fm_rgt]
    sub cx, 15
    call gfx_xor_fill
    jmp .bar

    ; --- icon grid ---
.icons:
    mov word [fm_vrow], 0
.irow:
    mov ax, [fm_vrow]
    cmp ax, [fm_shown]
    jae .isel
    mov word [fm_vcol], 0
.icell:
    mov ax, [fm_vrow]
    add ax, [fm_lscr]
    mul word [fm_cols]
    add ax, [fm_vcol]           ; AX = directory index
    cmp ax, [fm_lnf]
    jae .isel                   ; past the last entry: grid done
    push ax
    mov ax, FMI_CELL_W
    mul word [fm_vcol]
    add ax, [fm_cx]
    mov [fm_cellx], ax
    mov ax, FMI_CELL_H
    mul word [fm_vrow]
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov [fm_celly], ax
    pop ax

    push ax
    mov cx, [fm_cellx]          ; icon centered at cell +(31, 3)
    add cx, 31
    mov dx, [fm_celly]
    add dx, 3
    call fm_draw_icon16
    pop ax
    call fm_stage_name
    mov byte [fm_name+9], 0     ; grid names: 9 chars max (SPEC.md 22)
    mov si, fm_name
    call font_width
    mov cx, FMI_CELL_W
    sub cx, ax
    shr cx, 1                   ; centered under the icon
    add cx, [fm_cellx]
    mov dx, [fm_celly]
    add dx, 23
    mov byte [gfx_color], CBLACK
    call font_str

    inc word [fm_vcol]
    mov ax, [fm_vcol]
    cmp ax, [fm_cols]
    jb .icell
    inc word [fm_vrow]
    jmp .irow

.isel:
    ; selection cell (inverted), only when scrolled into view
    mov ax, [fm_lsel]
    cmp ax, [fm_lnf]
    jae .bar
    xor dx, dx
    div word [fm_cols]          ; AX = grid row, DX = column
    mov cx, dx                  ; CX = column
    sub ax, [fm_lscr]
    js .bar
    cmp ax, [fm_shown]
    jae .bar
    push ax
    mov ax, FMI_CELL_W
    mul cx
    add ax, [fm_cx]
    mov [fm_cellx], ax
    pop ax
    mov cx, FMI_CELL_H
    mul cx
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = cell top
    mov dx, ax
    add dx, FMI_CELL_H - 1
    mov ax, [fm_cellx]
    mov cx, ax
    add cx, FMI_CELL_W - 1
    call gfx_xor_fill

    ; --- scroll bar: x cw-14..cw-1, y 22..listb-1 (SPEC.md 22) ---
.bar:
    mov ax, [fm_rgt]
    sub ax, FM_SB_W-1           ; abs x1
    mov [fm_sbx1], ax
    mov bx, [fm_cy]
    add bx, FM_ROW_Y0
    mov [fm_sby1], bx           ; abs y1
    mov cx, [fm_rgt]            ; abs x2
    mov dx, [fm_cy]
    add dx, [fm_listb]
    dec dx
    mov [fm_sby2], dx           ; abs y2
    mov byte [gfx_color], CBLACK
    call gfx_frame

    mov ax, [fm_sbx1]           ; arrow-cell rules, 11 rows in from each end
    mov bx, [fm_rgt]
    mov dx, [fm_sby1]
    add dx, 10
    call gfx_hline
    mov dx, [fm_sby2]
    sub dx, 10
    call gfx_hline

    mov bp, 0                   ; up glyph: rows y1+3..y1+7, widths 1..9
    mov dx, [fm_sby1]
    add dx, 3
.uparr:
    mov ax, [fm_sbx1]
    add ax, 6
    sub ax, bp
    mov bx, [fm_sbx1]
    add bx, 6
    add bx, bp
    call gfx_hline
    inc dx
    inc bp
    cmp bp, 5
    jb .uparr

    mov bp, 4                   ; down glyph: rows y2-7..y2-3, widths 9..1
    mov dx, [fm_sby2]
    sub dx, 7
.dnarr:
    mov ax, [fm_sbx1]
    add ax, 6
    sub ax, bp
    mov bx, [fm_sbx1]
    add bx, 6
    add bx, bp
    call gfx_hline
    inc dx
    dec bp
    jns .dnarr

    mov ax, [fm_sbx1]           ; track: gray between the rules
    inc ax
    mov bx, [fm_sby1]
    add bx, 11
    mov cx, [fm_rgt]
    dec cx
    mov dx, [fm_sby2]
    sub dx, 11
    cmp bx, dx
    jg .nothumb                 ; degenerate track
    call gfx_fill_gray

    call fm_thumb               ; CF=1: everything fits, no thumb
    jc .nothumb
    mov bx, ax                  ; y1 = thumb top (abs)
    add dx, ax
    dec dx                      ; y2 = top + height - 1
    mov ax, [fm_sbx1]
    add ax, 2
    mov cx, [fm_rgt]
    sub cx, 2
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov byte [gfx_color], CBLACK
    call gfx_frame
.nothumb:

.norows:
    ; --- status line at (6, staty): the precedence is binding (SPEC.md 22),
    ;     because all four of these can be true at the same moment. The
    ;     header buffer is free by now, so every branch stages into it and
    ;     one truncation serves them all.
    mov di, fm_hdrbuf
    mov si, fm_s_loading        ; 1. a pending load trumps everything
    cmp word [ld_pending], 0
    jne .stat
    mov al, [fm_ledit]          ; 2. the input line, prompt and all - the
    or al, al                   ;    editor IS the status line (SPEC.md 22)
    jz .st_err
    cmp al, 3
    je .st_del
    mov si, fm_s_newfld
    cmp al, 1
    je .st_prompt
    mov si, fm_s_renam          ; "Rename NAME to: NEW"
    call fm_scat
    mov si, fm_onam
    call fm_scat
    mov si, fm_s_rento
.st_prompt:
    call fm_scat
    mov si, fm_ebuf
    jmp short .stat
.st_del:
    mov si, fm_s_delpr          ; "Delete NAME? Enter=yes Esc=no"
    call fm_scat
    mov si, fm_onam
    call fm_scat
    mov si, fm_s_delq
    jmp short .stat
.st_err:
    mov al, [fm_lferr]          ; 3./4. this window's own last word: the
    or al, al                   ;    free-space figure, or the FERR_* text
    jz .st_load                 ;    that keeps no dskw_* failure silent
    cmp al, FM_FREE
    je .st_free
    mov bx, fm_errtab
    jmp short .st_tab
.st_free:
    mov si, fm_msgbuf
    jmp short .stat
.st_load:
    mov al, [ld_status]         ; 5. and finally the loader's
    cmp al, LD_ENEED            ; status 6 carries a live number (SPEC.md
    je .st_need                 ; 21/22): "Needs nnnK", nnn = [ld_needkb]
    mov bx, fm_stattab
    cmp al, 5
    ja .statdone                ; out-of-range status: draw nothing
.st_tab:
    cmp al, FM_NERR
    jae .statdone
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov si, [bx]
    jmp short .stat
.st_need:
    mov si, fm_s_needs          ; "Needs " + the refusing package's KB
    call fm_scat                ; figure + "K" (SPEC.md 21/22) - the one
    mov ax, [ld_needkb]         ; status string with a live number in it
    call fm_utoa                ; appends the decimal at DI
    mov si, fm_s_kaych
.stat:
    call fm_scat                ; stage it, then truncate to the live cw
    mov ax, [fm_cw]             ; ("Out of memory" is 104px and cw can
    sub ax, 12                  ; legally be 94, SPEC.md 22)
    jg .stpos
    xor ax, ax
.stpos:
    mov cl, 3
    shr ax, cl                  ; AX = chars that fit
    cmp ax, FM_HDRMAX-1
    jae .stdraw
    mov bx, ax
    mov byte [fm_hdrbuf+bx], 0
.stdraw:
    cmp byte [fm_hdrbuf], 0
    je .statdone
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, [fm_staty]
    mov si, fm_hdrbuf
    call font_str
.statdone:

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_scat - append a NUL string to a buffer
; in:  SI = NUL source, DI = destination
; out: DI -> the stored NUL (so the next append overwrites it); SI advanced
; clobbers: AL (internal helper)
; -----------------------------------------------------------------------------
fm_scat:
    lodsb
    mov [di], al
    test al, al
    jz .done
    inc di
    jmp fm_scat
.done:
    ret

; -----------------------------------------------------------------------------
; fm_utoa - unsigned word to decimal ASCII, no leading zeros
; in:  AX = value, DI = destination (needs up to 6 bytes: 5 digits + NUL)
; out: DI -> the stored NUL
; clobbers: nothing else (internal helper)
; Digits are pushed least-significant first, then popped in print order.
; -----------------------------------------------------------------------------
fm_utoa:
    push ax
    push bx
    push cx
    push dx

    mov bx, 10
    xor cx, cx                  ; CX = digit count
.div:
    xor dx, dx
    div bx                      ; AX = quotient, DX = remainder
    add dl, '0'
    push dx
    inc cx
    test ax, ax
    jnz .div
.emit:
    pop ax
    mov [di], al
    inc di
    loop .emit
    mov byte [di], 0

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

; --- per-window state (SPEC.md 22): the kind's KD_POOL, one block per window
fm_pool:    resb 64             ; FM_MAXWIN x FS_SIZE (4 x 16; a literal per
                                ; the no-forward-ref-in-bss rule)

; --- the acting window, published by fm_vp_set and read everywhere ----------
fm_vp:      resw 1              ; -> its 16-byte state block above
fm_vseg:    resw 1              ; its VIEW_SEG cache segment (SPEC.md 2.3)
fm_vinst:   resw 1              ; -> its instance record, 0 = unowned

; --- app_launch has no argument channel: the target rides here (SPEC 22.1) --
fm_seed_drv: resb 1             ; drive for the next fm_kinit
fm_seed_cwd: resw 1             ; its directory's first cluster
fm_seed_nam: resw 1             ; -> fm_snam, or 0 = derive the title
fm_snam:     resb 17            ; the seeded folder name, COPIED (fm_name is
                                ; reused by the repaint before the drain)

fm_full:    resb 1              ; 1 = the next fm_repaint owes the frame too.
                                ; Module-global on purpose: it is raised and
                                ; consumed inside one held lock, by one window
fm_cmdwin:  resw 1              ; fm_oncmd/fm_rclick scratch: the acting
                                ; window ptr, held across the dispatch
fm_rcmdid:  resb 1              ; the FMC_* a context menu chose, handed from
                                ; fm_rclick to fm_rcmd. Module-global for the
                                ; same reason fm_cmdwin is: one UI task, one
                                ; popup at a time (SPEC.md 12.4)

; The name being typed is ONE buffer for all four windows: only the front
; window receives W_ONKEY (SPEC.md 13), so only one editor can ever be live,
; and fm_edit_arm clears every other block's FS_EDIT to keep it that way.
fm_elen:    resb 1              ; characters typed so far (0..12)
fm_ebuf:    resb 13             ; the name being typed, NUL-terminated

; What Rename/Delete was armed ON, captured at arm time by fm_stage_onam for
; the same one-live-editor reason - and because fm_name, the only other copy
; of a display name in this module, is rewritten for every row the painter
; draws (SPEC.md 22).
fm_onam:    resb 13             ; the target's 8.3 display name
fm_odir:    resb 1              ; 1 = it is a folder, so Delete means rmdir

; The free-space line. FS_FERR = FM_FREE marks the window that asked; this
; word says which one that was, because the figure itself is one buffer.
fm_msgwin:  resw 1              ; -> the state block that owns fm_msgbuf
fm_msgbuf:  resb 16             ; "65535 KB free" + NUL, with room to spare

; --- fm_layout's mirrors of the acting window's hot fields (SPEC.md 22.1) ---
; Read-only snapshots refreshed at the head of every fm_layout, because
; fm_draw_core has no register free to carry a state-block pointer through
; 400 lines. fm_lscr is the one that is written, and fm_clamp_scroll is the
; one place it is written back into the block.
fm_lview:   resb 1              ; FS_VIEW
fm_lmok:    resb 1              ; FS_MOK
fm_ldrv:    resb 1              ; FS_DRV
fm_ledit:   resb 1              ; FS_EDIT
fm_lferr:   resb 1              ; FS_FERR
fm_lpad:    resb 1              ; keeps the words below even-aligned
fm_lnf:     resw 1              ; FS_N
fm_lscr:    resw 1              ; FS_SCRL
fm_lsel:    resw 1              ; FS_SEL

fm_cx:      resw 1              ; fm_layout: content left (abs)
fm_cy:      resw 1              ; fm_layout: content top (abs)
fm_cw:      resw 1              ; fm_layout: content width
fm_ch:      resw 1              ; fm_layout: content height
fm_rgt:     resw 1              ; fm_layout: content right (abs, inclusive)
fm_staty:   resw 1              ; fm_layout: status text top (content-rel)
fm_listb:   resw 1              ; fm_layout: first row below the row area
fm_fit:     resw 1              ; fm_layout: whole rows that fit
fm_cols:    resw 1              ; fm_layout: grid columns (1 in list view)
fm_total:   resw 1              ; fm_layout: total rows
fm_nch:     resw 1              ; fm_layout: list-view name chars that fit

fm_shown:   resw 1              ; draw scratch: rows drawn this paint
fm_fsize:   resw 1              ; fm_stage_name: entry size, bytes
fm_ftype:   resw 1              ; fm_stage_name: SPEC.md 19 type word
fm_vrow:    resw 1              ; grid scratch: visible row / column
fm_vcol:    resw 1
fm_cellx:   resw 1              ; grid scratch: current cell origin (abs)
fm_celly:   resw 1
fm_sbx1:    resw 1              ; scroll bar scratch: left col (abs)
fm_sby1:    resw 1              ; top row (abs)
fm_sby2:    resw 1              ; bottom row (abs)

fm_name:    resb 17             ; NUL-safe copy of one 16-byte name field
fm_hdrbuf:  resb 48             ; = FM_HDRMAX (a literal per the no-forward-
                                ; ref-in-bss rule). The header is 22 bytes at
                                ; most; the size comes from the status line's
                                ; worst case, "Delete NAME? Enter=yes Esc=no"
                                ; at 38, which is staged whole and truncated
                                ; afterwards
fm_numbuf:  resb 6              ; fm_utoa scratch: 5 digits + NUL

section .text
