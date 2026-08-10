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
; Per-window state is a 24-byte KD_POOL block (fm_pool, 4 x 24) that
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
;
; **This module is `.cold` (SPEC.md 2.6).** Its code has a CS of its own, so
; a routine the kernel reaches from outside is a six-byte resident thunk in
; kernel.asm under the PUBLIC name, and the body here carries an `_x` suffix;
; calls that leave the cold set go out through `KERNEL_SEG:cw_*`. Two things
; are on the author rather than the build: CS is NOT KERNEL_SEG here, so
; nothing may take a kernel segment from it, and DATA stays in `.text` - the
; section toggles below are that, and a `db`/`dw` added on the wrong side of
; one is read at the wrong segment with no diagnostic at all.
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

; --- per-window state block (KD_POOL, 4 x 24 bytes; SPEC.md 22/29.3) --------
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
FS_EDIT     equ 13              ; byte: the status-line editor is armed here
                                ; +14 and +15 are FREE: FS_FERR and FS_LDST,
                                ; the FERR_* of the last file operation in
                                ; this window and the LD_* of the last load it
                                ; posted. Both were per window because a
                                ; status line is a report about what the user
                                ; did HERE, and [ld_status] is one global - so
                                ; a failed load put 'Bad package' on every
                                ; open Disk window at once, two of them each
                                ; claiming to have been asked. A verdict is a
                                ; TOAST now (SPEC.md 60.5) and belongs to no
                                ; window at all, which DISSOLVES that question
                                ; rather than answering it. The offsets are
                                ; left as a hole deliberately: every one below
                                ; is hand-written, and FS_SIZE is unchanged.
FS_VSEG     equ 16              ; word: this window's listing-cache claim
                                ; (SPEC.md 50.2), 0 = none - fm_kinit claims
                                ; VIEW_KB and the instance's teardown frees
                                ; it, so nothing here has a close hook
FS_VKB      equ 18              ; byte: how many KB that claim actually is.
                                ; A driver-backed volume lists DSK_VENT
                                ; entries and needs DSK_VKB (SPEC.md 22.6),
                                ; so the cache is RE-claimed when the window
                                ; moves to one - and a refusal just clears
                                ; FS_VSEG, which is the documented fallback
FS_DIRTY    equ 19              ; byte: 1 = a file operation has changed the
                                ; folder this window is showing since its
                                ; cache was built, so neither the cache nor
                                ; the pixels are a picture of it any more
                                ; (SPEC.md 22.8). Spent by fm_focus when the
                                ; window next comes to the front
FS_FREE     equ 20              ; word: KB free on the volume this window is
                                ; showing, 0xFFFF = not known. Read ONCE per
                                ; mount, in fmv_load, because counting it
                                ; walks the whole FAT (SPEC.md 22.7)
FS_USED     equ 22              ; word: KB summed over this folder's listed
                                ; entries, 0xFFFF = not known. Derived from
                                ; the window's own cache, so it costs no I/O
                                ; and follows the cache the paint reads
FS_SIZE     equ 24              ; = the Disk kind's KD_SSIZE

FM_MAXWIN   equ VIEW_SLOTS      ; = KD_CAP: one cache slot per window
FV_ICONS    equ DSK_NENT*DSK_DE_SIZE    ; icon images start here inside a
                                ; FLOPPY-sized slot; a driver-backed volume's
                                ; is [dsk_ioff], and fmv_iofs answers both
FM_HDRMAX   equ 48              ; fm_hdrbuf's size: the header and the status
                                ; line are both staged whole and truncated
                                ; afterwards, and the longest of them is the
                                ; delete confirmation (SPEC.md 22)

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here

; -----------------------------------------------------------------------------
; files_init - reset module state (no window is created at boot, SPEC.md 22)
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; .bss is not zeroed by the loader: every state variable is set here.
; -----------------------------------------------------------------------------
files_init_x:
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
    mov word [fm_tdirty], 0
    mov word [fm_vp], fm_pool   ; no acting window yet: point the three
    mov word [fm_vseg], 0           ; published words at slot 0, and at the
    mov word [fm_vinst], 0          ; global snapshot rather than a cache
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
fm_kinit_x:
    push ax
    push cx
    push dx
    push si

    mov word [di+FS_VSEG], 0    ; the listing cache: VIEW_KB of the heap,
    mov byte [di+FS_VKB], 0     ; owned by THIS instance so its teardown
    push bx                     ; frees it (SPEC.md 50.2/50.4). fmv_fit
    push dx                     ; RE-claims it at DSK_VKB the moment this
    mov ax, si                  ; window lists a driver-backed volume (22.6)
    sub ax, inst_tab
    mov cl, 5
    shr ax, cl
    mov bx, ax
    mov ax, VIEW_KB
    call KERNEL_SEG:cw_mem_claim
    jc .nocache                 ; no room: the window still works, it just
    mov [di+FS_VSEG], dx        ; re-reads the global snapshot to paint
    mov byte [di+FS_VKB], VIEW_KB
.nocache:
    pop dx
    pop bx

    mov word [di+FS_SEL], 0xFFFF    ; a fresh window has no selection,
    mov word [di+FS_CLKT], 0        ; nothing listed, list view, scrolled to
    mov word [di+FS_SCRL], 0        ; the top, and no half-typed name - a
    mov word [di+FS_N], 0           ; reopened window never resumes one
    mov byte [di+FS_VIEW], 0
    mov byte [di+FS_EDIT], 0
    mov byte [di+FS_DIRTY], 0   ; a KD_POOL block is REUSED (SPEC.md 29.3):
                                ; a window closed while it still owed a
                                ; refresh must not hand that debt to the next
                                ; window in its slot, which is about to list
                                ; its own folder from scratch anyway

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
    mov word [fm_tdirty], 0     ; app_launch's wm_show draws this window whole
                                ; a moment from now, caption included - do not
                                ; leave the first self-repaint owing a strip
                                ; that is already correct

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
    call KERNEL_SEG:cw_inst_win_owner ; DI = record, or 0; BX preserved
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
    mov bx, [di+FS_VSEG]        ; 0 = this window has no cache; every reader
    mov [fm_vseg], bx           ; below falls back to the global snapshot
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
    push bx
    push dx
    push si
    mov dx, [fm_vseg]
    or dx, dx
    jnz .have                   ; no cache (the claim was refused): read the
    mov dx, [dsk_dseg]          ; GLOBAL snapshot at the matching offset. A
    call fmv_iofs               ; slot offset is 0-based and the snapshot's
    cmp si, bx                  ; is not, so the two bases have to be swapped
    jb .dir                     ; over - which is all this arithmetic is
    sub si, bx
    add si, [dsk_ioff]
    jmp short .have
.dir:
    add si, [dsk_doff]
.have:
    mov ax, ds                  ; the kernel segment, out of DS: this module
                                ; is .cold, so CS is COLD_SEG (SPEC.md 2.6)
    call KERNEL_SEG:cw_dsk_copy_seg
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_iofs - where the icon images start inside a cache slot
; in:  nothing
; out: BX = the slot-relative offset
; clobbers: BX (the output), flags
;
; A slot MIRRORS the listing it was filled from, so its shape follows
; [dsk_nmax]: 1KB of entries then 2KB of icons on a floppy, 2KB then 4KB on a
; driver-backed volume (SPEC.md 22.6).
; -----------------------------------------------------------------------------
fmv_iofs:
    push ax
    push dx
    mov ax, [dsk_nmax]
    mov dx, DSK_DE_SIZE
    mul dx
    mov bx, ax
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
    push bx
    push cx
    push di
    mov cl, 6                   ; index * 64, past the entry images
    shl ax, cl
    call fmv_iofs
    add ax, bx
    mov si, ax
    mov di, dsk_ico
    mov cx, DSK_ICO_SIZE
    call fmv_copy_in
    pop di
    pop cx
    pop bx
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
    push bx
    push cx
    push dx
    push si
    push di
    call fmv_fit                ; the claim has to be big enough for THIS
    mov ax, [di+FS_VSEG]        ; volume's listing before anything is copied
    or ax, ax
    jz .none                    ; no cache: nothing to write it into
    push di
    call fmv_iofs               ; BX = where the icons start in a slot
    mov si, [dsk_doff]
    xor di, di
    mov cx, bx
    call fmv_copy_out
    mov si, [dsk_ioff]
    mov di, bx
    mov cx, [dsk_nmax]
    mov dx, DSK_ICO_SIZE
    push ax
    mov ax, cx
    mul dx
    mov cx, ax
    pop ax
    call fmv_copy_out
    pop di
.none:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_copy_out - CX bytes from the live listing into this window's slot
; in:  SI = source offset in [dsk_dseg], DI = slot offset, AX = the slot's
;      segment, CX = bytes (even)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fmv_copy_out:
    push dx
    mov dx, [dsk_dseg]
    call KERNEL_SEG:cw_dsk_copy_seg
    pop dx
    ret

; -----------------------------------------------------------------------------
; fmv_fit - make this window's cache big enough for the volume being listed
; in:  DI = state block
; out: nothing (all registers preserved)
;
; A floppy's listing is 3KB and a driver-backed volume's is DSK_VKB (SPEC.md
; 22.6), and a window moves between the two by clicking a desktop icon - so
; the claim is re-made when the shape changes rather than sized once at
; launch, and a machine with only floppies never pays for the bigger one.
;
; A refused claim clears FS_VSEG, which is the documented fallback and not an
; error: the window then paints from the global snapshot (SPEC.md 22.1) and
; costs floppy I/O it would otherwise have avoided.
; -----------------------------------------------------------------------------
fmv_fit:
    push ax
    push bx
    push cx
    push dx
    mov al, VIEW_KB             ; how many KB THIS listing needs
    cmp word [dsk_nmax], DSK_NENT
    jbe .want
    mov al, DSK_VKB
.want:
    cmp al, [di+FS_VKB]
    jbe .out                    ; what we hold already covers it
    mov cl, al
    mov dx, [di+FS_VSEG]        ; give the old one back FIRST: claiming the
    or dx, dx                   ; new one while holding it can refuse on a
    jz .claim                   ; machine that had the room all along (50.3)
    call fmv_owner
    call KERNEL_SEG:cw_mem_free
    mov word [di+FS_VSEG], 0
    mov byte [di+FS_VKB], 0
.claim:
    call fmv_owner              ; BX = the owning instance slot
    mov al, cl
    xor ah, ah
    call KERNEL_SEG:cw_mem_claim
    jc .out                     ; refused: no cache, and that is a normal
    mov [di+FS_VSEG], dx        ; path (SPEC.md 50.3)
    mov [di+FS_VKB], cl
.out:
    mov bx, [di+FS_VSEG]        ; fm_vseg is what every reader looks at, and
    mov [fm_vseg], bx           ; this may just have changed it
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_owner - the instance slot that owns this window's claim (internal)
; in:  DI = state block
; out: BX = the owner word mem_claim/mem_free want
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
fmv_owner:
    push ax
    mov ax, [fm_vinst]
    or ax, ax
    jnz .have
    mov bx, MEM_K_SAVE          ; cannot happen: fm_vp_set fills fm_vinst
    pop ax                      ; before anything reaches here
    ret
.have:
    sub ax, inst_tab
    push cx
    mov cl, 5
    shr ax, cl
    pop cx
    mov bx, ax
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
    call KERNEL_SEG:cw_dsk_chdir              ; a remount with [dsk_keepcwd] raised

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
    call fmv_store
    call fm_measure             ; the free/size line is about the listing we
                                ; just took, so it is read here and nowhere
                                ; else (SPEC.md 22.7)

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_measure - fill one window's FS_FREE / FS_USED (SPEC.md 22.7)
; in:  DI = state block; the GLOBAL snapshot is this window's directory (the
;      caller has just mounted, so disk_dir / disk_nfiles / dsk_* are ours)
; out: nothing (all registers preserved)
;
; Called from fmv_load and fmv_bcast - the two places a window's listing is
; replaced - and NOT from the painter, because the free figure walks the whole
; FAT (dsk_free_clus) and the status line is drawn on every repaint. That is
; the whole design: the expensive half is read once per mount, exactly as
; often as the listing it describes.
;
; Both figures are KB and both use 0xFFFF for "not known", which the status
; line draws as '?' - a failed mount has no honest number to give and must
; not print a stale one from the volume this window used to be showing.
; -----------------------------------------------------------------------------
fm_measure:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [di+FS_FREE], 0xFFFF
    mov word [di+FS_USED], 0xFFFF
    cmp byte [di+FS_MOK], 0
    je .out                     ; no good mount: no figures, and say so

    call KERNEL_SEG:cw_dsk_free_clus ; --- free: clusters -> KB, once per mount ---
    jc .used
    mov dl, [dsk_spc]
    xor dh, dh
    mul dx                      ; DX:AX = free sectors (a volume caps at
                                ; 65,535 sectors, SPEC.md 18.7, so DX = 0)
    shr ax, 1                   ; ...and a sector is half a KB
    mov [di+FS_FREE], ax

.used:                          ; --- used: sum this folder's listed sizes ---
    ; The entry size is a DWORD (SPEC.md 19.1) and a folder's total can pass
    ; 64KB long before the volume does, so the running sum is 32-bit and only
    ; the KB it converts to is a word. Subdirectories carry size 0 by
    ; contract, so this is the bytes of the FILES at this level - it does not
    ; recurse, and nothing here reads a sector to find out what is under one.
    xor bx, bx                  ; BX:CX = running byte total (hi:lo)
    xor cx, cx
    mov dx, [di+FS_N]           ; the listing this window was just given
    or dx, dx
    jz .kb
    xor ax, ax                  ; AX = entry index
.sum:
    push ax
    call KERNEL_SEG:cw_dsk_get_dir ; SI -> the staged entry, in our own segment
    mov ax, [si+20]             ; size lo...
    add cx, ax
    mov ax, [si+22]             ; ...and hi, with the carry out of the low
    adc bx, ax                  ; half folded in
    pop ax
    inc ax
    cmp ax, dx
    jb .sum
.kb:
    mov ax, cx                  ; bytes -> KB: (hi:lo) >> 10, and the result
    mov dx, bx                  ; is a word because a volume caps at 32MB
    mov cx, 10
.shift:
    shr dx, 1
    rcr ax, 1
    loop .shift
    cmp ax, 0xFFFF              ; a hostile listing could sum past the
    je .out                     ; sentinel: leave it saying "not known"
    mov [di+FS_USED], ax
.out:
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
fmv_sync_x:
    push ax
    push bx
    push dx
    mov al, [di+FS_DRV]
    cmp al, [disk_drive]
    jne .load
    mov dx, [di+FS_CWD]
    cmp dx, [dsk_cwd]
    jne .load
    cmp byte [dsk_lstale], 0    ; RIGHT FOLDER, NO LISTING. A quiet mount and
    je .out                     ; a deferred write both leave the globals
                                ; naming this very folder with disk_nfiles = 0
                                ; and the rebuild owed (SPEC.md 18.9/18.4), so
                                ; the (drive, cwd) compare above says "already
                                ; synced" about an EMPTY snapshot. The caller
                                ; is about to resolve a directory INDEX
                                ; against it - that is what a sync is for -
                                ; and index-against-nothing is the silent
                                ; half of docs/FIELD-NOTES.md 4: measured, a
                                ; double-click after a Control Panel close
                                ; launched nothing at all and cost no I/O
                                ; doing it. Fall through and re-list.
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
; fmv_mark - a file operation changed the CURRENT folder (SPEC.md 22.8)
; in:  nothing - [disk_drive] and [dsk_cwd] ARE the folder that changed,
;      because that is where every file API resolves a name (SPEC.md 19.2)
; out: nothing (all registers preserved)
;
; The other end of "paints read the cache, actions re-sync" (SPEC.md 22.1),
; and the half that was missing. A window's cache is rebuilt by the file
; manager's OWN operations and by nothing else, so a package writing through
; the file API changed a folder that four windows might be showing and none
; of them was told. What that cost was not a stale display but a wrong file:
; a double-click resolves a row against the cache and hands the loader a
; DIRECTORY INDEX, which is then resolved against the rebuilt globals - so a
; new name sorting ahead of the clicked one (SPEC.md 19.4) shifts every entry
; after it and the loader reads the wrong entry entirely
; (docs/FIELD-NOTES.md 4).
;
; This is called from dskw_sync (SPEC.md 18.4), the one place a successful
; file operation passes through, so no operation can be added that forgets
; it. It only ever sets a byte - the caller may hold the gfx lock, may be
; deep inside a batch, and must not be made to draw or to mount.
; -----------------------------------------------------------------------------
fmv_mark:
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
    mov byte [di+FS_DIRTY], 1
    cmp byte [dskw_batch], 0    ; a multi-file operation owes its own
                                ; coherence pass and pays it at the END
    jne .next                   ; (fmv_reload_all + fmv_repaint_all, 22.3),
                                ; which clears every mark it just made - so
                                ; waking the UI check here would only fire
                                ; during the replace question's PAUSE, the
                                ; one moment the operation is stopped and
                                ; waiting for the user to read the line it
                                ; is asking about
    mov byte [fm_fchk], 1       ; and otherwise the UI task owes the FRONT
                                ; window a look: the window a package just
                                ; wrote into may be the one under the
                                ; pointer, where waiting for a focus change
                                ; would mean waiting for a click that never
                                ; comes
.next:
    add si, I_RECSZ
    loop .scan
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_gneed - does anything ON SCREEN need the GLOBAL listing right now?
; in:  nothing
; out: CF = 1 = rebuild it before returning to the event loop; CF = 0 = record
;      the debt instead and let whoever reads it next pay
; clobbers: flags
;
; The other half of SPEC.md 18.4 rule 3, which used to have no other half: a
; successful write ALWAYS remounted, so a background write to a folder nobody
; was looking at cost a floppy's worth of sectors to rebuild a listing no
; pixel on screen was drawn from. That is the Control Panel's whole cost -
; SYSTEM.CFG lands on the system disk (51.5.1) while the user is on B: - and
; a package saving a file is the same shape.
;
; The answer is short because only ONE thing reads disk_dir / disk_nfiles
; while it is on screen: the Standard File dialog, which is modal and
; deliberately keeps no cache of its own (38.2). Its New Folder button is a
; WRITE made while it is up (fdlg_newfolder -> dskw_mkdir), so this is not a
; theoretical case - it is the one that must not be deferred, or the folder
; the user just made would not appear until something else re-listed.
;
; A Disk window is NOT on this list, and that is 22.1/22.8 rather than an
; omission: it paints from its own VIEW CACHE, it has just been marked
; FS_DIRTY by fmv_mark, and fm_focus rebuilds cache and pixels TOGETHER when
; it comes to the front. Remounting here would not refresh one - it would
; only make the mount fm_focus is about to skip happen earlier, for a window
; that may never be looked at again.
; -----------------------------------------------------------------------------
fmv_gneed:
    cmp word [fdlg_win], 0
    je .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; fm_focus - spend a Disk window's refresh debt as it comes to the front
; in:  BX = the window gaining the focus (0 = none), AL = 0 the caller draws
;      this window whole when we answer CF=1 / AL = 1 draw it here;
;      gfx lock HELD, UI task
; out: CF = 1 = it was a Disk window whose folder had changed under it and
;      its cache has been rebuilt - with AL = 0 the caller MUST now draw it
;      whole; CF = 0 = nothing was owed. All registers preserved.
;
; The second half of SPEC.md 22.8. A window that is not being looked at is
; not re-listed, because a re-list is a MOUNT and there may be four windows
; owing one; the debt is paid where it becomes visible, which is the moment
; the window comes to the front.
;
; The cache and the pixels are made current TOGETHER, and they have to be:
; a cache re-read without a repaint is the very failure this exists to stop,
; only with the stale half moved from the cache to the glass - the rows on
; screen would name one file and the hit-tester resolve another. So the
; caller says which of us is going to draw. wm_raise (SPEC.md 11.90) draws
; the window whole anyway when anything was covering it, and takes CF as
; permission to draw it whole when nothing was; the UI task's own pass has
; nobody to defer to and asks for AL = 1, where an unclipped fm_repaint is
; safe because wm_top just said nothing is on top of this window.
;
; The mount is skipped when the globals are already this folder and are a
; real listing - the common case by far, since the write that dirtied the
; window ended in a remount of the folder it wrote to (SPEC.md 18.4).
; -----------------------------------------------------------------------------
fm_focus_x:
    or bx, bx
    jz .none                    ; no front window (wm_top answers 0)
    push ax
    push cx
    push dx
    push si
    push di
    mov ch, al                  ; bank the mode: AL is about to carry drives
    call KERNEL_SEG:cw_inst_win_owner ; DI = its instance record, 0 = unowned
    or di, di
    jz .clean
    cmp byte [di+I_KIND], KIND_FILES
    jne .clean                  ; not a Disk window: this is every other
                                ; window in the system, and it is two
                                ; compares
    mov di, [di+I_SPTR]
    cmp byte [di+FS_DIRTY], 0
    je .clean
    mov byte [di+FS_DIRTY], 0
    call fm_vp_set              ; BX = the window: everything below reads the
    mov di, [fm_vp]             ; acting window through the published block
    mov al, [di+FS_DRV]
    cmp al, [disk_drive]
    jne .load
    mov dx, [di+FS_CWD]
    cmp dx, [dsk_cwd]
    jne .load
    cmp byte [dsk_lstale], 0    ; a quiet mount leaves the globals empty and
    jne .load                   ; owed (SPEC.md 18.9) - not a listing to take
    cmp byte [dsk_mntok], 0     ; and they have to have come from a mount
    je .load                    ; that actually worked
    call fmv_take               ; the globals ARE this folder: pure memory
    jmp short .drawn
.load:
    mov al, [di+FS_DRV]         ; they are not, so re-list where we stand -
    mov dx, [di+FS_CWD]         ; exactly what Refresh costs (SPEC.md 22)
    call fmv_load
.drawn:
    cmp ch, 0
    je .owed                    ; the caller is drawing this window
    mov si, bx
    call fm_repaint             ; frontmost, so nothing is on top of it and
                                ; no clip region is needed (SPEC.md 11.3)
.owed:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret
.clean:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.none:
    clc
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
    call KERNEL_SEG:cw_dsk_relist   ; PAY THE DEBT BEFORE ADOPTING ANYTHING.
                                ; Every window below takes the global snapshot
                                ; by memory copy (fmv_take), so if the write
                                ; that got us here DEFERRED its rebuild
                                ; (SPEC.md 18.4) the thing being broadcast is
                                ; disk_nfiles = 0 - and it lands in the cache
                                ; of every window on this folder, the acting
                                ; one included, which its caller then repaints
                                ; from. Measured: deleting one file out of a
                                ; Disk window redrew it as 'Drive B: 0 files'.
                                ; dsk_relist is idempotent and costs nothing
                                ; when nothing is owed.
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
    call fmv_take               ; adopt the snapshot: pure memory
    cmp di, [fm_vp]             ; the ACTING window - and only it - is about
    jne .next                   ; to be repainted by the caller that reached
    mov byte [di+FS_DIRTY], 0   ; fm_edit_commit, so its refresh debt (22.8)
                                ; is settled here. A sibling gets its cache
                                ; and keeps the debt: nothing redraws it, so
                                ; its pixels are still the folder as it was
.next:
    add si, I_RECSZ
    loop .scan
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_take - make one window's cache BE the current global snapshot (22.1)
; in:  DI = state block; the globals are already the new truth and are this
;      window's own (drive, folder) - the caller has compared them
; out: nothing (all registers preserved)
;
; The memory half of a re-list, with no mount in it: fmv_load does the same
; four stores around a dsk_chdir. Factored out because fmv_bcast and
; fm_focus (SPEC.md 22.8) both need exactly this and a second copy of it is
; a second place that can disagree about what a fresh cache means.
; -----------------------------------------------------------------------------
fmv_take:
    push ax
    mov ax, [disk_nfiles]
    mov [di+FS_N], ax
    mov byte [di+FS_MOK], 0     ; the mount's own verdict, which is what
    cmp byte [dsk_mntok], 0     ; fmv_load records too - after a successful
    je .listed                  ; write it is always 1 (SPEC.md 18.4)
    mov byte [di+FS_MOK], 1
.listed:
    mov word [di+FS_SEL], 0xFFFF    ; the listing was rebuilt: indices into
    mov word [di+FS_SCRL], 0        ; the old one mean nothing
    call fmv_store
    call fm_measure             ; a write moved both figures: the file that
                                ; arrived is in the folder AND off the free
                                ; list, so neither survives the copy
    pop ax
    ret
section .text                   ; DATA, so back to the kernel segment: cold code
                                ; reads it through DS (SPEC.md 2.6)

; --- has the file manager a refresh to spend? (SPEC.md 22.8) ----------------
; Set when a file operation marks a window (fmv_mark) and when the active
; window changes (menu_activate), read once per UI pass. It lives in .text
; with a real initialiser rather than in .bss, because menu_activate runs
; from the first wm_show of the boot and -f bin zeroes nothing.
fm_fchk:    db 0


; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
fm_tpl:     dw 110, 80, 320, 200, fm_ttl, fm_paint, fm_onkey, fm_onclick
                                ; the three callbacks are the RESIDENT thunks,
                                ; not the _x bodies: wm_draw_win and ui.inc
                                ; dispatch through this record with a near
                                ; call, from .text (SPEC.md 2.6)
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
fm_stattab:   dw fm_s_empty, fm_s_dskerr, fm_s_badpkg, fm_s_toobig, fm_s_ldfail
              dw fm_s_nomem
LD_NSTAT      equ 6             ; LD_OK .. LD_ENOMEM. The bound toast_say
                                ; tests, and it is DERIVED FROM THE TABLE
                                ; rather than typed beside the enum, because
                                ; the table is what an out-of-range code walks
                                ; off the end of (SPEC.md 22.9's silent ladder)

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

; --- the status-line input prompts (SPEC.md 22) ------------------------------
; The delete line is the ONLY thing between a menu click and a file that is
; gone, so it names the file, states the key that says yes and states the key
; that says no. Worst case it is 'Delete ' + 12 + 18 = 37 characters, which
; fits the default window's 38-char status line whole and truncates from the
; right - which is why the name and the word Enter come before Esc.
;
; A FOLDER gets a different tail, because Delete on one now removes
; everything under it (SPEC.md 18.6) and the line is the only place that can
; say so. It costs nine characters, so a maximal 12-character folder name
; overflows the default width by a few and clips the tail of 'Esc=no'; that
; is the right thing to lose, and widening the window shows it whole.
fm_s_newfld:  db 'New folder: ', 0
fm_s_renam:   db 'Rename ', 0
fm_s_rento:   db ' to: ', 0
fm_s_reppr:   db 'Replace ', 0     ; the overwrite question (SPEC.md 22.3),
fm_s_repq:    db '?', 0             ; on TWO lines: 'Replace NAME?' and the
fm_s_repk:    db 'Enter=yes  A=all  Esc=stop', 0  ; three answers are 45
                                    ; characters together and 38 fit the
                                    ; default window, which is resizable
                                    ; SMALLER still - so the question takes
                                    ; the row above the status line for as
                                    ; long as it is up
fm_s_delpr:   db 'Delete ', 0
fm_s_delq:    db '? Enter=yes Esc=no', 0
%ifndef KERN_SMALL
fm_s_fmtpr:   db 'Format ', 0       ; 'Format A: as 360K? Enter=yes Esc=no',
fm_s_fmtas:   db ': as ', 0         ; which is 35 characters at its longest
fm_s_fmtq:    db 'K? Enter=yes Esc=no', 0    ; and so inside FM_HDRMAX
fm_s_fmtok:   db 'Formatted ', 0    ; ...and the verdict, SPEC.md 60.6's own
                                    ; reasoning: a format is seconds of floppy
                                    ; the user waited for, so success speaks
                                    ; rather than only failure
%endif
fm_s_delq2:   db ' +contents? Enter=yes Esc=no', 0   ; ...for a folder
fm_s_size:    db 'Size ', 0         ; ...and the resting status line, which
fm_s_free:    db '   Free ', 0      ; carries both figures (SPEC.md 22.7)
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
; The four titles are laid out at x 120/168/232/280 and end at 343, against
; menu_relayout's MENU_CLK_HX limit of 432: 89px of slack, so no title can
; be silently dropped. Item strings do not enter that arithmetic at all.
; (SPEC.md 12.9 snapped the bar's layout to 8px cells and widened
; MENU_TITLE_PAD to 16, which cost 18px of that slack and moved every number
; in this paragraph.)
; -----------------------------------------------------------------------------
fm_s_mfile:   db 'File', 0
fm_s_medit:   db 'Edit', 0
fm_s_mfold:   db 'Folder', 0
fm_s_mview:   db 'View', 0
fm_s_mbuilt:  db 'Builtins', 0   ; the SAME menu as Locator's, with the
                                ; same items in the same order (SPEC.md
                                ; 12.3.1) - which is the whole point of
                                ; the rename

fm_s_open:    db 'Open', 0
fm_s_mkdir:   db 'New Folder...', 0
fm_s_rename:  db 'Rename...', 0
fm_s_delete:  db 'Delete', 0
fm_s_cut:     db 'Cut', 0
fm_s_copy:    db 'Copy', 0
fm_s_paste:   db 'Paste', 0
fm_s_pastin:  db 'Paste Into', 0

; The rule between Delete and Format, and the item it separates (SPEC.md
; 22.12). A rule is a MENU_DIS item and nothing cleverer: menu_hover never
; lands on one, which is exactly what a separator wants, and it costs the
; menu system no new concept. It is as wide as the widest item in the menu
; so it reads as a rule rather than as a short dash.
;
; Format has a MENU_DIS twin because whether a disk can be formatted is a
; stable, cheap fact - this window's mount failed and this volume is a
; floppy - which SPEC.md 47 rule 5 says to GREY rather than to refuse.
; fm_bar_gate does the swap on the press that opens the bar, exactly as
; ui_loc_gate does for Close Window and for its reason: nothing relays the
; bar out when a mount verdict changes.
%ifndef KERN_SMALL              ; kern_big only (SPEC.md 18.96's foot)
fm_s_fsep:    db MENU_DIS, '--------------', 0
fm_s_format:  db 'Format Disk...', 0
fm_s_formatx: db MENU_DIS, 'Format Disk...', 0
%endif
fm_s_refr:    db 'Refresh', 0
fm_s_up:      db 'Up One Folder', 0
fm_s_rootf:   db 'Root Folder', 0
fm_s_drva:    db 'Drive A:', 0
fm_s_drvb:    db 'Drive B:', 0
fm_s_aslist:  db 'as List', 0
fm_s_asicon:  db 'as Icons', 0
fm_s_newwin:  db 'New Window', 0
fm_s_opennew: db 'Open in New Window', 0

fm_items_file:
    dw fm_s_open, fm_s_mkdir, fm_s_rename, fm_s_delete
%ifndef KERN_SMALL
    dw fm_s_fsep, fm_s_format   ; fm_bar_gate rewrites this last word
FM_IFMT     equ 5               ; ...at this item index, which is also
                                ; FMC_FORMAT - FMC_OPEN, and is written down
                                ; once rather than being counted at the store
FM_NFILE    equ 6               ; items in the File menu
%else
FM_NFILE    equ 4               ; kern_small: no rule and no Format
%endif
fm_items_edit:
    dw fm_s_cut, fm_s_copy, fm_s_paste
fm_items_fold:
    dw fm_s_newwin, fm_s_opennew
    dw fm_s_refr, fm_s_up, fm_s_rootf, fm_s_drva, fm_s_drvb
fm_items_view:
    dw fm_s_aslist, fm_s_asicon
fm_items_built:
    dw menu_s_timer, menu_s_bounce, menu_s_disk

fm_menus:
    dw menu_loc_name            ; AM_NAME: still 'Locator' (SPEC.md 12.3)
    dw fm_oncmd                 ; AM_ONCMD: our own handler - the resident
                                ; thunk, like fm_tpl's three above
    dw 5                        ; AM_COUNT (= MENU_APPMAX, used exactly)
    dw fm_s_mfile, fm_items_file, FM_NFILE  ; 'Close Window' retired: the app-name
                                        ; cell carries Close now (SPEC.md
                                        ; 12.7), and a Disk window OWNS that
                                        ; cell - unlike the bare desktop,
                                        ; where [menu_win] is 0, no cell
                                        ; exists and Locator's own File menu
                                        ; keeps its Close Window as the only
                                        ; way to close from the bar
    dw fm_s_medit, fm_items_edit, 3      ; Cut/Copy/Paste were File's until
                                        ; SPEC.md 22.12 needed a sixth item
                                        ; there. Their own menu is where
                                        ; every other application in this
                                        ; tree - and the system this one is
                                        ; modelled on - has always put them
    dw fm_s_mfold, fm_items_fold, 7
    dw fm_s_mview, fm_items_view, 2
    dw fm_s_mbuilt, fm_items_built, 3
section .cold                   ; ...and back to cold code


; -----------------------------------------------------------------------------
; files_open - CMD_FILES dispatch entry (ui.inc); no lock held on entry
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; Opens where the volume already is - ([disk_drive], [dsk_cwd]) - and lets
; fm_choose decide whether that means fronting a window, creating one, or
; moving one (SPEC.md 22.1).
; -----------------------------------------------------------------------------
files_open_x:
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
files_open_drive_x:
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
    call KERNEL_SEG:cw_gfx_lock ; front + un-minimize, exactly as app_launch's
    and byte [di+I_FLAGS], 0xFE ; own at-cap path does (SPEC.md 29.4)
    mov bx, [di+I_WIN]
    call KERNEL_SEG:cw_wm_show
    call KERNEL_SEG:cw_gfx_unlock
    ret
.notopen:
    call fm_count               ; CX = live file-manager instances
    cmp cx, FM_MAXWIN
    jae .inplace
    call fm_seed                ; park the target for fm_kinit
    mov al, KIND_FILES
    call KERNEL_SEG:cw_app_launch ; was a tail jmp; it takes its own locks
    ret                             ; (the far shim cannot be jumped through)
.inplace:
    call KERNEL_SEG:cw_gfx_lock
    call fm_front_win           ; BX = the frontmost visible one
    jc .unlock                  ; every window minimized: nothing to move
    call fm_vp_set
    mov di, [fm_vp]
    call fmv_load               ; AL = drive, DX = cwd, DI = block
    call fm_settitle            ; SI = the name, still untouched
    mov si, bx
    call fm_repaint
.unlock:
    call KERNEL_SEG:cw_gfx_unlock
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
; fm_front_win - the frontmost VISIBLE file-manager window
; in:  nothing
; out: CF = 0 and BX = window ptr; CF = 1 = none. All else preserved.
;
; "Is this a file-manager window" is the W_MENUS test, not an instance walk:
; fm_kinit is the only thing in the kernel that ever stores fm_menus, so the
; comparison is exact and costs one word.
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
    call KERNEL_SEG:cw_wm_idx2ptr ; BX = window ptr
    test word [bx+W_FLAGS], 2   ; visible?
    jz .cont
    cmp word [bx+W_MENUS], fm_menus
    je .found
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

%ifndef KERN_SMALL              ; kern_big only (SPEC.md 18.96's foot)
; -----------------------------------------------------------------------------
; fm_bar_gate - point Format Disk... at its live or disabled string, just
;               before the bar can drop a menu (SPEC.md 22.12, 47 rule 5)
; in:  nothing; [menu_win] names whoever owns the bar
; out: nothing (all registers preserved)
;
; ui_loc_gate's twin, called from the same place and for exactly its reason:
; nothing relays the bar out when a window's mount verdict changes -
; menu_check hands OWNERSHIP over, but a set's item list is static and would
; keep whatever it last held. So the swap happens on the press that opens the
; bar, where one window is relevant and the answer is two compares.
;
; It runs on every application's press, not only a Disk window's, and that is
; deliberate rather than sloppy: the default is the DISABLED string, so a bar
; owned by anything else leaves the item greyed, which is what fm_items_file
; should carry when no Disk window is looking at an unformatted disk. Getting
; here with the live string left over from a window that has since closed is
; the failure this ordering exists to make impossible.
;
; NOT called before a context menu (SPEC.md 12.4): Format is not on one. If
; it ever is, this is the routine to call there too - and the descriptor note
; above fm_ctx_file is the argument for why that is allowed.
; -----------------------------------------------------------------------------
fm_bar_gate_x:
    push ax
    push bx
    push di
    mov ax, fm_s_formatx        ; greyed unless every test below passes
    mov bx, [menu_win]
    or bx, bx
    jz .set                     ; Locator owns the bar: no window, no disk
    cmp word [bx+W_MENUS], fm_menus
    jne .set                    ; not a Disk window (fm_front_win's test, and
                                ; exact for its reason - fm_kinit is the only
                                ; thing that ever stores fm_menus)
    call KERNEL_SEG:cw_inst_win_owner   ; DI = its instance record
    or di, di
    jz .set
    mov di, [di+I_SPTR]
    call fm_fmt_ok
    jc .set
    mov ax, fm_s_format
.set:
    mov [fm_items_file + FM_IFMT*2], ax
    pop di
    pop bx
    pop ax
    ret
%endif                          ; KERN_SMALL: no formatter

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
files_refresh_x:
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_wm_paint_all
    call KERNEL_SEG:cw_gfx_unlock
    ret

; -----------------------------------------------------------------------------
; fm_win_of - the visible window that owns the state block in [ld_pwin]
; in:  nothing ([ld_pwin])
; out: BX = its window ptr, CF = 0; CF = 1 = no visible window owns it
; clobbers: nothing else (flags)
;
; The reverse of fm_vp_set, and it exists because [ld_pwin] is the poster's
; STATE BLOCK (SPEC.md 22.1) - the loader needs the block, files_poster
; needs the window. Walking wm_zord rather than wm_wins means a window that
; has been closed or hidden since the load was posted simply is not found,
; which is the answer that path wants anyway.
; -----------------------------------------------------------------------------
fm_win_of:
    push ax
    push cx
    push si
    push di
    mov si, wm_zord
    mov cl, [wm_zn]
    xor ch, ch
    jcxz .no
.scan:
    mov al, [si]
    inc si
    call KERNEL_SEG:cw_wm_idx2ptr ; BX = window ptr; CX preserved
    test word [bx+W_FLAGS], 2
    jz .next
    push cx
    call KERNEL_SEG:cw_inst_win_owner ; DI = its instance record, or 0
    pop cx
    or di, di
    jz .next
    mov di, [di+I_SPTR]
    cmp di, [ld_pwin]
    je .found
.next:
    loop .scan
.no:
    pop di
    pop si
    pop cx
    pop ax
    xor bx, bx
    stc
    ret
.found:
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; files_poster - repaint JUST the window that posted a load (loader_run)
; in:  nothing ([ld_pwin]); no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
;
; The window that posted the load has "Loading..." in its status line, drawn
; because [ld_pending] was set (SPEC.md 22). The load clears it, and since
; SPEC.md 11.90 the successful path's wm_show draws only the window it just
; put up - so that one line would sit there stale until something else
; repainted. This is the cheap correction: one window's content, and the
; SPEC.md 11.3 clip region is what makes it safe to draw a window that the
; new one may now be sitting on top of.
;
; A poster-less load (a package asking for one, SPEC.md 22.1) falls back to
; the whole screen, because there is no window to aim at. A pending retitle
; does NOT: this is one of the two places that can spend one, and it is spent
; BEFORE the clip is armed - wm_title_set does its own region arithmetic on
; the title strip, and would otherwise be clipped to the content rect that
; strip is not inside.
; -----------------------------------------------------------------------------
files_poster_x:
    push ax
    push bx
    push si
    cmp word [ld_pwin], 0
    je .full
    call fm_win_of              ; [ld_pwin] is a STATE BLOCK, not a window
    jc .full                    ; ptr - its window has gone since it posted
    call KERNEL_SEG:cw_gfx_lock
    call fm_title_flush
    call KERNEL_SEG:cw_wm_clip_set ; CF = 1: none of it is visible any more
    jc .unlock
    mov si, bx
    call fm_status_only         ; the load changed ONE line of this window;
    jnc .unlock                 ; only a clip edge across it forces the rest
    call fm_repaint
.unlock:
    call KERNEL_SEG:cw_gfx_unlock ; ...which also disarms the clip
    jmp short .out
.full:
    call files_refresh_x
.out:
    pop si
    pop bx
    pop ax
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
    mov ax, [di+FS_N]
    mov [fm_lnf], ax
    mov ax, [di+FS_SCRL]
    mov [fm_lscr], ax
    mov ax, [di+FS_SEL]
    mov [fm_lsel], ax

    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
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
fm_paint_x:
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
fm_onclick_x:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, si                  ; publish the clicked window BEFORE anything
    call fm_vp_set              ; reads or writes per-window state

    mov di, [fm_vp]             ; was an editor line up in THIS window? ending
    mov al, [di+FS_EDIT]        ; one rewrites the status line, and that is the
    mov [fm_wased], al          ; only thing besides the two selection bands a
                                ; row click can change (SPEC.md 22.2)
    call fm_edit_end            ; clicking away abandons a half-typed name
    mov al, [fm_wased]          ; an ended editor line rewrites the status
    mov [fm_statowed], al       ; LINE and changes nothing else on this
                                ; window. It used to be ONE question with TWO
                                ; causes, the other being a retired verdict,
                                ; and verdicts are toasts now (SPEC.md 60.5).
                                ; The cheap paths below draw no text
                                ; at all, so they pay one fm_status_only for
                                ; it - where each used to escalate to a whole
                                ; repaint instead, which is what made clicking
                                ; a row a SECOND time cost a window

    mov [fm_pressx], cx         ; the ABSOLUTE point, banked before the
    mov [fm_pressy], dx         ; conversion below eats it: a drag tracks in
                                ; screen coordinates and may end in another
                                ; window entirely (SPEC.md 22.4)
    mov bx, si
    push cx                     ; save click x
    mov cx, dx                  ; CX = click y
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
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
    call fm_reload              ; re-list where we ARE, folder included
    jmp .redraw                 ; (under the gfx lock: stalls painters
                                ;  briefly; accepted by SPEC.md 22)

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
    jnc .sb_track
    jmp .quiet                  ; no thumb: the track is inert, and an inert
                                ; click owes the status line at most
.sb_track:
    sub ax, [fm_cy]             ; thumb top, content-relative
    cmp cx, ax
    jl .sb_pgup
    mov ax, [fm_fit]            ; at/below the thumb top: page down
    jmp short .sb_move
.sb_pgup:
    mov ax, [fm_fit]
    neg ax
    jmp short .sb_move
.sb_up:
    mov ax, -1
    jmp short .sb_move
.sb_down:
    mov ax, 1
.sb_move:                       ; SPEC.md 22.11: the view moves, and the
    call fm_scroll_by           ; header, the buttons and the status line do
    jc .redraw                  ; not. CF = 1 is 11.3's granularity refusal
    jmp .quiet                  ; .quiet pays the status line if this click
                                ; also retired a verdict (22.9), and nothing
                                ; else - a scroll moves no selection

    ; --- the row area ---
.rows:
    mov bx, [fm_vp]             ; BX = the state block from here on, AX the
    call fm_hit                 ; directory index (fm_hit spares BX)
    jc .clear

    push ax                     ; a press on a row may turn into a drag
    push bx                     ; (SPEC.md 22.4), and only the pointer
    mov cx, [fm_pressx]         ; leaving the row decides which
    mov dx, [fm_pressy]
    call fm_drag                ; CF=0 = it WAS a drag, already handled
    pop bx
    pop ax
    jnc .redraw                 ; ...except the repaint: a drop changes this
                                ; window's own listing (a Cut left it) and
                                ; may have armed the replace question

    cmp ax, [bx+FS_SEL]         ; second click on the selected entry?
    jne .select
    mov dx, [ui_click_t]        ; click birth tick (EV_C), not [ticks]:
    sub dx, [bx+FS_CLKT]        ; wrap-safe (SPEC.md 8); queued clicks
                                ; must not collapse into double-clicks.
                                ; PER WINDOW, or a click on row 3 here and
                                ; one on row 3 next door would compose
    cmp dx, FM_DBLCLK
    jae .restamp
    call fm_open_sel            ; double-click: a package goes to the
    cmp word [ld_pending], 0    ; loader, a folder re-lists this window (22)
    je .redraw                  ; folder: fm_go changed the whole listing
    call fm_status_only         ; package: the ONLY thing that changed on
    jnc .out                    ; screen is 'Loading...' in the status line
    jmp short .redraw           ; a clip edge crosses it: whole content

.restamp:                       ; already selected, too slow to be a double
    mov dx, [ui_click_t]        ; click: the entry does not move, so NOT ONE
    mov [bx+FS_CLKT], dx        ; PIXEL changes - all this click does is
    jmp short .quiet            ; re-arm the double-click window (22.2)

.select:
    mov dx, [ui_click_t]
    mov [bx+FS_CLKT], dx
    jmp short .newsel

.clear:                         ; off the rows: the selection goes away, which
    mov ax, 0xFFFF              ; is still just a band. No FS_CLKT stamp -
    cmp ax, [bx+FS_SEL]         ; empty space is not a double-clickable thing
    je .quiet                   ; ...and a second click on it changes nothing

.newsel:
    ; SPEC.md 22.2: two bands change and nothing else does. The old highlight
    ; is XORed off and the new one XORed on, so every row, every icon, the
    ; header, the buttons and the scroll bar stay exactly as they are - and a
    ; double-click's first half no longer flashes the whole window.
    mov dx, [fm_lsel]           ; the mirror still names the OLD selection
    mov [bx+FS_SEL], ax
    mov [fm_lsel], ax
    xchg ax, dx
    call fm_sel_bar             ; off...
    xchg ax, dx
    call fm_sel_bar             ; ...and on
    cmp byte [fm_statowed], 0   ; the bands drew no text, so a line that
    je .out                     ; changed is still owed (22.9)
    call fm_status_only
    jc .redraw                  ; 11.3's granularity refusal
    jmp short .out

.quiet:
    cmp byte [fm_statowed], 0   ; nothing moved, so there are no bands either -
    je .out                     ; but the line may still have changed, and a
    call fm_status_only         ; click on the row ALREADY selected owes that
    jnc .out                    ; and nothing more

.redraw:
    call fm_repaint             ; SI still = window ptr
.out:
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
fm_onkey_x:
    push ax
    push bx
    push dx
    push di

    mov bx, si                  ; publish the window this key belongs to
    call fm_vp_set
    mov di, [fm_vp]

    cmp byte [di+FS_EDIT], 0
    jne .editing                ; naming: nothing below is reachable - and the
                                ; prompt already owns the line, so there is no
                                ; verdict on it to retire
    mov byte [fm_statowed], 0   ; no editor line was up, so this keystroke
                                ; owes the status line nothing. It used to owe
                                ; one whenever it retired a verdict, and the
                                ; debt had to be RECORDED because the scroll
                                ; keys stopped drawing the line (22.11).
                                ; Verdicts are toasts now (SPEC.md 60.5)

    cmp al, 13
    je .enter
    cmp al, 'v'
    je .vtoggle
    cmp al, 'V'
    je .vtoggle
    cmp al, 8                   ; Backspace goes up, the way it does in every
    je .updir                   ; file manager since (SPEC.md 22)
    cmp al, 0x18                ; Ctrl+X / Ctrl+C / Ctrl+V (SPEC.md 22.3).
    je .cut                     ; CONTROL codes, not bare letters: 'c' and
    cmp al, 0x03                ; 'v' are already this window's own shortcuts
    je .copy                    ; and int 16h hands these straight over
    cmp al, 0x16
    je .paste
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
    jc .redraw                  ; it changed the listing or the mode
    call fm_status_only         ; ...otherwise the name being typed lives on
    jc .redraw                  ; the status line and that is ALL that moved.
    jmp .out                    ; CF = 1 is 11.3's granularity refusal

.remount:
    call fm_reload              ; r: re-list, without leaving the folder
    jmp .redraw

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

.cut:
    mov bx, FMC_CUT
    jmp short .cmd
.copy:
    mov bx, FMC_COPY
    jmp short .cmd
.paste:
    mov bx, FMC_PASTE
.cmd:
    push si                     ; fm_docmd repaints and clobbers everything,
    mov [fm_cmdwin], si         ; and SI is still the window pointer .redraw
    call fm_docmd               ; wants (the .updir precedent)
    pop si
    jmp .out                    ; fm_docmd has already repainted

.scr_up:
    mov bx, si                  ; the scroll keys clamp against the live
    call fm_layout              ; layout (fm_fit/fm_total); fm_scroll_by
    mov ax, -1                  ; commits the mirror and draws what moved
    jmp short .scr_move
.scr_dn:
    mov bx, si
    call fm_layout
    mov ax, 1
    jmp short .scr_move
.scr_pgup:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
    neg ax
    jmp short .scr_move
.scr_pgdn:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
.scr_move:                      ; SPEC.md 22.11, the fm_onclick .sb_move path
    call fm_scroll_by
    jc .redraw
    cmp byte [fm_statowed], 0   ; the keystroke may also have retired a
    je .out                     ; verdict (22.9), which nothing above draws
    call fm_status_only         ; now that the header band is left alone
    jc .redraw
    jmp short .out

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
    cmp word [si+16], 2         ; 2 = a folder, 3 = the parent link (19.5):
    jae .folder                 ; both of them navigate, neither is loadable
    cmp word [si+16], 0         ; a DATA file with an association opens in
    jne .package                ; its program instead of reaching the loader
    call KERNEL_SEG:cw_assoc_post ; (SPEC.md 54.4); without one it still goes
    jnc .out                    ; there and still says "Bad package", which
.package:                       ; is the truthful verdict it always was
    inc ax                      ; index + 1: ui.inc runs the loader once the
    mov [ld_pending], ax        ; lock drops (SPEC.md 21)
    mov [ld_pwin], di           ; ...resolved against THIS window (22.1)
    jmp short .out
.folder:
    call fm_stage_name          ; fm_name = the folder's display name, out
    mov ax, bx                  ; of the staged entry while it still exists
    mov si, fm_name
    cmp word [fm_ftype], 3      ; ...except '..', which has no caption worth
    jne .titled                 ; having: the parent cannot be named without
    xor si, si                  ; the grandparent's listing (SPEC.md 22), so
.titled:                        ; it derives one, exactly as fm_c_up does
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
; The body behind the a/b keys and the Folder menu's Drive A: / Drive B: -
; one copy, so a drive CHANGE can never mean two slightly different things.
; Unlike fm_go this is not navigation: it lands at the ROOT deliberately,
; because a disk you just put in the other drive has no meaningful "current
; folder".
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
; fm_reload - re-list this window WHERE IT IS (SPEC.md 22)
; in:  gfx lock held, [fm_vp] published
; out: nothing (all registers preserved)
;
; The body behind the Refresh button, the r key and Folder > Refresh. It used
; to be fm_mount, which meant every refresh silently walked the user back to
; the root - the one thing a refresh must not do, since the reason to press
; it is that the folder you are LOOKING at may have changed. The cwd is
; re-entered through the same validated pipeline a navigation uses, so a disk
; swapped underneath it fails the mount and lands at the root with the write
; gate shut (SPEC.md 19.2) - bounded, and the same answer a bad disk has
; always given.
;
; The title is left alone on purpose. fm_settitle takes the folder's NAME
; from the caller, and a refresh has no caller who knows it: re-deriving it
; would need the parent's listing, i.e. a second whole mount to print a
; string. The caption is already right, because nothing moved.
; -----------------------------------------------------------------------------
fm_reload:
    push ax
    push dx
    push di
    mov di, [fm_vp]
    mov al, [di+FS_DRV]
    mov dx, [di+FS_CWD]
    call fmv_load
    pop di
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
;
; The screen is corrected by wm_title_set (SPEC.md 11.92) and nothing else -
; but not from here. [fm_tdirty] banks the WINDOW whose 16 bytes just moved,
; and the next locked repaint of it spends that through fm_title_flush,
; because this routine's callers are a mixed set: fm_go, fm_mount and fm_view
; hold the gfx lock, fm_kinit runs before the window is ever shown, and
; fmv_sync's rarest path reaches here from ld_run, which deliberately holds no
; lock across a mount. A pointer rather than a flag, so it can never be spent
; on the wrong window.
;
; It used to bank [fm_full] instead, which escalated the next fm_repaint from
; the content to the whole FRAME - the caption being the one part of a window
; a content repaint never touches. That worked, but it redrew the window
; whole, its listing and everything overlapping it, to correct 17 rows. And
; the frame is drawn BEFORE the mount that decides what the caption should
; say, which is what made a deferred escalation necessary at all; a strip the
; window redraws once it has decided has no such ordering.
; -----------------------------------------------------------------------------
fm_settitle:
    push ax
    push cx
    push si
    push di
    mov di, [fm_vinst]
    or di, di
    jz .out                     ; unowned window: nothing to retitle
    mov ax, [di+I_WIN]          ; inst_bind_win runs before KD_INIT, so this
    mov [fm_tdirty], ax         ; is already valid on the very first call
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

; -----------------------------------------------------------------------------
; fm_title_flush - spend a pending retitle on the screen
; in:  nothing ([fm_tdirty]); caller HOLDS the gfx lock
; out: nothing (all registers preserved)
;
; Called from the two places that repaint a Disk window under the lock -
; fm_repaint and files_poster - and from nowhere else. W_TITLE already points
; at the bytes fm_settitle rewrote, so wm_title_set is told to leave it alone
; (AX = 0) and just correct the strip.
; -----------------------------------------------------------------------------
fm_title_flush:
    push ax
    push bx
    mov bx, [fm_tdirty]
    or bx, bx
    jz .out
    mov word [fm_tdirty], 0
    xor ax, ax
    call KERNEL_SEG:cw_wm_title_set
.out:
    pop bx
    pop ax
    ret
section .text                   ; DATA, so back to the kernel segment: cold code
                                ; reads it through DS (SPEC.md 2.6)


; =============================================================================
; The menu bar: fm_menus' command handler (SPEC.md 12.2/22)
; =============================================================================

; --- command ids -------------------------------------------------------------
; Contiguous WITHIN a menu, which is what makes fm_oncmd's base + item
; arithmetic work. They are internal constants with no on-disk or cross-
; module meaning, so a menu that gains an item simply renumbers what follows.
; SPEC.md 22.12 renumbered every one of these, which the paragraph above is
; the licence for: File gained a separator and Format, and Cut/Copy/Paste
; moved to an Edit menu of their own. FMC_CLOSEW went with the renumber - it
; had been kept at 7 only so that the ids below it would not shift, and that
; is not a reason when they are all shifting anyway.
FMC_OPEN    equ 0               ; File
FMC_MKDIR   equ 1
FMC_RENAME  equ 2
FMC_DELETE  equ 3
FMC_FMTSEP  equ 4               ; the RULE between Delete and Format. It is a
                                ; MENU_DIS item, so menu_hover can never land
                                ; on it and this id can never be dispatched -
                                ; but it occupies an item index, so it needs
                                ; an id and a fm_jmp entry all the same, and
                                ; that entry is a bare `ret` because a body
                                ; may not assume which surface picked it
FMC_FORMAT  equ 5
FMC_CUT     equ 6               ; Edit
FMC_COPY    equ 7
FMC_PASTE   equ 8
FMC_NEWWIN  equ 9               ; Folder
FMC_OPENNEW equ 10
FMC_REFRESH equ 11
FMC_UP      equ 12
FMC_ROOT    equ 13
FMC_DRVA    equ 14
FMC_DRVB    equ 15
FMC_LIST    equ 16              ; View
FMC_ICONS   equ 17
FMC_TIMER   equ 18              ; Builtins (SPEC.md 12.3.1) - the same three
FMC_BOUNCE  equ 19              ; commands Locator's own Builtins menu has,
FMC_DISK    equ 20              ; in the same order. Restart is NOT among them
                                ; any more: it moved to the System menu, which
                                ; is cell 0 of every application, so a Disk
                                ; window reaches it there like everything else
FMC_PASTEIN equ 21              ; no menu CELL of its own: the context menu's
                                ; "paste into the folder under the pointer"
                                ; (SPEC.md 22.3), which the bar cannot offer
                                ; because the bar has no pointer. It sits
                                ; PAST every menu's range so no base + item
                                ; sum can reach it by accident.
FMC_COUNT   equ 22

FM_NMENUS   equ 5               ; = fm_menus' AM_COUNT = MENU_APPMAX
fm_menu_base:
    db FMC_OPEN, FMC_CUT, FMC_NEWWIN, FMC_LIST, FMC_TIMER

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
    dw fm_c_nop                 ; the separator - unreachable through the bar
                                ; (MENU_DIS), and a `ret` rather than a hole
                                ; because fm_docmd's bound is FMC_COUNT and
                                ; every id below it must land somewhere
%ifndef KERN_SMALL
    dw fm_c_format              ; inline (probes the disk under the lock, like
                                ;         Refresh; arms the CONFIRM line, and
                                ;         the disk is WRITTEN at Enter -
                                ;         SPEC.md 22.12)
%else
    dw fm_c_nop                 ; kern_small has no formatter, and the ID
                                ; STAYS so that every FMC_* below keeps its
                                ; number in both builds - the two kernels
                                ; differ by two menu items, not by a
                                ; renumbering nobody can see
%endif
    dw fm_c_cut                 ; inline (the clipboard is a memory write)
    dw fm_c_copy                ; inline (ditto)
    dw fm_c_paste               ; inline (disk I/O under the lock, like
                                ;         Refresh and navigation already are;
                                ;         SPEC.md 22.3)
    dw fm_c_newwin              ; POSTED - app_launch takes the lock (29.4)
    dw fm_c_opennew             ; POSTED - ditto
    dw fm_c_refresh             ; inline (fmv_load, like the button)
    dw fm_c_up                  ; inline (fmv_sync + dsk_dotdot + fmv_load)
    dw fm_c_root                ; inline (fmv_load at the root)
    dw fm_c_drva                ; inline (fmv_load, drive 0)
    dw fm_c_drvb                ; inline (fmv_load, drive 1)
    dw fm_c_list                ; inline (FS_VIEW = 0)
    dw fm_c_icons               ; inline (FS_VIEW = 1)
    dw fm_c_timer               ; POSTED - app_launch takes the lock
    dw fm_c_bounce              ; POSTED - app_launch takes the lock
    dw fm_c_disk                ; POSTED - ui_cmd takes the lock
    dw fm_c_pastein             ; inline (fm_c_paste with another target), forever
section .cold                   ; ...and back to cold code


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
fm_oncmd_x:
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
    mov byte [fm_cmdline], 0    ; assume the whole window until a body says
    call [fm_jmp+bx]            ; otherwise. CALL r/m16 near-indirect: 8086
    mov si, [fm_cmdwin]
    cmp byte [fm_cmdline], 0
    je .full                    ; the command changed the listing, the
    call fm_status_only         ; selection or the mode...
    jnc .out                    ; ...or it only put a PROMPT on the status
.full:                          ; line, which is one line of text. CF = 1 is
    call fm_repaint             ; 11.3's granularity refusal, same as always
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
    cmp word [fm_ftype], 3      ; '..' is a way out of the folder, not a
    je .none                    ; thing in it: it renames and deletes like
    pop ax                      ; the no-selection case, silently (19.5)
    jmp fm_edit_arm
.none:
    pop ax                      ; nothing selected: a no-op, like File > Open
    ret

%ifndef KERN_SMALL              ; kern_big only (SPEC.md 18.96's foot)
; -----------------------------------------------------------------------------
; fm_fmt_ok - may the window whose block is in DI be formatted? (SPEC.md 22.12)
; in:  DI = a state block
; out: CF = 0 yes, CF = 1 no. All registers preserved.
;
; Two stable, cheap facts and nothing that touches a disk, which is what
; makes this SPEC.md 47 rule 5's case - a GREYED item rather than a live one
; that refuses. It is the whole predicate: fm_bar_gate greys from it and
; fm_c_format re-asks it, so the thing that is offered and the thing that
; happens cannot drift (rule 2).
;
; What is deliberately NOT here is whether the medium can be READ. That is
; rule 3's case - the only test is doing the thing - so fm_c_format probes on
; the spot and reports, and an unreadable disk gets a toast rather than a
; greyed menu that cannot say why.
; -----------------------------------------------------------------------------
fm_fmt_ok:
    push ax
    push bx
    cmp byte [di+FS_MOK], 0
    jne .no                     ; it MOUNTED: there is nothing here to format,
                                ; and "erase this perfectly good disk" is a
                                ; different feature with a different question
    mov bl, [di+FS_DRV]
    call KERNEL_SEG:cw_dsk_vol_row
    jc .no
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .no                     ; a driver-backed volume: its own driver owns
    test byte [bx+DV_UNIT], 0x80    ; formatting it (SPEC.md 52.2), and this
    jnz .no                     ; module cannot reach its geometry at all
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

fm_c_format:                    ; File > Format Disk... (SPEC.md 22.12)
    mov di, [fm_vp]
    call fm_fmt_ok              ; the greying is the primary answer; this is
    jc .no                      ; the belt, because a body may not assume
                                ; which surface picked it
    call fmv_sync_x             ; stand on THIS window's volume. The mount
                                ; fails - that IS the premise - and what it
                                ; leaves behind is the thing we need:
                                ; disk_mount sets [disk_drive] before it
                                ; validates anything and its failure path
                                ; does not undo it (SPEC.md 18.3)
    call dskw_fmt_probe         ; AL = the format this medium can take, or
    jc .bad                     ; CF=1 with AX = FERR_*. READS ONLY, so a
                                ; refusal here has changed nothing at all
    mov [fm_fmtrow], al
    mov di, [fm_vp]
    mov al, 5                   ; ...and ask, with the size in the question
    jmp fm_edit_arm
.bad:
    push bx                     ; an unreadable medium is a verdict about what
    push cx                     ; just happened, so it is a toast (60.5) - and
    mov bx, fm_errtab           ; nothing is armed, so the window is left
    mov cx, FM_NERR             ; exactly as it was
    call KERNEL_SEG:cw_toast_say
    pop cx
    pop bx
.no:
    ret
%endif                          ; KERN_SMALL: no formatter

fm_c_cut:                       ; File > Cut - the selection, onto the
    mov al, FCP_CUT             ; clipboard (SPEC.md 22.3)
    jmp short fm_c_clip
fm_c_copy:                      ; File > Copy
    mov al, FCP_COPY
fm_c_clip:
    mov di, [fm_vp]
    mov bx, [di+FS_SEL]
    call fcp_arm                ; CF=1 = nothing selected, or '..': a no-op,
    ret                         ; like every other command with nothing to
                                ; act on

fm_c_paste:                     ; File > Paste - into THIS window's folder
    mov di, [fm_vp]
    mov al, [di+FS_DRV]
    mov bx, [di+FS_CWD]
    jmp short fm_c_pasteto

fm_c_pastein:                   ; context menu > Paste Into - the folder the
    call fm_selfold             ; pointer is on, not the one we are in
    jc fm_c_pasteno             ; the selection is not a folder: a no-op
fm_c_pasteto:
    call fcp_paste              ; AL = FCPS_*; the operation may suspend on
    jmp fm_paste_res            ; an overwrite question (SPEC.md 22.3)
fm_c_pasteno:
    ret

; -----------------------------------------------------------------------------
; fm_selfold - the acting window's selection, if it is a folder
; in:  [fm_vp] published
; out: CF=0 with AL = its drive and BX = its first cluster; CF=1 = there is
;      no selection, or it is not a folder
; clobbers: AX, BX, flags
;
; Type 3 (the parent link, SPEC.md 19.5) counts: pasting onto '..' means
; pasting into the folder above, which is exactly what it looks like.
; -----------------------------------------------------------------------------
fm_selfold:
    push cx
    push si
    push di
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]           ; 0xFFFF (no selection) also fails this
    jae .no
    call fmv_get_dir            ; SI -> the staged entry (SPEC.md 19)
    cmp word [si+16], 2
    jb .no
    mov bx, [si+18]
    mov al, [di+FS_DRV]
    pop di
    pop si
    pop cx
    clc
    ret
.no:
    pop di
    pop si
    pop cx
    stc
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
    call KERNEL_SEG:cw_inst_launch_post ; NOT app_launch: it takes the lock
    ret                             ; we hold
fm_nw_full:
    call KERNEL_SEG:cw_snd_beep ; at the cap app_launch would front an
    ret                         ; existing window and silently drop the seed,
                                ; which reads as "the menu did nothing"
fm_nw_none:
    ret

fm_c_nop:                       ; the File menu's separator (SPEC.md 22.12)
    ret                         ; - MENU_DIS, so nothing can select it

fm_c_disk:                      ; Builtins > Disk - CMD_FILES, exactly what
    mov ax, CMD_FILES           ; Locator's own Builtins menu posts, so the
    call KERNEL_SEG:cw_ui_post_cmd  ; item does the same thing from either bar
    ret

fm_c_timer:                     ; Builtins > Timer
    mov al, KIND_TIMER
    call KERNEL_SEG:cw_inst_launch_post ; NOT app_launch: it takes the lock
    ret                             ; (29.4)

fm_c_bounce:                    ; Builtins > Bounce
    mov al, KIND_BOUNCE
    call KERNEL_SEG:cw_inst_launch_post
    ret

fm_c_refresh:                   ; Folder > Refresh
    jmp fm_reload
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
    call fmv_sync_x             ; dsk_dotdot walks [dsk_cwd], so that had
                                ; better be OUR folder (SPEC.md 22.1)
    call KERNEL_SEG:cw_dsk_dotdot ; AX = the parent's first cluster (19.2)
    jc .none                    ; a missing or corrupt '..': stay put
    xor si, si                  ; the parent cannot be named without the
    jmp fm_go                   ; grandparent's listing (SPEC.md 22)
.none:
    ret

fm_c_root:                      ; Folder > Root Folder
    xor ax, ax
    xor si, si
    jmp fm_go

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
section .text                   ; DATA, so back to the kernel segment: cold code
                                ; reads it through DS (SPEC.md 2.6)


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

; A descriptor is IMMUTABLE .text: four windows share these three, so nothing
; per-window (a selection, a disabled mask) may live in one. There is no
; disabled-item mask at all - a command with nothing to do (Up One Folder at
; the root, Rename with no disk) is the same silent no-op it already is from
; the bar, and one rule beats two.
;
; **The sharing is no longer the reason** (SPEC.md 47.3). ui_loc_gate points
; an item at a MENU_DIS twin on the press that OPENS the menu, when exactly
; one window is relevant, and the same trick works here - a shared descriptor
; only rules out per-window state that has to persist. What is left is a
; feature nobody has asked for rather than an impossibility, so if you are
; here to add it: it is doable, and the paragraph above is not an argument
; against it.
fm_ctx_file:                    ; the row under the pointer is a file
    dw fm_cxi_file, fm_cxc_file, 5
fm_ctx_fold:                    ; ...or a folder, which can also be opened
    dw fm_cxi_fold, fm_cxc_fold, 7
fm_ctx_dir:                     ; empty space: the folder's own commands
    dw fm_cxi_dir,  fm_cxc_dir,  9
fm_ctx_up:                      ; ...or '..' (SPEC.md 19.5), which opens and
    dw fm_cxi_fold, fm_cxc_fold, 2  ; opens-in-a-new-window like any folder
                                    ; and renames and deletes like nothing:
                                    ; the FIRST TWO of the folder's items,
                                    ; the same arrays cut short

fm_cxi_file:
    dw fm_s_open, fm_s_cut, fm_s_copy, fm_s_rename, fm_s_delete
fm_cxc_file:
    db FMC_OPEN, FMC_CUT, FMC_COPY, FMC_RENAME, FMC_DELETE

; Paste is on the FOLDER menu as well as the empty-space one, because
; "paste into that folder" is the thing a right-click on a folder is for -
; and it is the pointer-driven twin of the drag (SPEC.md 22.4). Cut and Copy
; come first so the two lists read the same way down to the divergence.
fm_cxi_fold:
    dw fm_s_open, fm_s_opennew, fm_s_cut, fm_s_copy
    dw fm_s_pastin, fm_s_rename, fm_s_delete
fm_cxc_fold:
    db FMC_OPEN, FMC_OPENNEW, FMC_CUT, FMC_COPY
    db FMC_PASTEIN, FMC_RENAME, FMC_DELETE

fm_cxi_dir:
    dw fm_s_mkdir, fm_s_paste, fm_s_refr, fm_s_up, fm_s_rootf
    dw fm_s_drva, fm_s_drvb, fm_s_aslist, fm_s_asicon
fm_cxc_dir:
    db FMC_MKDIR, FMC_PASTE, FMC_REFRESH, FMC_UP, FMC_ROOT
    db FMC_DRVA, FMC_DRVB, FMC_LIST, FMC_ICONS
section .cold                   ; ...and back to cold code


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
fm_rclick_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [fm_cmdwin], si         ; fm_rcmd acts on this window; fm_docmd's
    mov bx, si                  ; repaint needs it too
    call fm_vp_set

    mov di, [fm_vp]             ; was an editor line up in THIS window? Bank
    mov al, [di+FS_EDIT]        ; it BEFORE ending one, exactly as fm_onclick
    mov [fm_wased], al          ; does - the two selection bands below cannot
                                ; put a rewritten status line back, and this
                                ; flag is per-CLICK, so a stale one from the
                                ; last left-click is not an answer
    call fm_edit_end            ; a right-click abandons a half-typed name
                                ; exactly as a left one does (SPEC.md 22)

    push cx                     ; the popup anchors on the ABSOLUTE press
    push dx                     ; point; everything between here and the pop
                                ; works in content coordinates

    mov bx, si                  ; the same content-relative transform
    push cx                     ; fm_onclick makes: save press x...
    mov cx, dx                  ; CX = press y
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
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
    jb .have
    mov di, fm_ctx_fold         ; a folder has somewhere to open INTO...
    je .have
    mov di, fm_ctx_up           ; ...and the parent link has only that
.empty:
    mov word [di+FS_SEL], 0xFFFF
    mov di, fm_ctx_dir
.have:
    push di                     ; fm_sel_bar preserves everything, but the
                                ; fm_repaint fallback below is the 400-line
                                ; painter and uses every register there is
    mov si, [fm_cmdwin]
    cmp byte [fm_wased], 0      ; an editor line was up: fm_edit_end rewrote
    jne .rc_full                ; the status line, so the whole window owes
    mov ax, [fm_lsel]           ; SPEC.md 22.2, exactly as fm_onclick does it:
    call fm_sel_bar             ; the old band off...
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    mov [fm_lsel], ax           ; keep fm_layout's mirror honest
    call fm_sel_bar             ; ...and the new one on. The selection is the
    jmp short .rc_shown         ; ONLY thing a right-click changes, and this
.rc_full:                       ; still puts it on screen BEFORE the popup, so
    call fm_repaint             ; the save-under captures the new highlight
.rc_shown:
    pop di
    pop dx                      ; the absolute press point again
    pop cx

    mov bx, [di+MC_ITEMS]
    mov ax, [di+MC_NITEM]
    call KERNEL_SEG:cw_menu_popup ; CF = 1 nothing chosen; CF = 0 and AL = the
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
fm_rcmd_x:
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
    cmp byte [di+FS_EDIT], 4
    je .replace
%ifndef KERN_SMALL
    cmp byte [di+FS_EDIT], 5
    je .confirm                 ; Format (SPEC.md 22.12) takes mode 3's
                                ; asymmetry unchanged, and wants it more:
                                ; there is no undo, and the operation is not
                                ; one file but every one of them
%endif
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
    call dskw_char_x            ; upper-cases, or CF=1 = not in the FAT set
    jc .out
.store:
    mov [fm_ebuf+bx], al
    inc bx
    mov byte [fm_ebuf+bx], 0
    mov [fm_elen], bl
    jmp short .lineonly

.back:
    mov bl, [fm_elen]
    xor bh, bh
    or bl, bl
    jz .out
    dec bl                      ; `dec bl` IS `dec bx` here (BH is 0), so BX
    mov [fm_elen], bl           ; is the NEW length and the terminator lands
    mov byte [fm_ebuf+bx], 0    ; on the character just dropped - the -1 that
    jmp short .lineonly         ; used to be here punched a NUL one byte
                                ; EARLIER, so the name displayed short and
                                ; every character typed after a backspace
                                ; landed behind that NUL and never appeared

    ; The replace question (SPEC.md 22.3). Three answers, and the default is
    ; the safe one for the same reason mode 3's is: Enter replaces THIS file,
    ; 'a' replaces every one that follows, and anything else stops the
    ; operation with what has already been copied left in place.
.replace:
    mov ah, 1                   ; Enter: replace THIS one. The answer is built
    cmp al, 13                  ; by loading it outright rather than by
    je .rep_go                  ; counting `inc ah` up from the stop value -
    cmp al, 'a'                 ; the branch for Enter jumped over that first
    je .rep_all                 ; increment, so Enter delivered 0 and STOPPED
    cmp al, 'A'                 ; the operation. The line on screen says
    jne .rep_stop               ; 'Enter=yes' and the paste was cancelled by
.rep_all:                       ; it, which left replacing a single file with
    mov ah, 2                   ; no working answer at all
    jmp short .rep_go
.rep_stop:
    xor ah, ah
.rep_go:
    mov al, ah
    call fcp_answer             ; AL = FCPS_* on the way back
    call fm_paste_res
    jmp short .out

.confirm:
    cmp al, 13
    jne .cancel                 ; anything but Enter means no (SPEC.md 22)
.commit:
    call fm_edit_commit
    jmp short .out
.cancel:
    call fm_edit_end
.lineonly:
    clc                         ; CF = 0: the typed name is ON the status line
    jmp short .pops             ; and NOTHING else on this window moved, so a
.out:                           ; keystroke owes one line, not a repaint
    stc                         ; anything else - commit, cancel, the replace
.pops:                          ; question - changed the listing or the mode
    pop di                      ; and still owes the whole window. `pop` does
    pop bx                      ; not touch flags, so CF survives to the caller
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_samename - is the typed name the one it already has? (SPEC.md 22.10)
; in:  fm_ebuf, fm_onam
; out: CF = 1 if identical; all registers preserved
;
; Seeding the buffer took away the "nothing typed, so Enter cancels" exit -
; the buffer is never empty now - and pressing Enter on an untouched prompt
; would otherwise ask to rename a file to the name it has, which dskw_rename
; answers with 'Name taken'. That reads as a refusal when it was a no-op.
; Uppercase-exact: both sides are 19.1 display names, sanitized alike.
; -----------------------------------------------------------------------------
fm_samename:
    push ax
    push si
    push di
    mov si, fm_ebuf
    mov di, fm_onam
.cmp:
    lodsb                       ; DF=0 per SPEC.md 1
    cmp al, [di]
    jne .diff
    inc di
    or al, al
    jnz .cmp
    pop di
    pop si
    pop ax
    stc
    ret
.diff:
    pop di
    pop si
    pop ax
    clc
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
; out: nothing (all registers preserved); the verdict is said as a toast
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
%ifndef KERN_SMALL
    cmp al, 5
    je .fmt
%endif
    cmp al, 3
    je .del
    cmp byte [fm_elen], 0
    je .done                    ; nothing typed: Enter simply cancels
    call fmv_sync_x             ; act in OUR folder (SPEC.md 22.1)
    cmp al, 2
    je .ren
    mov si, fm_ebuf
    call dskw_mkdir_x           ; CF=1 with AX = FERR_* (SPEC.md 18.5)
    jmp short .verdict
.ren:
    call fm_extkeep             ; typed no extension? keep the old one (22.10)
    call fm_samename            ; ...and a name that came back UNCHANGED is
    jc .done                    ; Enter on an untouched prompt, which used to
                                ; be the empty buffer above and cancelled
    mov si, fm_onam             ; old name...
    mov di, fm_ebuf             ; ...new name; the eleven name bytes and
    call dskw_rename_x          ; nothing else move (SPEC.md 18.4)
    jmp short .verdict
%ifndef KERN_SMALL
    ; Format (SPEC.md 22.12). The row is the one the PROMPT was built from,
    ; not one re-derived now, for the reason Rename and Delete work from
    ; fm_onam rather than from FS_SEL: what the line asked about has to be
    ; what happens.
    ;
    ; No fmv_sync here and none wanted - fm_c_format did it when it armed the
    ; mode, the volume cannot have moved since (the UI task has been inside
    ; this window's keystrokes throughout), and a sync now would cost a
    ; doomed mount of the very disk we are about to replace.
.fmt:
    mov al, [fm_fmtrow]
    call dskw_format
    jc .bad
    mov di, [fm_vp]             ; the volume is a different one now, so the
    mov al, [di+FS_DRV]         ; window is re-listed at its ROOT rather than
    xor dx, dx                  ; at the folder it was pretending to be in
    call fmv_load
    push si
    xor si, si                  ; ...and the caption follows it: 'Disk' is
    call fm_settitle            ; the one name fm_settitle can always derive
    pop si

    ; 'Formatted B:' in the menu bar (SPEC.md 22.12.1). SPEC.md 60.6's
    ; reasoning where it is strongest: a format is seconds of floppy the user
    ; waited for, so the success is worth saying and not only the failure -
    ; and the window behind it is no substitute, because 'Drive B: 0 files'
    ; is a fact about the volume rather than a report of what just happened.
    ;
    ; INLINE, and only because it has one call site: fm_edit_commit has
    ; already saved AX, SI and DI, so a proc here would spend ten bytes of
    ; `.cold` re-saving them - and the cold rung had 51 bytes in it.
    ;
    ; It is said AFTER fmv_load, not before: a mount can arm SPEC.md 12.8's
    ; file-activity widget, which owns these pixels and which toast_show
    ; refuses while it is up - 60.6's own ordering.
    mov al, [di+FS_DRV]
    add al, 'A'
    mov ah, al                  ; banked: fm_scat spends AL and not AH
    mov di, fm_hdrbuf           ; borrowed - toast_show COPIES into toast_buf,
    mov si, fm_s_fmtok          ; and the painter stages this afresh later
    call fm_scat
    mov [di], ah
    mov byte [di+1], ':'
    mov byte [di+2], 0
    push cx
    push es
    mov ax, KERNEL_SEG          ; NOT `push cs`: this is `.cold` (SPEC.md
    mov es, ax                  ; 2.6), so CS here is COLD_SEG, and both
    mov si, fm_hdrbuf           ; fm_hdrbuf and the string are KERNEL_SEG
    xor cx, cx                  ; TOAST_TICKS, the default life
    call KERNEL_SEG:cw_toast_show
    pop es
    pop cx
    jmp short .bcast
%endif
.del:
    call fmv_sync_x
    mov si, fm_onam
    cmp byte [fm_odir], 0
    je .delfile
    call dskw_rmtree            ; a folder AND everything under it (18.6) -
                                ; the confirmation line says so, and there is
                                ; no undo on this system, which is exactly
                                ; why it is a confirmation and not a click
    jmp short .verdict
.delfile:
    call dskw_delete_x
.verdict:
    jc .bad
.bcast:
    call fmv_bcast              ; the new listing reaches every window that
    jmp short .done             ; is showing this folder
.bad:
    push bx                     ; every failure gets visible text (22) - as a
    push cx                     ; TOAST now (SPEC.md 60.5): a verdict is a
    mov bx, fm_errtab           ; report about what just happened, not a fact
    mov cx, FM_NERR             ; about this window, so it wants no per-window
    call KERNEL_SEG:cw_toast_say ; byte, no status-line rung, and no later
    pop cx                      ; interaction to come along and retire it.
    pop bx                      ; BX/CX are pushed because this routine
                                ; promises to preserve every register and
                                ; saves only AX, SI and DI
.done:
    call fm_edit_end
    pop di
    pop si
    pop ax
    ret

FM_DRAGMIN  equ 4               ; pixels the pointer must leave the press
                                ; point by before a click becomes a drag

; -----------------------------------------------------------------------------
; fm_drag - drag an entry onto a folder, which MOVES it (SPEC.md 22.4)
; in:  CX = press x, DX = press y (absolute), SI = the window, AX = the
;      directory index under the press; the gfx lock is HELD (this runs
;      inside W_ONCLICK)
; out: CF=0 = it was a drag and the drop is handled - the caller must not
;      also treat the press as a click; CF=1 = the pointer never left the
;      row, so it was an ordinary click and nothing was drawn
;
; ui_drag's loop, with three differences. The lock is already held, so this
; never calls gfx_lock - it unlocks and re-locks around the yield exactly as
; ui_drag's tracking pass does, and returns holding it. The outline is a row,
; not a window. And the drop target is resolved against EVERY visible window
; rather than the one we started in, which is the whole point: a drag from a
; GAMES window onto a folder in an APPS window is the same move Cut and
; Paste would have made (SPEC.md 22.3).
;
; A drag is a MOVE, always - the Macintosh rule for a drag within one
; volume, and the one this system can honour without a modifier key it has
; no way to report (int 16h shift state is not sampled anywhere else here).
; It TAKES THE CLIPBOARD to do it, arming a Cut the moment the pointer leaves
; the row: a drag is the same operation as Cut-then-Paste (SPEC.md 22.3) and
; giving it a second transport would be a second thing to keep correct.
; -----------------------------------------------------------------------------
fm_drag:
    push ax
    push bx
    push si
    push di
    mov [fm_dgidx], ax
    mov [fm_dgwin], si
    mov [fm_startx], cx
    mov [fm_starty], dx
    mov byte [fm_dgon], 0
    cmp byte [mou_seen], 0      ; ...but there is nothing to disambiguate with
    je .click                   ; no mouse on the machine (SPEC.md 9.6.1). The
                                ; keyboard's button is latched for the whole
                                ; W_ONCLICK dispatch and the pointer cannot
                                ; move until that returns, so this loop could
                                ; only ever end on a SECOND press: an ordinary
                                ; row click would cost two and a double-click
                                ; four. Drag and drop wants a mouse, exactly
                                ; as it did before the keyboard mouse existed

.wait:                          ; not a drag until the pointer leaves the row
    mov di, ui_ev
    call KERNEL_SEG:cw_evq_pop
    jc .noev
    cmp word [ui_ev + EV_TYPE], EVT_MUP
    jne .wait
    jmp .click
.noev:
    test byte [mouse_btn], 1
    jz .click                   ; released without moving: an ordinary click
    mov ax, [mouse_x]
    sub ax, [fm_startx]
    call fm_dgabs
    cmp ax, FM_DRAGMIN
    jae .begin
    mov ax, [mouse_y]
    sub ax, [fm_starty]
    call fm_dgabs
    cmp ax, FM_DRAGMIN
    jae .begin
    ; ...and wait out the tick with the lock FREE (SPEC.md 7.1.3). This pass
    ; drew nothing, so every gfx_unlock/gfx_lock round trip it makes is a
    ; cursor erase and redraw for no reason - ~1.9 ms of the field machine
    ; each, and unpaced this loop turns over as fast as the CPU allows. Every
    ; other loop of this shape already lingers (.track below, ui_drag,
    ; ui_grow) and ui_drag's comment says why in as many words: without it
    ; "the cursor blits inside gfx_unlock/gfx_lock dominate the loop". This
    ; one was the exception, and the exception is what a held button costs.
    ;
    ; The linger sits BETWEEN the unlock and the lock rather than after both,
    ; unlike .track's - .track holds the lock through its wait because its
    ; outline is drawn and must stay lit. Nothing is drawn here, so the free
    ; lock lets the mouse ISR move the cursor and background tasks paint.
    call KERNEL_SEG:cw_gfx_unlock ; the lock may only drop with nothing drawn
    mov ax, [ticks]
.wlinger:
    call KERNEL_SEG:cw_task_yield
    cmp ax, [ticks]
    je .wlinger
    call KERNEL_SEG:cw_gfx_lock
    jmp short .wait

.begin:
    mov bx, [fm_dgidx]          ; the drag IS a Cut, armed here rather than
    mov al, FCP_CUT             ; at the press, so an ordinary click never
    call fcp_arm                ; disturbs a clipboard the user filled
    jc .click                   ; '..' (SPEC.md 19.5) is not draggable
    mov byte [fm_dgon], 1
    call fm_dgmove
    call fm_dgxor               ; the outline is up from here on
.track:
    mov di, ui_ev
    call KERNEL_SEG:cw_evq_pop
    jc .nomup
    cmp word [ui_ev + EV_TYPE], EVT_MUP
    jne .track
    jmp short .drop
.nomup:
    test byte [mouse_btn], 1    ; the fallback ui_drag documents: a dropped
    jz .drop                    ; EVT_MUP still ends the drag by level
    call fm_dgxor               ; erase at the OLD position...
    call KERNEL_SEG:cw_gfx_unlock ; ...only then may the lock drop
    call KERNEL_SEG:cw_task_yield
    call KERNEL_SEG:cw_gfx_lock
    call fm_dgmove              ; ...take the new one...
    call fm_dgxor               ; ...and draw there
    mov ax, [ticks]
.linger:
    call KERNEL_SEG:cw_task_yield
    cmp ax, [ticks]
    je .linger
    jmp short .track

.drop:
    call fm_dgxor               ; erase; the lock stays held to the end
    call fm_dgdrop
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.click:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fm_dgabs - |AX| (module-internal); out AX, clobbers flags
; -----------------------------------------------------------------------------
fm_dgabs:
    or ax, ax
    jns .out
    neg ax
.out:
    ret

; -----------------------------------------------------------------------------
; fm_dgmove - take the pointer's position as the outline's next one
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fm_dgmove:
    push ax
    mov ax, [mouse_x]
    sub ax, 8
    mov [fm_dgx], ax
    mov ax, [mouse_y]
    sub ax, 4
    mov [fm_dgy], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_dgxor - the drag outline, at [fm_dgx]/[fm_dgy]
; out: nothing (all registers preserved)
; fm_dgmove is what advances those two, and the pairing is binding: the
; erase must use the coordinates the draw used or it leaves a rectangle
; behind and paints a second one.
; VRAM-direct for ui_drag_xor's reason (SPEC.md 32): a transient overlay
; XOR-erased before the lock ever drops.
; -----------------------------------------------------------------------------
fm_dgxor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [fm_dgx]            ; the position it was DRAWN at, not where the
    mov bx, [fm_dgy]            ; pointer is now: XOR only erases what it drew,
    mov cx, ax                  ; and the pointer moves between the two calls
    add cx, 95                  ; a 96 x FM_ROW_H ghost: wide enough to read
    mov dx, bx                  ; as "a row", narrow enough not to swamp a
    add dx, FM_ROW_H - 1        ; 320px window
    call KERNEL_SEG:cw_vga_xor_rect_vram ; clips, so the screen edges are free
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_dgdrop - resolve the release point and move the dragged entry there
; out: nothing; clobbers everything (fm_drag saved what it needs)
;
; The target is a folder if the pointer is on one, and the window's own
; folder otherwise - dropping on a file, on a blank row or on the header all
; mean "into the folder you can see". A drop onto the folder we came from is
; a no-op rather than a copy-and-delete of everything into itself.
; -----------------------------------------------------------------------------
fm_dgdrop:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    call KERNEL_SEG:cw_wm_hit   ; BX = the window under the point, 0 = none
    or bx, bx
    jz .out
    push cx                     ; is it a DISK window? fm_win_of goes the
    push dx                     ; other way (block -> window), so the test is
    call KERNEL_SEG:cw_inst_win_owner ; the instance that owns it
    pop dx
    pop cx
    or di, di
    jz .out
    cmp byte [di+I_KIND], KIND_FILES
    jne .out

    push cx                     ; content-relative, for fm_hit
    push dx
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
    pop cx                      ; CX = click y
    sub cx, dx
    pop dx                      ; DX = click x
    sub dx, ax
    push cx
    push dx
    call fm_vp_set              ; BX is still the window: publish it, so
    call fm_layout              ; fm_hit reads THAT window's mirrors
    pop dx
    pop cx

    mov di, [fm_vp]
    mov al, [di+FS_DRV]
    mov [fm_dgdrv], al          ; BANKED: fm_hit clobbers AX, and the target
    mov bx, [di+FS_CWD]         ; drive has to outlive it. Leaving it in AL
                                ; is what made every drop paste to a garbage
                                ; drive, fail FERR_NODISK and look inert
    call fm_hit                 ; AX = directory index, CF=1 = not a row
    jc .have
    push bx
    call fmv_get_dir            ; SI -> the staged entry (SPEC.md 19)
    cmp word [si+16], 2
    pop bx
    jb .have                    ; a file: into the window's folder
    mov bx, [si+18]             ; a folder (or '..'): into THAT one
.have:
    mov al, [fm_dgdrv]
    cmp al, [fcp_drv]           ; the same folder it came from? then the
    jne .go                     ; drag went nowhere and there is nothing
    cmp bx, [fcp_cwd]           ; to do - not a copy of it into itself
    je .out
.go:
    push ax
    push bx
    mov bx, [fm_dgwin]          ; the paste result is reported on the window
    call fm_vp_set              ; the drag STARTED in, which is where the
    pop bx                      ; user is looking
    pop ax
    call fcp_paste
    call fm_paste_res
.out:
    ret

; -----------------------------------------------------------------------------
; fm_paste_res - spend a Cut/Copy/Paste step's answer (SPEC.md 22.3)
; in:  AL = FCPS_*; [fm_vp] published; gfx lock held
; out: nothing; clobbers AX, BX, CX, SI, DI (fm_docmd saved them all)
;
; Three outcomes, and only one of them is a pause. A pause arms FS_EDIT = 4
; on the acting window - the Delete confirmation's mechanism, with a third
; answer - and the name it is asking about goes into fm_onam for the same
; reason Delete's does: the status line has to be able to SAY it, and every
; other copy of a name in this module is painter scratch.
; -----------------------------------------------------------------------------
fm_paste_res:
    push ax
    push di
    cmp al, FCPS_ASK
    je .ask
    call fm_edit_end            ; whatever was armed is spent
    call fmv_reload_all         ; the coherence pass this operation owes
    cmp al, FCPS_ERR
    jne .all
    mov al, [fcp_err]
    mov bx, fm_errtab           ; SPEC.md 60
    mov cx, FM_NERR
    call KERNEL_SEG:cw_toast_say
.all:
    call fmv_repaint_all        ; fmv_reload_all moved every window's CACHE;
    jmp short .out              ; their PIXELS are still the old listing, and
                                ; a paste is the one operation that can change
                                ; three windows at once - so it owes those
                                ; windows, and nothing else on the screen. The
                                ; caller's own repaint then lands on top of
                                ; ours, which is one window against the disk
                                ; I/O this operation has already done
.ask:
    push si
    push cx
    mov si, fcp_fname
    mov di, fm_onam
    mov cx, 13
    call fcp_ncopy
    pop cx
    pop si
    mov di, [fm_vp]
    mov al, 4                   ; the replace question (SPEC.md 22.3)
    call fm_edit_arm
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_repaint_all - put every Disk window's PIXELS where fmv_reload_all just
;                   put its cache (SPEC.md 22.3)
; in:  the gfx lock HELD - both callers are inside one (the drag loop keeps
;      it to the end, and the replace answer is a W_ONKEY callback), which is
;      also why the wm_paint_all this replaced was called bare
; out: nothing (all registers preserved)
;
; One wm_paint_dmg per Disk window (SPEC.md 11.91), and it replaced a
; wm_paint_all. An operation that changed three listings owes three windows
; and whatever overlaps them - not the desktop dither, not the drive zones,
; not every other application on screen. On the measured case, a file dragged
; between two Disk windows, that is 245 glyphs down to 96.
;
; **ONE call over their union, not one call per window**, and the difference
; is not subtle: wm_paint_dmg redraws a marked window WHOLE, so a per-window
; loop redraws each overlapping neighbour once per pass - two overlapping Disk
; windows cost four window paints, which measured WORSE than the wm_paint_all
; it was meant to replace. Inside one call the marking is transitive and every
; window is painted exactly once.
;
; **wm_dmg_wins and not wm_paint_dmg**, because a paste REVEALS nothing: no
; window moved, none was hidden, only contents changed. So there is no
; desktop to put back, no drive zone to redraw and no chrome to correct -
; only windows. wm_dmg_wins is 11.91's mark-and-draw pass on its own, the
; same one wm_dock_under borrows, and skipping the other three steps is what
; takes the union of two Disk windows from "very nearly a wm_paint_all" to
; the windows themselves.
;
; The windows are still redrawn WHOLE rather than in place, and that is the
; granularity rule (SPEC.md 11.3) rather than laziness: fm_repaint is one big
; fill plus ~40 font_strs, the fill clips per pixel and the glyphs per cell,
; so a window partly covered by another would come back with blank rows
; wherever a clip edge crossed it.
;
; The walk is over wm_zord and not inst_tab, because the question is "which
; VISIBLE windows are Disk windows" - inst_win_owner answers it in that
; direction, and the other way round would need a reverse map that does not
; exist.
; -----------------------------------------------------------------------------
fmv_repaint_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [fmv_uany], 0
    mov si, wm_zord
    mov cl, [wm_zn]
    xor ch, ch
    jcxz .done
.scan:
    mov al, [si]
    inc si
    push cx
    push si
    call KERNEL_SEG:cw_wm_idx2ptr ; BX = the window record
    test word [bx+W_FLAGS], 2   ; hidden windows have no pixels to correct
    jz .next
    call KERNEL_SEG:cw_inst_win_owner ; DI = its instance, 0 = unowned
    or di, di
    jz .next
    cmp byte [di+I_KIND], KIND_FILES
    jne .next
    mov si, bx
    call KERNEL_SEG:cw_wm_win_rect ; AX/BX/CX/DX = its frame, inclusive
    call fmv_uadd
.next:
    pop si
    pop cx
    loop .scan
.done:
    cmp byte [fmv_uany], 0
    je .out
    mov ax, [fmv_ux1]           ; seed 11.91's rect directly: the pass below
    mov [wm_dmg_x1], ax         ; is the only part of it we want, so there is
    mov ax, [fmv_uy1]           ; no wm_paint_dmg to do the clamping for us -
    mov [wm_dmg_y1], ax         ; and none is needed, because every rect
    mov ax, [fmv_ux2]           ; folded in came from wm_win_rect and a window
    mov [wm_dmg_x2], ax         ; is already on the screen (SPEC.md 11)
    mov ax, [fmv_uy2]
    mov [wm_dmg_y2], ax
    call KERNEL_SEG:cw_wm_dmg_wins
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fmv_uadd - grow the pending union by the rect in AX/BX/CX/DX (internal)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fmv_uadd:
    cmp byte [fmv_uany], 0
    jne .grow
    mov [fmv_ux1], ax
    mov [fmv_uy1], bx
    mov [fmv_ux2], cx
    mov [fmv_uy2], dx
    mov byte [fmv_uany], 1
    ret
.grow:
    cmp ax, [fmv_ux1]           ; screen coordinates, so unsigned throughout
    jae .g1
    mov [fmv_ux1], ax
.g1:
    cmp bx, [fmv_uy1]
    jae .g2
    mov [fmv_uy1], bx
.g2:
    cmp cx, [fmv_ux2]
    jbe .g3
    mov [fmv_ux2], cx
.g3:
    cmp dx, [fmv_uy2]
    jbe .g4
    mov [fmv_uy2], dx
.g4:
    ret

; -----------------------------------------------------------------------------
; fmv_reload_all - re-list EVERY live Disk window from its own (drive, folder)
; out: nothing (all registers preserved)
;
; The coherence pass a multi-file operation owes (SPEC.md 22.3). fmv_bcast
; reaches only the windows on the CURRENT (drive, cwd), which after a tree
; walk is wherever the walk happened to stop - and a paste changes the
; destination folder, a Cut's source folder, and every folder it created on
; the way down. Up to four mounts, once per operation, against a listing
; that would otherwise be silently wrong.
; -----------------------------------------------------------------------------
fmv_reload_all:
    push ax
    push bx
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
    push cx
    push si
    mov di, [si+I_SPTR]
    mov al, [di+FS_DRV]
    mov dx, [di+FS_CWD]
    call fmv_load               ; resets FS_SEL/FS_SCRL, which is honest:
    mov byte [di+FS_DIRTY], 0   ; every index into the old listing is
    pop si                      ; meaningless now. The refresh debt (22.8)
    pop cx                      ; goes with it: fmv_repaint_all is the other
                                ; half of this pass and puts every visible
                                ; window's pixels where its cache now is
.next:
    add si, I_RECSZ
    loop .scan
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    mov byte [fm_cmdline], 1    ; ...and the prompt IS the whole change: no
                                ; row moves, the selection stays where it is,
                                ; and the header, the icons and the buttons
                                ; are untouched. fm_docmd spends this; a
                                ; caller that is not fm_docmd simply leaves
                                ; it set for the next one to clear, which
                                ; fm_docmd does before every dispatch
    cmp al, 2                   ; RENAME starts from the name the file has
    jne .out                    ; (SPEC.md 22.10). Modes 1 and 4 - New Folder
    call fm_edit_seed           ; and the replace question - start empty
.out:
    ret

; -----------------------------------------------------------------------------
; fm_edit_seed - start a rename from the current name's STEM (SPEC.md 22.10)
; in:  fm_onam = the target's display name (fm_stage_onam has run)
; out: nothing (all registers preserved)
;
; The stem only. An extension is the part of a name a user almost never means
; to change and would otherwise have to retype exactly, so it is left off the
; buffer and fm_extkeep puts it back at commit - which also means typing one
; is how you CHANGE it. Bounded by the same 12 characters fm_editkey allows.
; -----------------------------------------------------------------------------
fm_edit_seed:
    push ax
    push cx
    push si
    push di
    mov si, fm_onam
    mov di, fm_ebuf
    xor cl, cl
.cp:
    lodsb                       ; DF=0 per SPEC.md 1
    or al, al
    jz .end
    cmp al, '.'
    je .end
    mov [di], al                ; mov/inc, never stosb: ES is not DS here
    inc di
    inc cl
    cmp cl, 12
    jb .cp
.end:
    mov byte [di], 0
    mov [fm_elen], cl
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_extkeep - no extension typed? keep the one it had (SPEC.md 22.10)
; in:  fm_ebuf/fm_elen = the typed name, fm_onam = the old one
; out: nothing (all registers preserved); fm_ebuf/fm_elen may grow
;
; Only when the user typed no '.' at all. Typing one - including a trailing
; '.' with nothing after it - is how the extension is changed or dropped, so
; theirs always wins. Bounded at 12, and a name with no room for an extension
; is left as typed for dskw_name83 to judge.
; -----------------------------------------------------------------------------
fm_extkeep:
    push ax
    push bx
    push si
    push di
    call fm_hasdot
    jc .out                     ; the user typed one: theirs wins
    mov si, fm_onam
.find:
    lodsb
    or al, al
    jz .out                     ; the old name had no extension either
    cmp al, '.'
    jne .find                   ; SI now points AT the old extension
    mov bl, [fm_elen]
    xor bh, bh
    cmp bl, 12
    jae .out                    ; no room for the '.', let alone what follows
    mov di, fm_ebuf
    add di, bx
    mov byte [di], '.'
    inc di
    inc bl
.cp:
    lodsb
    or al, al
    jz .term
    cmp bl, 12
    jae .term
    mov [di], al
    inc di
    inc bl
    jmp short .cp
.term:
    mov byte [di], 0
    mov [fm_elen], bl
.out:
    pop di
    pop si
    pop bx
    pop ax
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
; A pending retitle is spent here (SPEC.md 11.92) and costs 17 rows. It used
; to be [fm_full], which escalated this call to the whole FRAME - the window's
; CAPTION lives there, and a content repaint never touches it, so a folder
; change would otherwise leave the title bar naming the folder we just left.
; That escalation redrew this window whole and every window overlapping it,
; and before that it was wm_paint_all: the desktop, both drive zones, the
; chrome and every other window, to correct one string.
; -----------------------------------------------------------------------------
fm_repaint:
    push ax
    push bx
    push cx
    push dx

    call fm_title_flush

    mov bx, si
    call KERNEL_SEG:cw_wm_content ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill ; white-fill content

    mov bx, si
    call fm_draw_core
    call KERNEL_SEG:cw_wm_grow_paint ; the white fill above erased the grow box;
                                ; BX is still the window ptr (SPEC.md 11.1)
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_nfiles - the header's count: this window's listing minus its parent link
; in:  the fm_l* mirrors, [fm_vp]/[fm_vseg] published (fm_layout has run)
; out: AX = entries the user would call files
; clobbers: AX (the output), flags
;
; Asked of the LISTING rather than derived from FS_CWD, because those two can
; disagree for exactly one mount: dsk_synth_up gives an unreadable '..' the
; root as its target rather than dropping the row (SPEC.md 19.5), and a
; window can also be showing a cache taken before it moved. One staged entry
; per paint is cheaper than a mirror that could be wrong.
; -----------------------------------------------------------------------------
fm_nfiles:
    push si
    mov ax, [fm_lnf]
    or ax, ax
    jz .out                     ; an empty listing has no entry 0 to stage
    push ax
    xor ax, ax
    call fmv_get_dir            ; is entry 0 the parent link, or a real one?
    cmp word [si+16], 3
    pop ax
    jne .out
    dec ax
.out:
    pop si
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
    call KERNEL_SEG:cw_icon_draw16 ; body-only table entry (SPEC.md 25)
    jmp .done
.noicon:
    mov si, ico_app16           ; the built-in app icon (library record)
    call KERNEL_SEG:cw_icon_draw
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
    mov ax, [si+4]              ; size dword (entry offsets 20/22)
    mov [fm_fsize], ax
    mov ax, [si+6]
    mov [fm_fsizeh], ax
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
    call fm_nfiles              ; the listing MINUS its parent link (19.5):
    call fm_utoa                ; '..' is a way out, not a file
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
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_gfx_frame              ; color still CBLACK from the header
    mov cx, ax
    add cx, 3                   ; 56px label centered in the 63px rect
    mov dx, [fm_cy]
    add dx, 4
    mov si, fm_s_refresh
    call KERNEL_SEG:cw_font_str

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
    call KERNEL_SEG:cw_gfx_frame
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
    call KERNEL_SEG:cw_font_str
.nobtns:

    call fm_draw_rows           ; the row area, the selection and the bar -
                                ; a proc of its own so a SCROLL can have them
                                ; without the header and the two buttons,
                                ; which a scroll never changes (SPEC.md 22.11)
    call fm_draw_status         ; the status line is a proc of its own so a
                                ; status-ONLY change - the loader's
                                ; 'Loading...' and back again - can cost one
                                ; line instead of a whole content repaint
                                ; (fm_status_only below)

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_rows - the row area, the selection and the scroll bar
; in:  fm_layout already run for the acting window; the content under this
;      band already erased; gfx lock held
; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI, BP (fm_draw_core banked them)
;
; Everything fm_draw_core draws BETWEEN the buttons and the status line, and
; nothing else. Split out for SPEC.md 22.11: a scroll changes no pixel of the
; header, of either button or of the status line, so the paths that scroll
; call this and leave those alone - and cannot drift from the full painter,
; because there is only the one body.
; -----------------------------------------------------------------------------
fm_draw_rows:
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
    xor ax, ax                  ; every visible row of them
    mov [fm_rlo], ax
    mov ax, [fm_shown]
    mov [fm_rhi], ax
    call fm_draw_lrows

.lsel:
    mov ax, [fm_lsel]           ; the selection bar, drawn by the same proc a
    call fm_sel_bar             ; selection CHANGE uses (SPEC.md 22.2)
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
    call KERNEL_SEG:cw_font_width
    mov cx, FMI_CELL_W
    sub cx, ax
    shr cx, 1                   ; centered under the icon
    add cx, [fm_cellx]
    mov dx, [fm_celly]
    add dx, 23
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_font_str

    inc word [fm_vcol]
    mov ax, [fm_vcol]
    cmp ax, [fm_cols]
    jb .icell
    inc word [fm_vrow]
    jmp .irow

.isel:
    mov ax, [fm_lsel]           ; the selection cell, same proc as the bar
    call fm_sel_bar

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
    call KERNEL_SEG:cw_gfx_frame

    mov ax, [fm_sbx1]           ; arrow-cell rules, 11 rows in from each end
    mov bx, [fm_rgt]
    mov dx, [fm_sby1]
    add dx, 10
    call KERNEL_SEG:cw_gfx_hline
    mov dx, [fm_sby2]
    sub dx, 10
    call KERNEL_SEG:cw_gfx_hline

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
    call KERNEL_SEG:cw_gfx_hline
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
    call KERNEL_SEG:cw_gfx_hline
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
    call KERNEL_SEG:cw_gfx_fill_gray

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
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_gfx_frame
.nothumb:
.norows:
    ret

; -----------------------------------------------------------------------------
; fm_draw_lrows - the list rows [fm_rlo, fm_rhi), and nothing around them
; in:  fm_layout already run; the band under those rows already erased; lock
;      held
; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI, BP
;
; The range is an argument because a scroll draws only the rows the blit
; EXPOSED (SPEC.md 22.11), and the full painter draws 0..fm_shown. One loop
; for both, so the two cannot disagree about what a row looks like.
; -----------------------------------------------------------------------------
fm_draw_lrows:
    mov di, [fm_rlo]
.next:
    cmp di, [fm_rhi]
    jae .out
    call fm_draw_lrow
    inc di
    jmp short .next
.out:
    ret

; -----------------------------------------------------------------------------
; fm_draw_lrow - one list row: its icon, its name and its size
; in:  DI = visible row index (below fm_shown); fm_layout already run; the
;      row's band already erased; gfx lock held
; out: nothing; DI preserved
; clobbers: AX, BX, CX, DX, SI, BP
; -----------------------------------------------------------------------------
fm_draw_lrow:
    push di
    mov ax, FM_ROW_H            ; BP = row band top (absolute)
    mul di
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bp, ax
    mov ax, di
    add ax, [fm_lscr]           ; AX = directory index

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
    call KERNEL_SEG:cw_font_str ; name at x = 24

    cmp word [fm_ftype], 2      ; a folder and the parent link have no
    jae .lnosize                ; meaningful size (19), and a column of
                                ; zeroes would only read as one
    mov ax, [fm_fsize]
    mov dx, [fm_fsizeh]
    push di
    mov di, fm_numbuf
    call fm_ultoa_x             ; the full dword: BEVERLY.MOD is 116,085
    pop di                      ; bytes, not 65,535
    mov si, fm_numbuf
    call KERNEL_SEG:cw_font_width ; AX = string width, px
    mov cx, [fm_rgt]            ; right-align ending at cw-22 (clear of
    sub cx, 20                  ; the scroll bar, SPEC.md 22)
    sub cx, ax
    mov dx, bp
    call KERNEL_SEG:cw_font_str
.lnosize:
    pop di
    ret

; -----------------------------------------------------------------------------
; fm_draw_licon - visible list row DI's 16x16 icon, and nothing else
; in:  DI = visible row index; fm_layout already run; gfx lock held
; out: nothing; DI preserved (fm_draw_icon16 preserves everything)
; clobbers: AX, CX, DX - its one caller is fm_scrollpaint's strip loop, whose
;           only live register across it is the row counter
;
; The repair pass for the few columns a scroll blit cannot reach (SPEC.md
; 22.11.1). Its caller erases the strip first and that is NOT belt and
; braces: ico_core's white pass is the icon's own SILHOUETTE (ico_app16's is
; a diamond), not its 16x16 cell, so an icon leaves every pixel the row
; before it lit outside the new outline. Caught by a pixel diff against a
; forced full repaint, which showed the old row's icon edge surviving in a
; three-column stripe.
; -----------------------------------------------------------------------------
fm_draw_licon:
    mov ax, FM_ROW_H
    mul di                      ; (DX is clobbered here and set below)
    add ax, [fm_by1]            ; = fm_cy + FM_ROW_Y0, which fm_scrollpaint
    mov dx, ax                  ; has already worked out. DX = row band top
    mov ax, di
    add ax, [fm_lscr]           ; AX = directory index
    mov cx, [fm_cx]
    add cx, 4
    jmp fm_draw_icon16          ; ...and its ret returns to the loop

; -----------------------------------------------------------------------------
; fm_sel_bar - XOR the selection highlight for ONE directory index
; in:  AX = directory index, 0xFFFF = none; fm_layout already run for the
;      acting window; gfx lock held
; out: nothing (all registers preserved)
;
; Draws nothing when the index names no entry, or is scrolled out of the row
; area, or there is no row area at all - so a caller may hand it a selection
; it has not range-checked, which both of them do.
;
; XOR is what makes SPEC.md 22.2's two-band selection change possible: the
; same call that puts the highlight up takes it back down, so a click that
; moves the selection costs two inverted bands instead of a white fill and
; every row, icon, name, size, button and the scroll bar. Factored out of
; fm_draw_core rather than written twice for the same reason fm_hit was -
; the painter and the click path cannot be allowed to disagree about which
; pixels a selected entry owns, or a "take it down" would leave a fringe.
;
; Correct only because W_ONCLICK fires on the FRONTMOST window alone (SPEC.md
; 13): nothing is on top of it, so what is on screen inside its content is
; exactly what the last paint put there, XOR included. A clip region is never
; armed here and none is needed - and the granularity rule (11.3) does not
; apply either way, because an inversion erases nothing to draw back.
; -----------------------------------------------------------------------------
fm_sel_bar:
    push ax
    push bx
    push cx
    push dx
    cmp ax, [fm_lnf]            ; 0xFFFF (none) also fails this
    jae .out
    cmp byte [fm_lview], 0
    jne .icon

    sub ax, [fm_lscr]           ; list: the whole row band, x 0 .. cw-16
    js .out
    cmp ax, [fm_fit]            ; fit = 0 (no row area) lands here too, and
    jae .out                    ; fit bounds shown, so it is the same test
    mov cx, FM_ROW_H
    mul cx
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = row band top
    mov dx, ax
    add dx, FM_ROW_H - 1        ; y2
    mov ax, [fm_cx]
    mov cx, [fm_rgt]
    sub cx, 15
    call KERNEL_SEG:cw_gfx_xor_fill
    jmp short .out

.icon:
    xor dx, dx
    div word [fm_cols]          ; AX = grid row, DX = column (cols >= 1 per
    mov cx, dx                  ; fm_layout, so this cannot divide by zero)
    sub ax, [fm_lscr]
    js .out
    cmp ax, [fm_fit]
    jae .out
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
    call KERNEL_SEG:cw_gfx_xor_fill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_status_only - repaint one Disk window's status line and NOTHING else
; in:  SI = window ptr; gfx lock held (a clip region may be armed)
; out: CF = 0 drawn; CF = 1 refused - the caller must fall back to a whole
;      repaint. All registers preserved.
;
; This exists for the loader round trip. Posting a load and finishing one
; change exactly one line of text - "Loading..." on, then off again - and both
; used to cost a full fm_repaint: a white fill of the whole content plus every
; row, every icon, the header and the buttons, to correct eight pixel rows.
;
; The refusal is the SPEC.md 11.3 granularity rule, not caution. The erase is
; a fill (per-pixel clipping) and the text is glyphs (whole-cell clipping), so
; a clip edge crossing this rect would erase rows the text could not be put
; back into - the line would go blank rather than stale. wm_clip_test on the
; whole rect answers that in one call, exactly as fr_status does; CF = 1 sends
; the caller back to fm_repaint, which is what it did before this existed.
; -----------------------------------------------------------------------------
fm_status_only:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call fm_layout              ; fm_cx/fm_cy/fm_cw/fm_rgt/fm_staty, and the
                                ; fm_l* snapshot fm_draw_status reads

    mov ax, [fm_cx]             ; the status line's own rect: full content
    mov bx, [fm_cy]             ; width, the 8 rows font_str draws into. The
    add bx, [fm_staty]          ; scroll bar stops at fm_listb, two rows
    mov cx, [fm_rgt]            ; above this, so nothing else lives here
    mov dx, bx
    add dx, 7
    cmp ax, cx
    jg .refuse                  ; degenerate content: nothing to draw into
    cmp bx, dx
    jg .refuse
    call KERNEL_SEG:cw_wm_clip_test ; whole rect visible? (SPEC.md 11.3)
    jc .refuse

    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    call fm_draw_status
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Scrolling (SPEC.md 22.11)
;
; Every one of the eight ways to scroll a Disk window - the two arrow cells,
; the two halves of the track, and Up/Down/PgUp/PgDn - used to end in
; fm_repaint: a white fill of the whole content and then every pixel of it
; back again. That is a header, two framed buttons, a whole scroll bar and a
; status line redrawn to move a list, and on a 4.77MHz machine it is the
; content going blank and filling in a piece at a time. None of those four
; things can change when the view scrolls.
;
; What DOES change is the rows - and mostly they only MOVE. Scrolling by d
; rows leaves fit-d of them showing exactly the same names at a different y,
; which is what gfx_scroll (SPEC.md 5.5) moves in one blit, so a click on the
; down arrow letters ONE row instead of ten. Three tiers, cheapest first:
;
;   fm_scrollpaint  the list view moving by less than a windowful: one blit,
;                   the rows it exposed, the two selection bands and the
;                   thumb. Nothing else on the window is touched at all.
;   fm_rows_only    the icon grid, and any jump longer than the view: the row
;                   band is erased and redrawn, and the header, the buttons
;                   and the status line are still left alone.
;   fm_repaint      the fallback, when a clip region cuts the band - SPEC.md
;                   11.3's granularity rule, exactly as fm_status_only takes
;                   it: a fill clips per pixel and a glyph per whole cell, so
;                   a cut band would come back BLANK rather than stale.
;
; And a scroll that hits an end stop now draws nothing whatever, where it
; used to repaint the window to show the same pixels.
; =============================================================================

; -----------------------------------------------------------------------------
; fm_scroll_by - move one Disk window's view by a signed number of rows
; in:  SI = window ptr, AX = signed row delta; fm_layout has run for this
;      window; gfx lock held
; out: CF = 0 the screen is current; CF = 1 the caller must fm_repaint.
;      All registers preserved.
;
; The ONE entry the eight scroll paths share, so the clamp, the tier choice
; and the write-back cannot drift apart between the mouse and the keyboard.
; fm_clamp_scroll is still the only place the mirror reaches the state block;
; what this adds is the delta the clamp ACTUALLY took, which is what the
; drawing below is a function of - a PgDn two rows from the end moves two.
; -----------------------------------------------------------------------------
fm_scroll_by:
    push ax
    push bx
    push cx
    push dx
    push si                     ; the two tiers below clobber freely and bank
    push di                     ; nothing: this is their only caller, so one
    push bp                     ; prologue serves both (they are 14 bytes of
                                ; .cold apiece otherwise, and the budget is
                                ; measured in 512-byte rungs)
    mov bx, [fm_lscr]           ; where the SCREEN stands
    add [fm_lscr], ax
    call fm_clamp_scroll        ; ...and where the window now does
    mov ax, [fm_lscr]
    sub ax, bx
    mov [fm_sdlt], ax
    jz .same                    ; an end stop: not one pixel changes, where
                                ; this used to repaint the window whole
    cmp byte [fm_lview], 0
    jne .rows                   ; the grid is 22.11.2's exception
    cwd                         ; |d|, the branchless way
    xor ax, dx
    sub ax, dx
    cmp ax, [fm_fit]
    jae .rows                   ; a jump of a windowful or more: every row is
                                ; new, so there is nothing for a blit to move.
                                ; fm_fit = 0 - no row area at all - lands here
                                ; too, and fm_rows_only draws nothing for it,
                                ; so it needs no test of its own
    call fm_scrollpaint
    jnc .done
.rows:
    call fm_rows_only
    jmp short .done
.same:
    clc
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                         ; POP does not touch flags: CF is still the
                                ; answer

; -----------------------------------------------------------------------------
; fm_rows_only - erase and redraw the row band, and NOTHING around it
; in:  fm_layout already run for the acting window; gfx lock held
; out: CF = 0 drawn; CF = 1 refused - the caller must fm_repaint
; clobbers: AX, BX, CX, DX, SI, DI, BP (fm_scroll_by banked them)
;
; The middle tier. It is fm_status_only's shape at the other end of the
; window: one rect, tested WHOLE against any armed clip region (SPEC.md 11.3)
; because the erase clips per pixel and the glyphs per cell, then filled and
; redrawn. What it saves over fm_repaint is the header, both buttons and the
; status line - about thirty glyph cells, and the flash of the whole content
; going white to move a list.
;
; It does NOT re-run fm_layout: fm_clamp_scroll has just written the mirror
; every one of these reads, and re-deriving the rest from an unchanged window
; record would answer with what is already there. Same contract as
; fm_draw_rows, fm_sel_bar and fm_thumb.
; -----------------------------------------------------------------------------
fm_rows_only:
    cmp word [fm_fit], 0
    je .done                    ; no row area: a scroll shows nowhere

    mov ax, [fm_cx]             ; the row band: full content width (the bar
    mov bx, [fm_cy]             ; is inside it and is redrawn with it), from
    add bx, FM_ROW_Y0           ; the header's bottom to the status line's top
    mov cx, [fm_rgt]
    mov dx, [fm_cy]
    add dx, [fm_listb]
    dec dx
    cmp ax, cx
    jg .refuse                  ; degenerate content: nothing to draw into.
                                ; The y span needs no test of its own - fit
                                ; >= 1 puts fm_listb a row or more below 22
    call KERNEL_SEG:cw_wm_clip_test
    jc .refuse

    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    call fm_draw_rows
.done:
    clc
    ret
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; fm_scrollpaint - move the list view with one blit (SPEC.md 22.11.1)
; in:  fm_layout run and [fm_lscr] ALREADY the new position; [fm_sdlt] = the
;      signed row delta taken, non-zero and smaller than [fm_fit]; gfx lock
;      held
; out: CF = 0 drawn; CF = 1 refused - the caller falls back
; clobbers: AX, BX, CX, DX, SI, DI, BP (fm_scroll_by banked them)
;
; gfx_scroll wants byte columns, and a Disk window is not WF_SNAP - its
; content origin is W_X+1, wherever the user dragged it. So the span is
; rounded INWARD at both ends, which is the opposite of what Note Pad does
; (SPEC.md 27.7.2) and for a reason: NP_MARGIN is 8 there, so rounding a text
; band's x1 DOWN still lands inside the content, while a Disk row's icon
; starts four pixels in and rounding down would take the blit outside the
; window altogether - onto the frame, and past it onto whatever is to the
; left. Rounding in leaves two strips this cannot move, and each is answered
; rather than paid for:
;
;   left, up to 7px:  white margin, and at most three columns of the icon at
;                     x+4. 22.11.1's strip pass erases those and re-icons the
;                     rows, and only when the rounding really did reach them -
;                     five window positions in eight need nothing at all.
;   right, up to 7px: always white. The span stops LEFT of the scroll bar, so
;                     the bar's frame, rules and arrows are never disturbed
;                     and only the thumb is redrawn; and a list row's size
;                     column ends at rgt-21, clear of everything the rounding
;                     can leave behind.
;
; The selection is the one thing that spans both strips - its band runs the
; full row width - so it comes OFF before the blit and back ON after, at the
; new row. That is SPEC.md 22.2's two-band move with the blit in the middle,
; and it needs no fixing up because XOR is its own inverse.
; -----------------------------------------------------------------------------
fm_scrollpaint:
    ; --- the band the blit may move ---
    mov ax, [fm_cx]
    add ax, 7
    and ax, 0xFFF8              ; x1, IN to a byte column
    mov [fm_bx1], ax
    mov ax, [fm_rgt]
    sub ax, FM_SB_W-1           ; the scroll bar's left column...
    and ax, 0xFFF8
    dec ax                      ; ...and x2, stopping short of it. A window
    mov [fm_bx2], ax            ; too narrow to hold a whole byte column
                                ; needs no test of its own: it lands x1 > x2
                                ; or x2 off the screen, and gfx_scroll's own
                                ; contract refuses both
    mov ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov [fm_by1], ax
    mov ax, [fm_fit]            ; whole rows only: a content height with a
    mov cl, 4                   ; remainder leaves a sliver below the last
    shl ax, cl                  ; row that no row painter would ever erase
    add ax, [fm_by1]
    dec ax
    mov [fm_by2], ax

    mov ax, [fm_cx]             ; the whole band, tested against an armed
    mov bx, [fm_by1]            ; clip region (11.3) - the blit tests its own
    mov cx, [fm_rgt]            ; rect, but the exposed rows below are wider
    mov dx, [fm_by2]            ; than it and are a fill plus glyphs
    call KERNEL_SEG:cw_wm_clip_test
    jc .refuse

    ; --- the selection comes off where it is now ---
    mov ax, [fm_sdlt]
    sub [fm_lscr], ax           ; ...so fm_sel_bar sees the OLD scroll
    mov ax, [fm_lsel]
    call fm_sel_bar
    mov ax, [fm_sdlt]
    add [fm_lscr], ax

    ; --- and the rows move ---
    mov si, ax
    mov cl, 4
    shl si, cl                  ; SI = dy = d * FM_ROW_H, sign and all
    mov ax, [fm_bx1]
    mov bx, [fm_by1]
    mov cx, [fm_bx2]
    mov dx, [fm_by2]
    call KERNEL_SEG:cw_gfx_scroll
    jc .refuse                  ; it refused: fm_repaint puts the selection
                                ; back with everything else

    ; --- which rows it exposed ---
    mov ax, [fm_sdlt]
    mov bx, [fm_fit]
    or ax, ax
    jl .expup
    mov [fm_rhi], bx            ; scrolled DOWN: the last d rows are new
    sub bx, ax
    mov [fm_rlo], bx
    jmp short .stripck
.expup:
    mov word [fm_rlo], 0        ; scrolled UP: the first |d| are
    neg ax
    mov [fm_rhi], ax

    ; --- the columns the rounding put out of the blit's reach ---
    ; Erased and re-iconed BEFORE the exposed band, so that band's own fill
    ; and row painter cover the rows this skips.
.stripck:
    mov ax, [fm_cx]
    add ax, 4                   ; the icon's left edge
    cmp [fm_bx1], ax
    jle .expfill                ; the blit reached it: nothing stale anywhere
    mov ax, [fm_cx]
    mov cx, [fm_bx1]
    dec cx                      ; the strip, over the whole row band
    mov bx, [fm_by1]
    mov dx, [fm_by2]
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill ; an icon does NOT erase what it lands on: it
                                ; is drawn through a SILHOUETTE mask (the
                                ; generic one is a diamond), so the pixels
                                ; the old row's icon put outside the new
                                ; one's outline survive a redraw
    xor di, di
.strip:
    cmp di, [fm_fit]
    jae .expfill
    cmp di, [fm_rlo]
    jb .stripdo
    cmp di, [fm_rhi]
    jb .stripnext               ; an exposed row is drawn whole below
.stripdo:
    call fm_draw_licon
.stripnext:
    inc di
    jmp short .strip

    ; --- and the rows it exposed, and only those ---
.expfill:
    mov cl, 4
    mov ax, [fm_rlo]
    shl ax, cl
    add ax, [fm_by1]
    mov bx, ax                  ; y1 = the first exposed row's top
    mov ax, [fm_rhi]
    shl ax, cl
    add ax, [fm_by1]
    dec ax
    mov dx, ax                  ; y2 = the last one's bottom
    mov ax, [fm_cx]
    mov cx, [fm_rgt]
    sub cx, FM_SB_W             ; the column left of the bar
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    call fm_draw_lrows

.thumb:
    call fm_sb_thumb            ; the only part of the bar a scroll moves
    mov ax, [fm_lsel]           ; ...and the selection goes back on, at the
    call fm_sel_bar             ; row it now sits in
    clc
    ret
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; fm_sb_thumb - move the scroll-bar thumb, and draw nothing else on the bar
; in:  fm_layout run, [fm_lscr] the new scroll, [fm_sdlt] the delta that got
;      it there; gfx lock held
; out: nothing
; clobbers: AX, BX, CX, DX - its one caller reaches it with nothing live
;
; The thumb's HEIGHT is fit*track/total, which no scroll changes - so a scroll
; TRANSLATES it, and the frame, the two rules, the two arrow glyphs and all
; of the track it did not cover are exactly where they were. Three calls
; instead of the bar's sixteen.
;
; The new thumb is drawn BEFORE the strip it vacated is greyed, which is
; SPEC.md 42.7.1's ordering: for the width of one fill the thumb is briefly
; doubled, and a doubled thumb reads as movement where an absent one reads as
; a blink. A step too small to move it draws nothing at all.
; -----------------------------------------------------------------------------
fm_sb_thumb:
    call fm_thumb               ; where it goes ([fm_lscr] is already new)
    jc .out                     ; no thumb: everything fits, so nothing moved
    mov [fm_thnew], ax
    mov [fm_thh], dx
    mov ax, [fm_sdlt]
    sub [fm_lscr], ax
    call fm_thumb               ; ...and where it was. Its CF is deliberately
    mov bx, [fm_sdlt]           ; not tested: fm_thumb refuses on fm_total
    add [fm_lscr], bx           ; against fm_fit, and a scroll changes
                                ; neither, so it cannot disagree with the
                                ; call above - and the restore has spent the
                                ; flags by here anyway
    cmp ax, [fm_thnew]
    je .out                     ; the track is short enough that this step
                                ; does not move it: draw nothing

    mov bx, ax                  ; BX/DX = the band it is leaving
    mov dx, ax
    add dx, [fm_thh]
    dec dx
    push ax                     ; AX = the old top, wanted after the fills

    mov ax, [fm_rgt]            ; the new thumb: white body, black frame
    sub ax, FM_SB_W-3           ; (= scroll-bar x1 + 2)
    mov cx, [fm_rgt]
    sub cx, 2
    push bx
    push dx
    mov bx, [fm_thnew]
    mov dx, bx
    add dx, [fm_thh]
    dec dx
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_gfx_frame
    pop dx
    pop bx
    pop ax

    cmp ax, [fm_thnew]
    jl .down
    mov ax, [fm_thnew]          ; it moved UP: what it vacated is the strip
    add ax, [fm_thh]            ; below the new one (= new bottom + 1)
    cmp ax, bx
    jle .grey                   ; disjoint: the whole old band is vacated
    mov bx, ax
    jmp short .grey
.down:
    mov ax, [fm_thnew]          ; it moved DOWN: the strip above it
    dec ax
    cmp ax, dx
    jge .grey
    mov dx, ax
.grey:
    cmp bx, dx
    jg .out
    mov ax, [fm_rgt]            ; the track, between the arrow-cell rules
    sub ax, FM_SB_W-2           ; (= scroll-bar x1 + 1)
    mov cx, [fm_rgt]
    dec cx
    call KERNEL_SEG:cw_gfx_fill_gray
.out:
    ret

; -----------------------------------------------------------------------------
; fm_draw_status - stage and draw JUST the status line
; in:  fm_layout already run for the acting window; gfx lock held
; out: nothing (all registers preserved)
;
; Draws only - it does not erase first, because its usual caller
; (fm_draw_core) white-fills the whole content before it runs. The caller
; that needs an erase is fm_status_only, which does its own.
; -----------------------------------------------------------------------------
fm_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- status line at (6, staty): the precedence is binding (SPEC.md 22),
    ;     because every one of these can be true at the same moment. There
    ;     used to be five rungs and two of them were VERDICTS - the FERR_* of
    ;     the last file operation and the LD_* of the last load - which took
    ;     the line until some later interaction retired it. Those are toasts
    ;     now (SPEC.md 60.5): a verdict is a report about what just happened,
    ;     so it belongs to the moment rather than to a window, and it goes
    ;     away on its own. What is left is what the line is FOR - one thing
    ;     that is true now (a load pending), one being typed, and one that
    ;     describes the folder. The header buffer is free by now, so every
    ;     branch stages into it and one truncation serves them all.
    mov di, fm_hdrbuf
    mov si, fm_s_loading        ; 1. a pending load trumps everything
    cmp word [ld_pending], 0
    jne .fs_stat
    mov al, [fm_ledit]          ; 2. the input line, prompt and all - the
    or al, al                   ;    editor IS the status line (SPEC.md 22)
    jz .fs_st_space
    cmp al, 3
    je .fs_st_del
    cmp al, 4
    je .fs_st_rep
%ifndef KERN_SMALL
    cmp al, 5
    je .fs_st_fmt
%endif
    mov si, fm_s_newfld
    cmp al, 1
    je .fs_st_prompt
    mov si, fm_s_renam          ; "Rename NAME to: NEW"
    call fm_scat
    mov si, fm_onam
    call fm_scat
    mov si, fm_s_rento
.fs_st_prompt:
    call fm_scat
    mov si, fm_ebuf
    jmp .fs_stat
.fs_st_rep:
    mov si, fm_s_reppr          ; line one: 'Replace NAME?', in the row area
    call fm_scat                ; (a repaint redraws both, and the question
    mov si, fm_onam             ; is only up between two of them)
    call fm_scat
    mov si, fm_s_repq
    call fm_scat
    mov byte [di], 0
    mov dx, [fm_cy]
    add dx, [fm_staty]
    sub dx, 9
    call fm_stat_line
    mov di, fm_hdrbuf           ; ...and line two, the answers, where every
    mov si, fm_s_repk           ; other status line goes
    jmp short .fs_stat
%ifndef KERN_SMALL
    ; "Format A: as 360K? Enter=yes Esc=no" (SPEC.md 22.12). The SIZE is the
    ; point of naming it: dskw_fmt_probe cannot always tell 360K media from
    ; 720K in a 1.2MB drive, and this line is the one place the user can
    ; catch that before it costs them anything.
.fs_st_fmt:
    push bx
    mov si, fm_s_fmtpr
    call fm_scat
    mov al, [fm_ldrv]
    add al, 'A'
    mov [di], al
    inc di
    mov si, fm_s_fmtas
    call fm_scat
    mov al, [fm_fmtrow]
    call dskw_fmt_row           ; SI = the row it is about to write
    mov ax, [si+DFMT_KB]
    call fm_utoa
    pop bx
    mov si, fm_s_fmtq
    jmp short .fs_stat
%endif
.fs_st_del:
    mov si, fm_s_delpr          ; "Delete NAME? Enter=yes Esc=no", or
    call fm_scat                ; "Delete NAME +contents? ..." for a folder
    mov si, fm_onam
    call fm_scat
    mov si, fm_s_delq
    cmp byte [fm_odir], 0
    je .fs_stat
    mov si, fm_s_delq2
    jmp short .fs_stat
    ; --- 3. the resting state: this folder's size and the volume's free
    ;     space (SPEC.md 22.7). The two rungs that used to sit between here
    ;     and the editor walked a word table by index, and a code past the end
    ;     of it is what once made this whole ladder go SILENT - LD_EBAD
    ;     doubled through 2, 4, 8, 16, failed the bound and fell out of a
    ;     branch that draws nothing. toast_say carries that bound now, in one
    ;     place, for both tables.
.fs_st_space:
    push bx
    mov bx, [fm_vp]
    mov si, fm_s_size
    call fm_scat
    mov ax, [bx+FS_USED]
    call fm_kbfig
    mov si, fm_s_free
    call fm_scat
    mov ax, [bx+FS_FREE]
    call fm_kbfig
    pop bx
    mov byte [di], 0
    jmp short .fs_stat_drawn

.fs_stat:
    call fm_scat                ; stage it, then draw it on the status line
.fs_stat_drawn:
    mov dx, [fm_cy]
    add dx, [fm_staty]
    call fm_stat_line
.fs_done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_stat_line - draw fm_hdrbuf at (content x + 6, DX), truncated to the
;                window's live width
; in:  fm_hdrbuf staged, DX = absolute y; fm_layout has run; lock held
; out: nothing (all registers preserved)
;
; A proc rather than the tail of fm_draw_status because the replace question
; (SPEC.md 22.3) needs it TWICE, on two rows. The truncation is not optional:
; 'Out of memory' is 104px and cw can legally be 94 (SPEC.md 22).
; -----------------------------------------------------------------------------
fm_stat_line:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [fm_hdrbuf], 0
    je .out
    mov ax, [fm_cw]
    sub ax, 12
    jg .pos
    xor ax, ax
.pos:
    mov cl, 3
    shr ax, cl                  ; AX = characters that fit
    cmp ax, FM_HDRMAX-1
    jae .draw
    mov bx, ax
    mov byte [fm_hdrbuf+bx], 0
.draw:
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov si, fm_hdrbuf
    call KERNEL_SEG:cw_font_str
.out:
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
; fm_kbfig - stage one KB figure, or '?' for the not-known sentinel
; in:  AX = KB (0xFFFF = not known), DI = destination
; out: DI -> the stored NUL
; clobbers: DI (the output), flags
;
; The sentinel is drawn rather than skipped: a Disk window whose mount failed
; still has a status line, and a blank one there reads as "no free space" -
; the one thing it must not be mistaken for (SPEC.md 22.7).
; -----------------------------------------------------------------------------
fm_kbfig:
    cmp ax, 0xFFFF
    je .unknown
    call fm_utoa
    mov byte [di], 'K'
    inc di
    mov byte [di], 0
    ret
.unknown:
    mov byte [di], '?'
    inc di
    mov byte [di], 0
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
; fm_ultoa - unsigned dword to decimal ASCII, no leading zeros
; in:  DX:AX = value, DI = destination (needs up to 11 bytes: 10 digits + NUL)
; out: DI -> the stored NUL
; clobbers: nothing else (internal helper)
; fm_utoa's shape with the 32-bit divide done in two steps: hi/10 first,
; its remainder riding into the lo divide as the high half, so the pushed
; digit is the true dword remainder and the loop repeats on the quotient.
; -----------------------------------------------------------------------------
fm_ultoa_x:
    push ax
    push bx
    push cx
    push dx
    push si

    mov si, 10
    xor cx, cx                  ; CX = digit count
.div:
    mov bx, ax                  ; BX = lo, briefly
    mov ax, dx
    xor dx, dx
    div si                      ; AX = hi quotient, DX = its remainder
    xchg ax, bx                 ; AX = lo, BX = hi quotient
    div si                      ; (rem:lo)/10 -> AX = lo quotient, DX = digit
    add dl, '0'
    push dx
    inc cx
    mov dx, bx                  ; DX:AX = the running quotient
    mov bx, ax
    or bx, dx
    jnz .div
.emit:
    pop ax
    mov [di], al
    inc di
    loop .emit
    mov byte [di], 0

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

; --- per-window state (SPEC.md 22): the kind's KD_POOL, one block per window
fmv_ux1:    resw 1              ; fmv_repaint_all's pending union: the rect
fmv_uy1:    resw 1              ; every Disk window's frame is folded into,
fmv_ux2:    resw 1              ; spent as ONE wm_paint_dmg so that no window
fmv_uy2:    resw 1              ; is painted twice
fmv_uany:   resb 1              ; 0 = nothing folded in yet
fm_pool:    resb 96             ; FM_MAXWIN x FS_SIZE (4 x 24; a literal per
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

fm_tdirty:  resw 1              ; the window whose caption fm_settitle just
                                ; rewrote and nobody has drawn yet, or 0. A
                                ; POINTER, not a flag: fmv_sync can retitle a
                                ; window from ld_run, which holds no lock, so
                                ; the redraw is spent later by whichever of
                                ; fm_repaint / files_poster gets there first -
                                ; and it must not be spendable on some other
                                ; window that repainted in between
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

; --- fm_layout's mirrors of the acting window's hot fields (SPEC.md 22.1) ---
; Read-only snapshots refreshed at the head of every fm_layout, because
; fm_draw_core has no register free to carry a state-block pointer through
; 400 lines. fm_lscr is the one that is written, and fm_clamp_scroll is the
; one place it is written back into the block.
fm_lview:   resb 1              ; FS_VIEW
fm_lmok:    resb 1              ; FS_MOK
fm_ldrv:    resb 1              ; FS_DRV
fm_ledit:   resb 1              ; FS_EDIT
fm_wased:   resb 1              ; NOT a mirror: FS_EDIT as it stood BEFORE
                                ; fm_onclick's fm_edit_end cleared it. A row
                                ; click changes two selection bands and, if an
                                ; editor line was up, the status line - this
                                ; is how the fast path knows which (22.2). It
                                ; also keeps the words below even-aligned,
                                ; which is all the pad it replaced ever did
fm_lnf:     resw 1              ; FS_N
fm_lscr:    resw 1              ; FS_SCRL
fm_lsel:    resw 1              ; FS_SEL
fm_statowed: resb 1             ; 1 = this click or keystroke retired a
                                ; verdict, so the status line owes a redraw
                                ; that the cheap paths do not provide - the
                                ; two selection bands (SPEC.md 22.9) and now
                                ; a scroll, which draws no text at all (22.11)
fm_cmdline: resb 1              ; 1 = the FMC_* just run changed the status
                                ; LINE and nothing else on the window
%ifndef KERN_SMALL
fm_fmtrow:  resb 1              ; the dskw_fmt_tab row the FORMAT confirmation
                                ; (SPEC.md 22.12) was built from, banked when
                                ; the mode was armed. Not per-window, because
                                ; fm_edit_arm clears every OTHER block's
                                ; FS_EDIT - at most one confirmation exists in
                                ; the machine at a time, which is the same
                                ; thing that lets fm_onam and fm_ebuf be
                                ; single copies
%endif

fm_pressx:  resw 1              ; fm_onclick's press point, ABSOLUTE - the
fm_pressy:  resw 1              ; drag tracks in screen coordinates (22.4)
fm_dgidx:   resw 1              ; fm_drag: the row it began on...
fm_dgwin:   resw 1              ; ...the window it began in...
fm_startx:  resw 1              ; ...and where the button went down
fm_starty:  resw 1
fm_dgx:     resw 1              ; where the outline is DRAWN (fm_dgxor)
fm_dgy:     resw 1
fm_dgon:    resb 1              ; 1 = the outline is on screen
fm_dgdrv:   resb 1              ; the drop target's drive, banked past fm_hit

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
fm_rlo:     resw 1              ; draw scratch: the visible rows fm_draw_lrows
fm_rhi:     resw 1              ; is to letter, [lo, hi) - every one of them
                                ; for a full paint, the ones a blit EXPOSED
                                ; for a scroll (SPEC.md 22.11)
fm_sdlt:    resw 1              ; scroll scratch: the signed row delta the
                                ; clamp actually took, which is what all of
                                ; the drawing below it is a function of
fm_bx1:     resw 1              ; scroll scratch: the blit band, rounded in
fm_bx2:     resw 1              ; to byte columns and stopping short of the
fm_by1:     resw 1              ; scroll bar; y covers whole rows only
fm_by2:     resw 1
fm_thnew:   resw 1              ; scroll scratch: where the thumb is going
fm_thh:     resw 1              ; ...and its height, which a scroll cannot
                                ; change (fm_sb_thumb)
fm_fsize:   resw 1              ; fm_stage_name: entry size, bytes (lo word)
fm_fsizeh:  resw 1              ; ...and its high word (SPEC.md 19)
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
fm_numbuf:  resb 11             ; fm_ultoa scratch: 10 digits + NUL

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
fmf_files_init:         call files_init_x
                    retf
%ifndef KERN_SMALL
fmf_fm_bar_gate:        call fm_bar_gate_x
                    retf
%endif
fmf_files_open:         call files_open_x
                    retf
fmf_files_open_drive:   call files_open_drive_x
                    retf
fmf_files_poster:       call files_poster_x
                    retf
fmf_files_refresh:      call files_refresh_x
                    retf
fmf_fm_focus:           call fm_focus_x
                    retf                ; CF is the answer, and neither
                                        ; `call` nor `retf` touches it
fmf_fm_kinit:           call fm_kinit_x
                    retf
fmf_fm_onclick:         call fm_onclick_x
                    retf
fmf_fm_oncmd:           call fm_oncmd_x
                    retf
fmf_fm_onkey:           call fm_onkey_x
                    retf
fmf_fm_paint:           call fm_paint_x
                    retf
fmf_fm_rclick:          call fm_rclick_x
                    retf
fmf_fm_rcmd:            call fm_rcmd_x
                    retf
fmf_fmv_sync:           call fmv_sync_x
                    retf

section .text
